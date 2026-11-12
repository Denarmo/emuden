// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>
#include <mutex>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/store/settings.h"
#include "den/system/settings.h"

namespace den::system {

namespace {

constexpr int FILE_VERSION = 1;
constexpr const char* GLOBAL_VERSION_KEY = "settings.version";

using Values = std::map<std::string, JsonValue>;

Values ReadValues(const JsonValue& object) {
    Values out;
    for (const auto& [key, value] : object.members) {
        out[key] = value;
    }
    return out;
}

JsonValue WriteLayer(const std::map<std::string, Values>& layer) {
    JsonValue out = JsonObject();
    for (const auto& [name, values] : layer) {
        if (values.empty()) {
            continue;
        }
        JsonValue object = JsonObject();
        for (const auto& [key, value] : values) {
            object.members.emplace_back(key, value);
        }
        out.members.emplace_back(name, std::move(object));
    }
    return out;
}

void ReadLayer(const JsonValue* json, std::map<std::string, Values>& layer) {
    if (json && json->kind == JsonValue::Kind::Object) {
        for (const auto& [name, object] : json->members) {
            layer[name] = ReadValues(object);
        }
    }
}

} // namespace

struct SettingsStore::Impl {
    store::Settings* global = nullptr;
    std::filesystem::path file;
    mutable std::mutex mutex;
    std::map<std::string, Values> consoles, games;
    std::uint64_t revision = 1;
    bool writable = true;

    void Load() {
        const auto text = ReadTextFile(file);
        if (!text) {
            return;
        }
        const auto root = ParseJson(*text);
        if (!root || root->kind != JsonValue::Kind::Object) {
            log::Warn("settings", "{} is not readable; overrides start empty", PathToUtf8(file));
            return;
        }
        const JsonValue* version = root->Find("version");
        if (version && version->number > FILE_VERSION) {
            // Written by a newer app: use nothing of it and leave the file as it is.
            log::Warn("settings", "{} is from a newer version; overrides are not saved",
                      PathToUtf8(file));
            writable = false;
            return;
        }
        ReadLayer(root->Find("consoles"), consoles);
        ReadLayer(root->Find("games"), games);
    }

    void Save() {
        ++revision;
        if (!writable) {
            return;
        }
        const JsonValue root = JsonObject({{"version", JsonNumber(FILE_VERSION)},
                                           {"consoles", WriteLayer(consoles)},
                                           {"games", WriteLayer(games)}});
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        WriteFileAtomic(file, WriteJson(root));
    }

    const JsonValue* InLayer(const std::map<std::string, Values>& layer, const std::string& name,
                             const std::string& key) const {
        if (name.empty()) {
            return nullptr;
        }
        const auto values = layer.find(name);
        if (values == layer.end()) {
            return nullptr;
        }
        const auto value = values->second.find(key);
        return value == values->second.end() ? nullptr : &value->second;
    }

    // The global layer's value, read with the type the key's default has.
    bool Global(const KeyInfo* info, const std::string& key, JsonValue& out) const {
        if (!global || !global->Has(key)) {
            return false;
        }
        const JsonValue::Kind kind = info ? info->fallback.kind : JsonValue::Kind::String;
        if (kind == JsonValue::Kind::Bool) {
            out = JsonBool(global->GetBool(key, info->fallback.boolean));
        } else if (kind == JsonValue::Kind::Number) {
            out = JsonNumber(global->GetNumber(key, info->fallback.number));
        } else {
            out = JsonString(global->GetString(key, info ? info->fallback.string : std::string()));
        }
        return true;
    }

