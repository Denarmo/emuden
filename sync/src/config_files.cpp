// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The settings as config files: what syncs of settings.json and the console and game layers,
// and how a downloaded one is applied (device-only values stay as they are).

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "den/base/files.h"
#include "den/store/settings.h"
#include "den/sync/plan.h"
#include "den/system/settings.h"

namespace den::sync {

namespace {

namespace fs = std::filesystem;

// Prefixes of keys that describe this device, not the user's taste.
constexpr const char* DEVICE_ONLY[] = {
    "sync.",           "app.",           "window.",        "display.",      "paths.",         "input.",
    "plugins.",        "engine.",        "setup.",        "settings.",      "apps.",
    "beside.",         "automation.",    "library.folders", "ui.scale",     "ui.text_scale",
    "ui.companion.",   "ui.keyboard",    "player.dual_screen.", "player.swap_displays",
    "player.overlay_on_second_screen",   "player.refresh_hz",   "player.audio_latency_ms",
    "player.screen_size", "player.volume",
};

// A fraction with 9 significant digits: what a float setting holds. The last bits of a double
// move a little each time the settings are read and written again, which would make the file
// differ on every run and go up (or conflict) every time.
JsonValue Steady(const JsonValue& value) {
    if (value.kind == JsonValue::Kind::Number && value.number != std::trunc(value.number)) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.9g", value.number);
        if (const auto parsed = ParseJson(text)) {
            return *parsed;
        }
    }
    if (value.kind == JsonValue::Kind::Array) {
        JsonValue out = value;
        for (JsonValue& item : out.items) {
            item = Steady(item);
        }
        return out;
    }
    return value;
}

std::vector<std::string> Parts(const std::string& key) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t dot = key.find('.'); dot != std::string::npos; dot = key.find('.', start)) {
        parts.push_back(key.substr(start, dot - start));
        start = dot + 1;
    }
    parts.push_back(key.substr(start));
    return parts;
}

class SettingsFiles final : public ConfigFiles {
public:
    SettingsFiles(store::Settings* settings, system::SettingsStore* layers, fs::path settings_file,
                  std::set<std::string> pc)
        : settings_(settings), layers_(layers), settings_file_(std::move(settings_file)), pc_(std::move(pc)) {}

    std::vector<LocalItem> Export(const GameIndex& games) override {
        std::vector<LocalItem> out;
        if (settings_ != nullptr) {
            LocalItem item;
            item.path = "config/settings.json";
            item.text = WriteJson(Portable(settings_->StoredValues()));
            item.modified = FileTime(settings_file_);
            out.push_back(std::move(item));
        }
        if (layers_ == nullptr) {
            return out;
        }
        const std::int64_t modified = FileTime(layers_->OverridesFile());
        JsonValue consoles = JsonObject();
        for (const std::string& system : layers_->Scopes(system::Layer::Console)) {
            if (!pc_.contains(system)) {
                JsonValue values = Portable(layers_->Overrides(system::Scope::Console(system)));
                if (!values.members.empty()) {
                    consoles.members.emplace_back(system, std::move(values));
                }
            }
        }
        LocalItem overrides;
        overrides.path = "config/settings-overrides.json";
        overrides.text = WriteJson(JsonObject({{"version", JsonNumber(1)}, {"consoles", std::move(consoles)}}));
        overrides.modified = modified;
        out.push_back(std::move(overrides));
        for (const std::string& id : layers_->Scopes(system::Layer::Game)) {
            const GameRef* game = games.ById(id);
            if (game == nullptr) {
                continue;
            }
            JsonValue values = Portable(layers_->Overrides(system::Scope::Game(game->system, id)));
            if (values.members.empty()) {
                continue;
            }
            LocalItem item;
            item.path = "config/games/" + game->system + "/" + game->stem + ".json";
            item.text = WriteJson(values);
            item.modified = modified;
            item.game_id = id;
            out.push_back(std::move(item));
        }
        return out;
    }

    Status Import(const std::string& path, const std::string& text, const GameIndex& games) override {
        const auto root = ParseJson(text);
        if (!root || root->kind != JsonValue::Kind::Object) {
            return Status::Fail(path + " is not a settings file");
        }
        if (path == "config/settings.json") {
            if (settings_ == nullptr) {
                return Status::Fail("there are no settings to apply it to");
            }
            const JsonValue values = Portable(*root);
            settings_->SetValues(values.members);
            return {};
        }
        if (layers_ == nullptr) {
            return Status::Fail("there are no settings to apply it to");
        }
        if (path == "config/settings-overrides.json") {
            const JsonValue* consoles = root->Find("consoles");
            if (consoles != nullptr && consoles->kind == JsonValue::Kind::Object) {
                for (const auto& [system, values] : consoles->members) {
                    if (!pc_.contains(system) && values.kind == JsonValue::Kind::Object) {
                        Merge(system::Scope::Console(system), values);
                    }
                }
            }
            return {};
        }
        const std::string system = SystemOf(path);
        const std::string name = path.substr(path.rfind('/') + 1);
        const GameRef* game = name.ends_with(".json") ? games.Find(system, name.substr(0, name.size() - 5)) : nullptr;
        if (game == nullptr) {
            return Status::Fail("that game is not in this library");
        }
        Merge(system::Scope::Game(game->system, game->id), *root);
        return {};
    }

private:
    bool Shared(const std::string& key) const {
        if (IsDeviceOnlySetting(key)) {
            return false;
        }
        for (const std::string& part : Parts(key)) {
            if (pc_.contains(part)) {
                return false;
            }
        }
        return true;
    }

