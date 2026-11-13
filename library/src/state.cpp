// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/store/document.h"
#include "state.h"

namespace den::library {

namespace {

constexpr int SAVED_VERSION = 1;
// Raised whenever the scan names, ids or groups games differently: an older cache is dropped.
constexpr int CACHE_VERSION = 1;

double Number(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Number ? value->number : 0.0;
}

bool Flag(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Bool && value->boolean;
}

} // namespace

std::optional<Saved> LoadSaved(const std::filesystem::path& file) {
    const auto document = store::LoadDocument(file);
    if (!document) {
        return std::nullopt;
    }
    Saved saved;
    if (const JsonValue* folders = document->data.Find("folders"); folders != nullptr) {
        for (const JsonValue& folder : folders->items) {
            if (std::string path = folder.String("path"); !path.empty()) {
                saved.folders.push_back({std::move(path), folder.String("system")});
            }
        }
    }
    if (const JsonValue* records = document->data.Find("records"); records != nullptr) {
        for (const auto& [id, value] : records->members) {
            Record record;
            record.favourite = Flag(value, "favourite");
            record.last_played = static_cast<std::int64_t>(Number(value, "last_played"));
            record.play_seconds = static_cast<std::int64_t>(Number(value, "play_seconds"));
            record.play_count = static_cast<int>(Number(value, "play_count"));
            record.added = static_cast<std::int64_t>(Number(value, "added"));
            saved.records[id] = record;
        }
    }
    if (const JsonValue* sessions = document->data.Find("sessions"); sessions != nullptr) {
        for (const JsonValue& item : sessions->items) {
            Session session{item.String("id"), static_cast<std::int64_t>(Number(item, "started")),
                            static_cast<std::int64_t>(Number(item, "seconds"))};
            if (!session.id.empty()) {
                saved.sessions.push_back(std::move(session));
            }
        }
    }
    return saved;
}

bool SaveSaved(const std::filesystem::path& file, const Saved& saved) {
    JsonValue folders = JsonArray();
    for (const Folder& folder : saved.folders) {
        folders.items.push_back(
            JsonObject({{"path", JsonString(folder.path)}, {"system", JsonString(folder.system)}}));
    }
    JsonValue records = JsonObject();
    for (const auto& [id, record] : saved.records) {
        records.members.emplace_back(
            id, JsonObject({{"favourite", JsonBool(record.favourite)},
                            {"last_played", JsonNumber(static_cast<double>(record.last_played))},
                            {"play_seconds", JsonNumber(static_cast<double>(record.play_seconds))},
                            {"play_count", JsonNumber(record.play_count)},
                            {"added", JsonNumber(static_cast<double>(record.added))}}));
    }
    JsonValue sessions = JsonArray();
    for (const Session& session : saved.sessions) {
        sessions.items.push_back(
            JsonObject({{"id", JsonString(session.id)},
                        {"started", JsonNumber(static_cast<double>(session.started))},
                        {"seconds", JsonNumber(static_cast<double>(session.seconds))}}));
    }
    return store::SaveDocument(file, SAVED_VERSION,
                               JsonObject({{"folders", std::move(folders)},
                                           {"records", std::move(records)},
                                           {"sessions", std::move(sessions)}}));
}

std::vector<Entry> LoadEntryCache(const std::filesystem::path& file) {
    std::vector<Entry> entries;
    const auto document = store::LoadDocument(file);
    if (!document || document->version != CACHE_VERSION) {
        return entries;
    }
    const JsonValue* list = document->data.Find("entries");
    if (list == nullptr) {
        return entries;
    }
    for (const JsonValue& item : list->items) {
        Entry entry;
        entry.id = item.String("id");
        entry.name = item.String("name");
        entry.system = item.String("system");
        entry.path = item.String("path");
        if (entry.id.empty() || entry.name.empty() || entry.system.empty() || entry.path.empty()) {
            continue;
        }
        entry.cover = item.String("cover");
        entry.size_bytes = static_cast<std::uint64_t>(Number(item, "size"));
        entry.original_name = item.String("original_name");
        entry.archive_member = item.String("archive_member");
        entry.disc = static_cast<int>(Number(item, "disc"));
        entry.placeholder_cover = Flag(item, "placeholder_cover");
        entry.hack_of = item.String("hack_of");
        if (const JsonValue* versions = item.Find("other_versions"); versions != nullptr) {
            for (const JsonValue& version : versions->items) {
                entry.other_versions.push_back(version.string);
            }
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool SaveEntryCache(const std::filesystem::path& file, const std::vector<Entry>& entries) {
    JsonValue list = JsonArray();
    for (const Entry& entry : entries) {
        JsonValue versions = JsonArray();
        for (const std::string& version : entry.other_versions) {
            versions.items.push_back(JsonString(version));
        }
        list.items.push_back(JsonObject({
            {"id", JsonString(entry.id)},
            {"name", JsonString(entry.name)},
            {"system", JsonString(entry.system)},
            {"path", JsonString(entry.path)},
            {"cover", JsonString(entry.cover)},
            {"size", JsonNumber(static_cast<double>(entry.size_bytes))},
            {"original_name", JsonString(entry.original_name)},
            {"archive_member", JsonString(entry.archive_member)},
            {"disc", JsonNumber(entry.disc)},
            {"placeholder_cover", JsonBool(entry.placeholder_cover)},
            {"hack_of", JsonString(entry.hack_of)},
            {"other_versions", std::move(versions)},
        }));
    }
    return store::SaveDocument(file, CACHE_VERSION, JsonObject({{"entries", std::move(list)}}));
}

std::optional<std::vector<std::string>> ReadOldGameFolders(const std::filesystem::path& profile) {
    const auto text = ReadTextFile(profile / "config" / "qt-config.ini");
    if (!text) {
        return std::nullopt;
    }
    // Paths\gamedirs\<n>\path=<folder>, among the keys of the [UI] section.
    constexpr std::string_view PREFIX = "Paths\\gamedirs\\";
    constexpr std::string_view SUFFIX = "\\path";
    std::vector<std::pair<int, std::string>> found;
    std::size_t at = 0;
    while (at < text->size()) {
        const std::size_t end = std::min(text->find('\n', at), text->size());
        const std::string line = Trim(std::string_view(*text).substr(at, end - at));
        at = end + 1;
        const std::size_t equals = line.find('=');
        if (!line.starts_with(PREFIX) || equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(PREFIX.size(), equals - PREFIX.size());
        if (!key.ends_with(SUFFIX)) {
            continue;
        }
        const int number = std::atoi(key.c_str());
        std::string value = Trim(std::string_view(line).substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        // The file doubles backslashes.
        std::string folder;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '\\') {
                ++i;
            }
            folder.push_back(value[i]);
        }
        // The emulated storage entries are not folders of the host.
        if (folder.empty() || folder == "SDMC" || folder == "UserNAND" || folder == "SysNAND") {
            continue;
        }
        found.emplace_back(number, std::move(folder));
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> folders;
    for (auto& [number, folder] : found) {
        folders.push_back(std::move(folder));
    }
    return folders;
}

} // namespace den::library
