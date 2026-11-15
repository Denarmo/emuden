// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Secrets on Linux and other desktops: one JSON file in the data folder that only the user
// may read (0600), written atomically.

#include <sys/stat.h>

#include <mutex>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/platform/pal.h"

namespace den::pal {

namespace {

constexpr std::size_t MAX_VALUE = 2048;

class FileSecrets final : public Secrets {
public:
    std::optional<std::string> Get(const std::string& key) override {
        std::lock_guard lock(mutex_);
        const JsonValue root = Load();
        const JsonValue* value = root.Find(key);
        if (value == nullptr || value->kind != JsonValue::Kind::String) {
            return std::nullopt;
        }
        return value->string;
    }
    Status Set(const std::string& key, const std::string& value) override {
        if (value.size() > MAX_VALUE) {
            return Status::Fail("the secret is larger than 2 KiB");
        }
        std::lock_guard lock(mutex_);
        JsonValue root = Load();
        std::erase_if(root.members, [&](const auto& member) { return member.first == key; });
        root.members.emplace_back(key, JsonString(value));
        return Save(root) ? Status() : Status::Fail("the secrets file could not be written");
    }
    void Remove(const std::string& key) override {
        std::lock_guard lock(mutex_);
        JsonValue root = Load();
        if (std::erase_if(root.members, [&](const auto& member) { return member.first == key; }) > 0) {
            Save(root);
        }
    }

private:
    static std::filesystem::path File() {
        return DataDir() / "secrets.json";
    }
    static JsonValue Load() {
        if (const auto text = ReadTextFile(File())) {
            if (auto root = ParseJson(*text); root && root->kind == JsonValue::Kind::Object) {
                return std::move(*root);
            }
        }
        return JsonObject();
    }
    static bool Save(const JsonValue& root) {
        const mode_t old = umask(077); // the temporary file is created 0600 already
        const bool written = WriteFileAtomic(File(), WriteJson(root));
        umask(old);
        chmod(File().c_str(), 0600);
        return written;
    }

    std::mutex mutex_;
};

} // namespace

Secrets& GetSecrets() {
    static FileSecrets secrets;
    return secrets;
}

} // namespace den::pal