    Layer Find(const std::string& key, const Scope& scope, JsonValue& out) const {
        const KeyInfo* info = FindKey(key);
        if (const JsonValue* value = InLayer(games, scope.game, key)) {
            out = *value;
            return Layer::Game;
        }
        if (const JsonValue* value = InLayer(consoles, scope.system, key)) {
            out = *value;
            return Layer::Console;
        }
        if (Global(info, key, out)) {
            return Layer::Global;
        }
        out = info ? info->fallback : JsonValue();
        return Layer::Default;
    }
};

SettingsStore::SettingsStore(store::Settings* global, std::filesystem::path overrides_file)
    : impl_(std::make_unique<Impl>()) {
    impl_->global = global;
    impl_->file = std::move(overrides_file);
    impl_->Load();
    if (global && !global->Has(GLOBAL_VERSION_KEY)) {
        global->SetNumber(GLOBAL_VERSION_KEY, FILE_VERSION);
    }
}

SettingsStore::~SettingsStore() = default;

JsonValue SettingsStore::Resolve(const std::string& key, const Scope& scope) const {
    std::lock_guard lock(impl_->mutex);
    JsonValue out;
    impl_->Find(key, scope, out);
    return out;
}

Layer SettingsStore::Source(const std::string& key, const Scope& scope) const {
    std::lock_guard lock(impl_->mutex);
    JsonValue out;
    return impl_->Find(key, scope, out);
}

void SettingsStore::SetJson(const std::string& key, JsonValue value, const Scope& scope) {
    std::lock_guard lock(impl_->mutex);
    switch (scope.layer()) {
    case Layer::Game:
        impl_->games[scope.game][key] = std::move(value);
        impl_->Save();
        return;
    case Layer::Console:
        impl_->consoles[scope.system][key] = std::move(value);
        impl_->Save();
        return;
    default:
        break;
    }
    ++impl_->revision;
    if (!impl_->global) {
        return;
    }
    if (value.kind == JsonValue::Kind::Bool) {
        impl_->global->SetBool(key, value.boolean);
    } else if (value.kind == JsonValue::Kind::Number) {
        impl_->global->SetNumber(key, value.number);
    } else {
        impl_->global->SetString(key, value.string);
    }
}

void SettingsStore::Clear(const std::string& key, const Scope& scope) {
    std::lock_guard lock(impl_->mutex);
    switch (scope.layer()) {
    case Layer::Game:
        if (impl_->games[scope.game].erase(key)) {
            impl_->Save();
        }
        return;
    case Layer::Console:
        if (impl_->consoles[scope.system].erase(key)) {
            impl_->Save();
        }
        return;
    default:
        break;
    }
    ++impl_->revision;
    if (impl_->global) {
        impl_->global->Remove(key);
    }
}

bool SettingsStore::IsSet(const std::string& key, const Scope& scope) const {
    std::lock_guard lock(impl_->mutex);
    switch (scope.layer()) {
    case Layer::Game:
        return impl_->InLayer(impl_->games, scope.game, key) != nullptr;
    case Layer::Console:
        return impl_->InLayer(impl_->consoles, scope.system, key) != nullptr;
    default:
        return impl_->global && impl_->global->Has(key);
    }
}

void SettingsStore::ClearScope(const Scope& scope) {
    std::lock_guard lock(impl_->mutex);
    const Layer layer = scope.layer();
    std::size_t erased = 0;
    if (layer == Layer::Game) {
        erased = impl_->games.erase(scope.game);
    } else if (layer == Layer::Console) {
        erased = impl_->consoles.erase(scope.system);
    }
    if (erased) {
        impl_->Save();
    }
}

std::uint64_t SettingsStore::Revision() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->revision;
}

namespace {
std::unique_ptr<SettingsStore> g_store;
}

void InitSettings(store::Settings* global, const std::filesystem::path& data_dir) {
    g_store = std::make_unique<SettingsStore>(global, data_dir / "settings-overrides.json");
}

void ShutdownSettings() {
    g_store.reset();
}

bool HasStore() {
    return g_store != nullptr;
}

SettingsStore& Store() {
    if (!g_store) {
        // Reached only by code that runs before InitSettings: defaults, nothing saved.
        g_store = std::make_unique<SettingsStore>(nullptr, std::filesystem::path());
        log::Warn("settings", "the settings store was used before InitSettings");
    }
    return *g_store;
}

} // namespace den::system
