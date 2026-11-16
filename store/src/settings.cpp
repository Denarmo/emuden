// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>
#include <mutex>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/store/settings.h"

namespace den::store {

struct Settings::Impl {
    std::filesystem::path file;
    mutable std::mutex mutex;
    JsonValue root = JsonObject();
    std::map<std::string, JsonValue> defaults;

    // The value in effect: the stored one, else the default.
    const JsonValue* Find(const std::string& key) const {
        if (const JsonValue* stored = root.Find(key)) {
            return stored;
        }
        const auto fallback = defaults.find(key);
        return fallback == defaults.end() ? nullptr : &fallback->second;
    }

    void Set(const std::string& key, JsonValue value) {
        std::lock_guard lock(mutex);
        for (auto& [name, existing] : root.members) {
            if (name == key) {
                existing = std::move(value);
                Save();
                return;
            }
        }
        root.members.emplace_back(key, std::move(value));
        Save();
    }

    void Save() {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        WriteFileAtomic(file, WriteJson(root));
    }
};

Settings::Settings(std::filesystem::path file) : impl_(std::make_unique<Impl>()) {
    impl_->file = std::move(file);
    if (const auto text = ReadTextFile(impl_->file)) {
        if (auto parsed = ParseJson(*text); parsed && parsed->kind == JsonValue::Kind::Object) {
            impl_->root = std::move(*parsed);
        }
    }
}

Settings::~Settings() = default;

bool Settings::Has(const std::string& key) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->Find(key) != nullptr;
}

bool Settings::Stored(const std::string& key) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->root.Find(key) != nullptr;
}

void Settings::SetDefaults(std::vector<std::pair<std::string, JsonValue>> defaults) {
    std::lock_guard lock(impl_->mutex);
    impl_->defaults.clear();
    for (auto& [key, value] : defaults) {
        impl_->defaults[key] = std::move(value);
    }
}

std::string Settings::GetString(const std::string& key, const std::string& fallback) const {
    std::lock_guard lock(impl_->mutex);
    const JsonValue* value = impl_->Find(key);
    return value && value->kind == JsonValue::Kind::String ? value->string : fallback;
}

bool Settings::GetBool(const std::string& key, bool fallback) const {
    std::lock_guard lock(impl_->mutex);
    const JsonValue* value = impl_->Find(key);
    return value && value->kind == JsonValue::Kind::Bool ? value->boolean : fallback;
}

double Settings::GetNumber(const std::string& key, double fallback) const {
    std::lock_guard lock(impl_->mutex);
    const JsonValue* value = impl_->Find(key);
    return value && value->kind == JsonValue::Kind::Number ? value->number : fallback;
}

std::vector<std::string> Settings::GetList(const std::string& key) const {
    std::lock_guard lock(impl_->mutex);
    std::vector<std::string> out;
    const JsonValue* value = impl_->Find(key);
    if (value && value->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : value->items) {
            if (item.kind == JsonValue::Kind::String) {
                out.push_back(item.string);
            }
        }
    }
    return out;
}

void Settings::SetString(const std::string& key, const std::string& value) {
    impl_->Set(key, JsonString(value));
}

void Settings::SetBool(const std::string& key, bool value) {
    impl_->Set(key, JsonBool(value));
}

void Settings::SetNumber(const std::string& key, double value) {
    impl_->Set(key, JsonNumber(value));
}

void Settings::SetList(const std::string& key, const std::vector<std::string>& values) {
    std::vector<JsonValue> items;
    for (const std::string& value : values) {
        items.push_back(JsonString(value));
    }
    impl_->Set(key, JsonArray(std::move(items)));
}

JsonValue Settings::StoredValues() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->root;
}

void Settings::SetValues(const std::vector<std::pair<std::string, JsonValue>>& values) {
    std::lock_guard lock(impl_->mutex);
    for (const auto& [key, value] : values) {
        bool found = false;
        for (auto& [name, existing] : impl_->root.members) {
            if (name == key) {
                existing = value;
                found = true;
                break;
            }
        }
        if (!found) {
            impl_->root.members.emplace_back(key, value);
        }
    }
    impl_->Save();
}

void Settings::Remove(const std::string& key) {
    std::lock_guard lock(impl_->mutex);
    std::erase_if(impl_->root.members, [&](const auto& member) { return member.first == key; });
    impl_->Save();
}

} // namespace den::store
