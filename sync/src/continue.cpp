// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/sync/continue.h"
#include "den/sync/names.h"
#include "den/sync/plan.h"

namespace den::sync {

namespace fs = std::filesystem;

namespace {

// A resume state written within this long after a session started belongs to that session.
constexpr std::int64_t SESSION_SPAN = 24 * 3600;

fs::path GameFile(const fs::path& data_dir, const std::string& system, const std::string& game_id,
                  const char* suffix) {
    return data_dir / STATES / PathFromUtf8(SafeFileName(system)) / PathFromUtf8(SafeFileName(game_id) + suffix);
}

struct Latest {
    std::string device; // "" = this device
    std::int64_t time = 0;
    std::string key;    // as the play file wrote it
};

} // namespace

fs::path ResumeScreenshot(const fs::path& data_dir, const std::string& system, const std::string& game_id) {
    return GameFile(data_dir, system, game_id, ".resume.png");
}

fs::path ResumeState(const fs::path& data_dir, const std::string& system, const std::string& game_id) {
    return GameFile(data_dir, system, game_id, ".resume.state");
}

std::vector<ContinueItem> ContinueGames(const ContinueRequest& request) {
    std::map<std::string, Latest> by_key;
    std::map<std::string, const library::Entry*> local; // key -> this device's entry
    const auto offer = [&](const std::string& key, const std::string& device, std::int64_t time) {
        Latest& latest = by_key[ToLower(key)];
        if (time > latest.time) {
            latest = {device, time, key};
        }
    };
    for (const library::Entry& entry : request.entries) {
        const std::string stem = PathToUtf8(PathFromUtf8(entry.path).stem());
        if (entry.system.empty() || stem.empty()) {
            continue;
        }
        const std::string key = entry.system + "/" + stem;
        local.emplace(ToLower(key), &entry);
        if (const auto play = request.own_play.find(entry.id); play != request.own_play.end() && play->second.last_played > 0) {
            offer(key, "", play->second.last_played);
        }
    }
    for (const fs::path& file : ListDirectory(request.data_dir / "sync" / "playtime")) {
        if (file.extension() != ".json" || PathToUtf8(file.stem()) == request.own_id) {
            continue;
        }
        const auto root = ParseJson(ReadTextFile(file).value_or(""));
        const JsonValue* games = root ? root->Find("games") : nullptr;
        if (games == nullptr || games->kind != JsonValue::Kind::Object) {
            continue;
        }
        std::string device = root->String("device");
        if (device.empty()) {
            device = "another device";
        }
        for (const auto& [key, value] : games->members) {
            const JsonValue* last = value.kind == JsonValue::Kind::Object ? value.Find("last_played") : nullptr;
            const std::size_t slash = key.find('/');
            if (last == nullptr || last->kind != JsonValue::Kind::Number || slash == std::string::npos ||
                slash == 0 || slash + 1 == key.size() || request.never.contains(key.substr(0, slash))) {
                continue;
            }
            offer(key, device, static_cast<std::int64_t>(last->number));
        }
    }

    std::vector<ContinueItem> out;
    for (const auto& [lower, latest] : by_key) {
        ContinueItem item;
        item.device = latest.device;
        item.time = latest.time;
        if (const auto found = local.find(lower); found != local.end()) {
            const library::Entry& entry = *found->second;
            item.game_id = entry.id;
            item.system = entry.system;
            item.stem = PathToUtf8(PathFromUtf8(entry.path).stem());
            const fs::path state = ResumeState(request.data_dir, entry.system, entry.id);
            item.resume = IsRegularFile(state);
            const std::int64_t saved = item.resume ? FileTime(state) : 0;
            if (saved >= latest.time && saved - latest.time < SESSION_SPAN) {
                item.time = saved;
                const fs::path picture = ResumeScreenshot(request.data_dir, entry.system, entry.id);
                if (IsRegularFile(picture)) {
                    item.screenshot = picture;
                }
            }
        } else {
            // Only another device has it: the names as its play file gave them.
            item.system = latest.key.substr(0, latest.key.find('/'));
            item.stem = latest.key.substr(latest.key.find('/') + 1);
        }
        item.key = item.system + "/" + item.stem;
        out.push_back(std::move(item));
    }
    std::stable_sort(out.begin(), out.end(), [](const ContinueItem& a, const ContinueItem& b) { return a.time > b.time; });
    if (out.size() > request.limit) {
        out.resize(request.limit);
    }
    return out;
}

std::string StateCore(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    char header[9 + 255] = {};
    in.read(header, sizeof(header));
    const std::streamsize got = in.gcount();
    if (got < 9 || std::memcmp(header, "DENSTAT1", 8) != 0) {
        return {};
    }
    const auto size = static_cast<unsigned char>(header[8]);
    return got >= 9 + size ? std::string(header + 9, size) : std::string();
}

ResumeCoreChoice ChooseResumeCore(const ResumeCoreInput& input) {
    if (input.wrote.empty() || EqualsNoCase(input.wrote, input.own)) {
        return ResumeCoreChoice::Same;
    }
    if (input.own_for_game) {
        return ResumeCoreChoice::Unusable;
    }
    return input.wrote_installed ? ResumeCoreChoice::Switch : ResumeCoreChoice::Missing;
}

std::string AgoText(std::int64_t then, std::int64_t now) {
    const std::int64_t ago = std::max<std::int64_t>(now - then, 0);
    if (ago < 60) {
        return "Just now";
    }
    if (ago < 3600) {
        return std::format("{} min ago", ago / 60);
    }
    if (ago < 24 * 3600) {
        return std::format("{} h ago", ago / 3600);
    }
    const std::int64_t days = ago / (24 * 3600);
    if (days == 1) {
        return "Yesterday";
    }
    if (days < 14) {
        return std::format("{} days ago", days);
    }
    if (days < 60) {
        return std::format("{} weeks ago", days / 7);
    }
    return std::format("{} months ago", days / 30);
}

} // namespace den::sync
