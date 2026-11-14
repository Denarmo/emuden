// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/library/systems.h"

namespace den::library {

extern const char* const BUILTIN_SYSTEMS_JSON[];

namespace {

std::vector<std::string> Strings(const JsonValue* list) {
    std::vector<std::string> out;
    if (list != nullptr && list->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : list->items) {
            if (item.kind == JsonValue::Kind::String && !item.string.empty()) {
                out.push_back(ToLower(item.string));
            }
        }
    }
    return out;
}

bool Flag(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Bool && value->boolean;
}

std::optional<SystemDef> ParseSystem(const JsonValue& json) {
    SystemDef def;
    def.id = ToLower(json.String("id"));
    def.name = json.String("name");
    if (def.id.empty() || def.name.empty()) {
        return std::nullopt;
    }
    def.short_name = json.String("short_name");
    if (const JsonValue* order = json.Find("order"); order != nullptr) {
        def.order = static_cast<int>(order->number);
    }
    if (const JsonValue* content = json.Find("content"); content != nullptr) {
        def.extensions = Strings(content->Find("extensions"));
        def.folder_aliases = Strings(content->Find("folder_aliases"));
        def.folder_only_extensions = Strings(content->Find("folder_only_extensions"));
        def.compound_extensions = Strings(content->Find("compound_extensions"));
        if (const std::string zip = content->String("zip"); !zip.empty()) {
            def.zip = zip;
        }
        def.scanner = content->String("scanner");
        def.cover_is_rom = Flag(*content, "cover_is_rom");
        if (const JsonValue* multi = content->Find("multi_file");
            multi != nullptr && multi->kind == JsonValue::Kind::Object) {
            MultiFile rules;
            rules.index = Strings(multi->Find("index"));
            rules.members = Strings(multi->Find("members"));
            rules.loose_members = Flag(*multi, "loose_members");
            rules.track_naming = multi->String("track_naming");
            def.multi_file = std::move(rules);
        }
        if (const JsonValue* fields = content->Find("header_title");
            fields != nullptr && fields->kind == JsonValue::Kind::Array) {
            for (const JsonValue& field : fields->items) {
                const JsonValue* offset = field.Find("offset");
                const JsonValue* length = field.Find("length");
                if (offset != nullptr && length != nullptr && length->number > 0 &&
                    length->number <= 256 && offset->number >= 0) {
                    def.header_title.push_back({static_cast<std::uint64_t>(offset->number),
                                                static_cast<std::size_t>(length->number)});
                }
            }
        }
    }
    if (const JsonValue* artwork = json.Find("artwork"); artwork != nullptr) {
        def.libretro_thumbnails = artwork->String("libretro_thumbnails");
        if (const JsonValue* urls = artwork->Find("cover_by_id"); urls != nullptr) {
            for (const JsonValue& url : urls->items) {
                if (url.kind == JsonValue::Kind::String && url.string.starts_with("https://") &&
                    url.string.find("{id}") != std::string::npos) {
                    def.cover_by_id.push_back(url.string);
                }
            }
        }
    }
    return def;
}

bool Contains(const std::vector<std::string>& list, std::string_view value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// "Game Boy Advance" and "game-boy_advance" are the same folder name.
std::string FolderKey(std::string_view name) {
    std::string out;
    for (const char c : ToLower(name)) {
        if (c != ' ' && c != '-' && c != '_' && c != '.') {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

std::vector<SystemDef> ParseSystems(std::string_view json) {
    std::vector<SystemDef> out;
    const auto root = ParseJson(json);
    if (!root) {
        return out;
    }
    const JsonValue* list = &*root;
    if (root->kind == JsonValue::Kind::Object) {
        if (const JsonValue* systems = root->Find("systems"); systems != nullptr) {
            list = systems;
        } else if (auto one = ParseSystem(*root)) {
            out.push_back(std::move(*one));
            return out;
        }
    }
    if (list->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : list->items) {
            if (auto def = ParseSystem(item)) {
                out.push_back(std::move(*def));
            }
        }
    }
    return out;
}

const std::vector<SystemDef>& BuiltinSystems() {
    static const std::vector<SystemDef> systems = [] {
        std::vector<SystemDef> out;
        for (const char* const* line = BUILTIN_SYSTEMS_JSON; *line != nullptr; ++line) {
            for (SystemDef& def : ParseSystems(*line)) {
                out.push_back(std::move(def));
            }
        }
        return out;
    }();
    return systems;
}

const std::vector<std::string>& BuiltinRomExtensions() {
    static const std::vector<std::string> extensions = [] {
        std::vector<std::string> out;
        for (const SystemDef& def : BuiltinSystems()) {
            for (const std::string& extension : def.extensions) {
                if (!Contains(out, extension)) {
                    out.push_back(extension);
                }
            }
        }
        return out;
    }();
    return extensions;
}

SystemTable::SystemTable() : systems_(BuiltinSystems()) {}

void SystemTable::Merge(std::vector<SystemDef> systems) {
    for (SystemDef& def : systems) {
        const auto found = std::find_if(systems_.begin(), systems_.end(),
                                        [&](const SystemDef& s) { return s.id == def.id; });
        if (found == systems_.end()) {
            systems_.push_back(std::move(def));
            continue;
        }
        if (def.extensions.empty()) {
            def.extensions = found->extensions;
            def.zip = found->zip;
        }
        if (def.folder_aliases.empty()) {
            def.folder_aliases = found->folder_aliases;
        }
        if (def.short_name.empty()) {
            def.short_name = found->short_name;
        }
        if (def.libretro_thumbnails.empty()) {
            def.libretro_thumbnails = found->libretro_thumbnails;
        }
        *found = std::move(def);
    }
    std::stable_sort(systems_.begin(), systems_.end(),
                     [](const SystemDef& a, const SystemDef& b) { return a.order < b.order; });
}

const SystemDef* SystemTable::Find(std::string_view id) const {
    for (const SystemDef& def : systems_) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

const SystemDef* SystemTable::FromFolderName(std::string_view folder) const {
    const std::string lower = ToLower(folder);
    const std::string key = FolderKey(folder);
    for (const SystemDef& def : systems_) {
        if (def.extensions.empty()) {
            continue;
        }
        if (def.id == lower || Contains(def.folder_aliases, lower) || FolderKey(def.name) == key ||
            (!def.short_name.empty() && FolderKey(def.short_name) == key)) {
            return &def;
        }
    }
    return nullptr;
}

const SystemDef* SystemTable::FromUniqueExtension(std::string_view extension) const {
    const SystemDef* found = nullptr;
    for (const SystemDef& def : systems_) {
        if (!Contains(def.extensions, extension)) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = &def;
    }
    // ".md" is far more often Markdown than a Mega Drive ROM.
    if (found != nullptr && Contains(found->folder_only_extensions, extension)) {
        return nullptr;
    }
    return found;
}

std::string SystemTable::RomExtension(const std::string& lower_name) const {
    for (const SystemDef& def : systems_) {
        for (const std::string& compound : def.compound_extensions) {
            if (lower_name.size() > compound.size() + 1 && lower_name.ends_with("." + compound)) {
                return compound;
            }
        }
    }
    const std::size_t dot = lower_name.rfind('.');
    return dot == std::string::npos ? std::string() : lower_name.substr(dot + 1);
}

} // namespace den::library
