// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The app's settings in layers: a value is looked up per game, then per console, then
// globally, then falls back to the key's default. A feature declares its keys next to its
// code as typed objects, which registers them:
//
//     const den::system::Key<bool> REWIND{"player.rewind", true,
//         {.label = "Rewind", .section = "Player", .per_console = true, .per_game = true}};
//     ...
//     if (den::system::Store().Get(REWIND, den::system::Scope::Game("gba", id))) ...
//
// The global layer is the app's settings.json (den::store::Settings, shared with code that
// uses it directly); console and game overrides live in "settings-overrides.json".

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "den/base/json.h"

namespace den::store {
class Settings;
}

namespace den::system {

enum class Kind { Bool, Number, Text, Choice };
enum class Layer { Default, Global, Console, Game };

/// Where a value applies. The default is the global layer.
struct Scope {
    std::string system; // console (plugin system id); empty = global
    std::string game;   // library entry id; empty = the console

    static Scope Global() {
        return {};
    }
    static Scope Console(std::string system) {
        return {std::move(system), {}};
    }
    static Scope Game(std::string system, std::string game) {
        return {std::move(system), std::move(game)};
    }
    Layer layer() const {
        return !game.empty() ? Layer::Game : !system.empty() ? Layer::Console : Layer::Global;
    }
};

/// What a settings page needs to show a key.
struct KeyMeta {
    std::string label;
    std::string description;
    std::string section;              // the settings section it is listed under
    std::vector<std::string> options; // Choice: the allowed values
    double min = 0, max = 1, step = 0.1; // Number
    bool per_console = false;         // may be overridden per console
    bool per_game = false;            // may be overridden per game
    bool device_only = false;         // never leaves this device (sync)
};

struct KeyInfo {
    std::string key; // dotted, "player.rewind"
    Kind kind = Kind::Text;
    JsonValue fallback;
    KeyMeta meta;
};

/// Every key declared in the program, in declaration order within a section.
const std::vector<KeyInfo>& Keys();
const KeyInfo* FindKey(const std::string& key);
std::vector<KeyInfo> KeysIn(const std::string& section);
/// Declares a key; a second declaration of the same key is ignored.
void RegisterKey(KeyInfo info);

namespace detail {
inline JsonValue ToJson(bool value) {
    return JsonBool(value);
}
inline JsonValue ToJson(double value) {
    return JsonNumber(value);
}
inline JsonValue ToJson(int value) {
    return JsonNumber(value);
}
inline JsonValue ToJson(const std::string& value) {
    return JsonString(value);
}
inline void FromJson(const JsonValue& json, bool& out) {
    out = json.kind == JsonValue::Kind::Bool ? json.boolean : out;
}
inline void FromJson(const JsonValue& json, double& out) {
    out = json.kind == JsonValue::Kind::Number ? json.number : out;
}
inline void FromJson(const JsonValue& json, int& out) {
    out = json.kind == JsonValue::Kind::Number ? static_cast<int>(json.number) : out;
}
inline void FromJson(const JsonValue& json, std::string& out) {
    out = json.kind == JsonValue::Kind::String ? json.string : out;
}
inline Kind KindOf(const bool&, const KeyMeta&) {
    return Kind::Bool;
}
inline Kind KindOf(const double&, const KeyMeta&) {
    return Kind::Number;
}
inline Kind KindOf(const int&, const KeyMeta&) {
    return Kind::Number;
}
inline Kind KindOf(const std::string&, const KeyMeta& meta) {
    return meta.options.empty() ? Kind::Text : Kind::Choice;
}
} // namespace detail

/// A typed key: bool, int, double or std::string (a Choice when the meta lists options).
template <typename T>
class Key {
public:
    Key(std::string key, T fallback, KeyMeta meta = {}) : key_(std::move(key)), fallback_(fallback) {
        RegisterKey({key_, detail::KindOf(fallback_, meta), detail::ToJson(fallback_), std::move(meta)});
    }
    const std::string& name() const {
        return key_;
    }
    const T& fallback() const {
        return fallback_;
    }

private:
    std::string key_;
    T fallback_;
};

class SettingsStore {
public:
    /// `global` outlives the store. `overrides_file` holds the console and game layers.
    SettingsStore(store::Settings* global, std::filesystem::path overrides_file);
    ~SettingsStore();

    /// The value in effect for the scope.
    template <typename T>
    T Get(const Key<T>& key, const Scope& scope = {}) const {
        T out = key.fallback();
        detail::FromJson(Resolve(key.name(), scope), out);
        return out;
    }
    /// Sets the value in the scope's own layer.
    template <typename T>
    void Set(const Key<T>& key, const T& value, const Scope& scope = {}) {
        SetJson(key.name(), detail::ToJson(value), scope);
    }

    // The same by name, for pages built from KeyInfo.
    /// The value in effect; the key's default when no layer sets it (null for an unknown key).
    JsonValue Resolve(const std::string& key, const Scope& scope = {}) const;
    void SetJson(const std::string& key, JsonValue value, const Scope& scope = {});
    /// Removes the value from the scope's own layer, so the layer below shows through.
    void Clear(const std::string& key, const Scope& scope = {});
    /// Whether the scope's own layer sets the key.
    bool IsSet(const std::string& key, const Scope& scope = {}) const;
    /// The layer the value in effect comes from.
    Layer Source(const std::string& key, const Scope& scope = {}) const;
    /// Drops every override of a game (it left the library) or of a console.
    void ClearScope(const Scope& scope);

    /// Grows on every change.
    std::uint64_t Revision() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The app's store. InitSettings is called once at startup, before any feature runs.
void InitSettings(store::Settings* global, const std::filesystem::path& data_dir);
void ShutdownSettings();
SettingsStore& Store();
bool HasStore();

} // namespace den::system
