// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Play time as config files (docs/sync.md, section 3.2): config/playtime/<device id>.json holds
// what one device played, by portable game key ("gba/Pokemon - Lazarus"). Each device writes
// only its own file. The other devices' files are kept under <data>/sync/playtime, as they
// were downloaded, and added up into the library, so no time is ever counted twice.

#include <algorithm>
#include <cctype>
#include <system_error>

#include "den/base/files.h"
#include "den/base/json.h"
#include "engine_impl.h"

namespace den::sync {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view PREFIX = "config/playtime/";

double Number(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Number ? value->number : 0.0;
}

// Device ids are letters and digits (play files of other forms are not looked at).
bool IsDeviceId(const std::string& id) {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
}

// The device id of config/playtime/<id>.json, "" for another path.
std::string DeviceOf(const std::string& path) {
    if (!path.starts_with(PREFIX) || !path.ends_with(".json")) {
        return {};
    }
    const std::string name = path.substr(PREFIX.size(), path.size() - PREFIX.size() - 5);
    return IsDeviceId(name) ? name : std::string();
}

class PlayFiles final : public ConfigFiles {
public:
    PlayFiles(PlaySource source, fs::path dir, std::string device_id, std::string device_name)
        : source_(std::move(source)), dir_(std::move(dir)), id_(std::move(device_id)),
          name_(std::move(device_name)) {}

    std::vector<LocalItem> Export(const GameIndex& games) override {
        std::vector<LocalItem> out;
        if (!source_.own || id_.empty()) {
            return out;
        }
        std::map<std::string, library::PlayTotals> by_key;
        for (const auto& [id, play] : source_.own()) {
            if (const GameRef* game = games.ById(id)) {
                library::PlayTotals& total = by_key[game->system + "/" + game->stem];
                total.seconds += play.seconds;
                total.count += play.count;
                total.last_played = std::max(total.last_played, play.last_played);
            }
        }
        JsonValue list = JsonObject();
        std::int64_t latest = 0;
        for (const auto& [key, play] : by_key) {
            list.members.emplace_back(key, JsonObject({{"seconds", JsonNumber(static_cast<double>(play.seconds))},
                                                       {"count", JsonNumber(play.count)},
                                                       {"last_played", JsonNumber(static_cast<double>(play.last_played))}}));
            latest = std::max(latest, play.last_played);
        }
        LocalItem own;
        own.path = std::string(PREFIX) + id_ + ".json";
        own.text = WriteJson(JsonObject(
            {{"version", JsonNumber(1)}, {"device", JsonString(name_)}, {"games", std::move(list)}}));
        own.modified = latest;
        out.push_back(std::move(own));
        // The other devices' files as they came: they never change here, so they never go up.
        for (const fs::path& file : ListDirectory(dir_)) {
            const std::string stem = PathToUtf8(file.stem());
            if (file.extension() != ".json" || stem == id_ || !IsDeviceId(stem)) {
                continue;
            }
            LocalItem item;
            item.path = std::string(PREFIX) + stem + ".json";
            item.text = ReadTextFile(file).value_or("");
            item.modified = FileTime(file);
            out.push_back(std::move(item));
        }
        // Every run, so a game added since the last download gets its time too.
        AddUp(games);
        return out;
    }

    Status Import(const std::string& path, const std::string& text, const GameIndex& games) override {
        const std::string device = DeviceOf(path);
        if (device.empty()) {
            return Status::Fail(path + " is not a play time file");
        }
        if (device == id_) {
            return {}; // this device's own file: what it exports is what counts
        }
        const auto root = ParseJson(text);
        if (!root || root->kind != JsonValue::Kind::Object) {
            return Status::Fail(path + " is not a play time file");
        }
        std::error_code ec;
        fs::create_directories(dir_, ec);
        if (!WriteFileAtomic(dir_ / PathFromUtf8(device + ".json"), text)) {
            return Status::Fail(path + " could not be kept");
        }
        AddUp(games);
        return {};
    }

private:
    void AddUp(const GameIndex& games) {
        if (!source_.set_others) {
            return;
        }
        std::map<std::string, library::PlayTotals> by_id;
        for (const fs::path& file : ListDirectory(dir_)) {
            if (file.extension() != ".json" || PathToUtf8(file.stem()) == id_) {
                continue;
            }
            const auto root = ParseJson(ReadTextFile(file).value_or(""));
            const JsonValue* list = root ? root->Find("games") : nullptr;
            if (list == nullptr || list->kind != JsonValue::Kind::Object) {
                continue;
            }
            for (const auto& [key, value] : list->members) {
                const std::size_t slash = key.find('/');
                const GameRef* game =
                    slash == std::string::npos ? nullptr : games.Find(key.substr(0, slash), key.substr(slash + 1));
                if (game == nullptr || value.kind != JsonValue::Kind::Object) {
                    continue;
                }
                library::PlayTotals& total = by_id[game->id];
                total.seconds += std::max<std::int64_t>(static_cast<std::int64_t>(Number(value, "seconds")), 0);
                total.count += std::max(static_cast<int>(Number(value, "count")), 0);
                total.last_played = std::max(total.last_played, static_cast<std::int64_t>(Number(value, "last_played")));
            }
        }
        source_.set_others(by_id);
    }

    PlaySource source_;
    fs::path dir_;
    std::string id_, name_;
};

// The settings files and the play time files as one.
class Both final : public ConfigFiles {
public:
    Both(std::unique_ptr<ConfigFiles> settings, std::unique_ptr<ConfigFiles> play)
        : settings_(std::move(settings)), play_(std::move(play)) {}

    std::vector<LocalItem> Export(const GameIndex& games) override {
        std::vector<LocalItem> out = settings_->Export(games);
        for (LocalItem& item : play_->Export(games)) {
            out.push_back(std::move(item));
        }
        return out;
    }

    Status Import(const std::string& path, const std::string& text, const GameIndex& games) override {
        return path.starts_with(PREFIX) ? play_->Import(path, text, games) : settings_->Import(path, text, games);
    }

private:
    std::unique_ptr<ConfigFiles> settings_, play_;
};

} // namespace

std::unique_ptr<ConfigFiles> CreatePlayFiles(PlaySource source, fs::path dir, std::string device_id,
                                             std::string device_name) {
    return std::make_unique<PlayFiles>(std::move(source), std::move(dir), std::move(device_id),
                                       std::move(device_name));
}

std::unique_ptr<ConfigFiles> CombineConfigFiles(std::unique_ptr<ConfigFiles> settings,
                                                std::unique_ptr<ConfigFiles> play) {
    return std::make_unique<Both>(std::move(settings), std::move(play));
}

std::vector<std::string> OtherDeviceNames(const fs::path& dir, const std::string& own_id) {
    std::vector<std::string> names;
    for (const fs::path& file : ListDirectory(dir)) {
        if (file.extension() != ".json" || PathToUtf8(file.stem()) == own_id) {
            continue;
        }
        const auto root = ParseJson(ReadTextFile(file).value_or(""));
        const std::string name = root ? root->String("device") : std::string();
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    return names;
}

} // namespace den::sync