    // The members that sync, sorted so that equal settings give equal files; list entries
    // naming a game of a PC console ("steam:...") are cut out.
    JsonValue Portable(const JsonValue& values) const {
        JsonValue out = JsonObject();
        for (const auto& [key, value] : values.members) {
            if (!Shared(key)) {
                continue;
            }
            JsonValue kept = Steady(value);
            if (kept.kind == JsonValue::Kind::Array) {
                std::erase_if(kept.items, [&](const JsonValue& item) {
                    const std::size_t colon = item.string.find(':');
                    return item.kind == JsonValue::Kind::String && colon != std::string::npos &&
                           pc_.contains(item.string.substr(0, colon));
                });
            }
            out.members.emplace_back(key, std::move(kept));
        }
        std::sort(out.members.begin(), out.members.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        return out;
    }

    // The remote layer's shared values with this device's own ones.
    void Merge(const system::Scope& scope, const JsonValue& remote) {
        JsonValue merged = Portable(remote);
        for (const auto& [key, value] : layers_->Overrides(scope).members) {
            if (!Shared(key)) {
                merged.members.emplace_back(key, value);
            }
        }
        layers_->SetOverrides(scope, merged);
    }

    store::Settings* settings_;
    system::SettingsStore* layers_;
    fs::path settings_file_;
    std::set<std::string> pc_;
};

} // namespace

namespace {

JsonValue Merge3(const JsonValue* base, const JsonValue& local, const JsonValue& remote) {
    using Kind = JsonValue::Kind;
    if (local.kind == Kind::Object && remote.kind == Kind::Object) {
        JsonValue out = JsonObject();
        std::vector<std::string> keys;
        for (const auto& [key, value] : remote.members) {
            keys.push_back(key);
        }
        for (const auto& [key, value] : local.members) {
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
            }
        }
        std::sort(keys.begin(), keys.end());
        for (const std::string& key : keys) {
            const JsonValue* in_base = base != nullptr && base->kind == Kind::Object ? base->Find(key) : nullptr;
            const JsonValue* here = local.Find(key);
            const JsonValue* there = remote.Find(key);
            if (here != nullptr && there != nullptr) {
                out.members.emplace_back(key, Merge3(in_base, *here, *there));
            } else if (here != nullptr) {
                // Only here: new here, or removed there (then it goes unless it changed here).
                if (in_base == nullptr || WriteJson(*in_base) != WriteJson(*here)) {
                    out.members.emplace_back(key, *here);
                }
            } else if (in_base == nullptr || WriteJson(*in_base) != WriteJson(*there)) {
                out.members.emplace_back(key, *there); // new there, or removed here but changed there
            }
        }
        return out;
    }
    const bool changed_here = base == nullptr ? false : WriteJson(*base) != WriteJson(local);
    return changed_here ? local : remote;
}

} // namespace

std::string MergeSettings(const std::string& base, const std::string& local, const std::string& remote) {
    const auto b = ParseJson(base);
    const auto l = ParseJson(local);
    const auto r = ParseJson(remote);
    if (!l || !r || l->kind != JsonValue::Kind::Object || r->kind != JsonValue::Kind::Object) {
        return {};
    }
    return WriteJson(Merge3(b ? &*b : nullptr, *l, *r));
}

bool IsDeviceOnlySetting(const std::string& key) {
    // The renderer a core draws with (core_options.<core>... and game_options.<system>.<game>...)
    // depends on this device's GPU and drivers.
    if (key.ends_with(".emuden_renderer")) {
        return true;
    }
    for (const char* prefix : DEVICE_ONLY) {
        if (key.starts_with(prefix)) {
            return true;
        }
    }
    const system::KeyInfo* info = system::FindKey(key);
    return info != nullptr && info->meta.device_only;
}

std::unique_ptr<ConfigFiles> CreateSettingsFiles(store::Settings* settings, system::SettingsStore* layers,
                                                 fs::path settings_file, std::set<std::string> pc) {
    return std::make_unique<SettingsFiles>(settings, layers, std::move(settings_file), std::move(pc));
}

} // namespace den::sync
