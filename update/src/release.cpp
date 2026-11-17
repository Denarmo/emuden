// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/update/release.h"

#include <algorithm>
#include <cctype>

#include "den/base/files.h"
#include "den/update/version.h"

namespace den::update {

namespace {

bool IsHex64(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
               return std::isxdigit(static_cast<unsigned char>(c)) != 0;
           });
}

bool Flag(const JsonValue& json, std::string_view key) {
    const JsonValue* value = json.Find(key);
    return value && value->kind == JsonValue::Kind::Bool && value->boolean;
}

// The text with the Markdown marks inside a line taken out: **bold**, `code`, [text](link).
std::string Inline(std::string_view line) {
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '*' || c == '`' || (c == '_' && i + 1 < line.size() && line[i + 1] == '_')) {
            continue;
        }
        if (c == '_' && (i == 0 || line[i - 1] == ' ' || i + 1 == line.size() || line[i + 1] == ' ')) {
            continue; // _emphasis_, not a snake_case name
        }
        if (c == '[') {
            const std::size_t close = line.find("](", i);
            const std::size_t end = close == std::string_view::npos ? close : line.find(')', close);
            if (end != std::string_view::npos) {
                out += line.substr(i + 1, close - i - 1);
                i = end;
                continue;
            }
        }
        out += c;
    }
    return out;
}

} // namespace

const Asset* Release::Find(std::string_view asset_name) const {
    for (const Asset& asset : assets) {
        if (asset.name == asset_name) {
            return &asset;
        }
    }
    return nullptr;
}

std::string UpdateRepo() {
    const std::string repo = DEN_UPDATE_REPO;
    return IsRepoName(repo) ? repo : DEFAULT_REPO;
}

bool IsRepoName(std::string_view repo) {
    const std::size_t slash = repo.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 == repo.size() ||
        repo.find('/', slash + 1) != std::string_view::npos) {
        return false;
    }
    const auto part_ok = [](std::string_view part) {
        return part != "." && part != ".." && std::all_of(part.begin(), part.end(), [](char c) {
                   return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
               });
    };
    return part_ok(repo.substr(0, slash)) && part_ok(repo.substr(slash + 1));
}

std::string LatestUrl(std::string_view repo) {
    return "https://api.github.com/repos/" + std::string(repo) + "/releases/latest";
}

std::string ListUrl(std::string_view repo) {
    return "https://api.github.com/repos/" + std::string(repo) + "/releases?per_page=10";
}

std::string ReleasesPage(std::string_view repo) {
    return "https://github.com/" + std::string(repo) + "/releases";
}

std::optional<Release> ParseRelease(const JsonValue& json) {
    if (json.kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    Release release;
    release.tag = json.String("tag_name");
    if (!ParseVersion(release.tag)) {
        return std::nullopt;
    }
    release.version = release.tag[0] == 'v' || release.tag[0] == 'V' ? release.tag.substr(1) : release.tag;
    release.name = json.String("name");
    release.notes = json.String("body");
    release.page = json.String("html_url");
    release.published = json.String("published_at");
    release.draft = Flag(json, "draft");
    release.prerelease = Flag(json, "prerelease");
    if (const JsonValue* assets = json.Find("assets"); assets && assets->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : assets->items) {
            Asset asset;
            asset.name = item.String("name");
            asset.url = item.String("browser_download_url");
            if (const JsonValue* size = item.Find("size"); size && size->kind == JsonValue::Kind::Number &&
                                                           size->number > 0) {
                asset.size = static_cast<std::uint64_t>(size->number);
            }
            const std::string digest = item.String("digest");
            if (digest.starts_with("sha256:") && IsHex64(std::string_view(digest).substr(7))) {
                asset.sha256 = ToLower(digest.substr(7));
            }
            if (!asset.name.empty() && !asset.url.empty()) {
                release.assets.push_back(std::move(asset));
            }
        }
    }
    return release;
}

std::optional<Release> PickRelease(std::string_view json_text, bool beta) {
    const auto json = ParseJson(json_text);
    if (!json) {
        return std::nullopt;
    }
    std::vector<const JsonValue*> candidates;
    if (json->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : json->items) {
            candidates.push_back(&item);
        }
    } else {
        candidates.push_back(&*json);
    }
    std::optional<Release> best;
    std::optional<Version> best_version;
    for (const JsonValue* item : candidates) {
        auto release = ParseRelease(*item);
        if (!release || release->draft || (release->prerelease && !beta)) {
            continue;
        }
        const auto version = ParseVersion(release->version);
        // A tag "-beta" not marked as a pre-release is still one.
        if (!version || (!version->pre.empty() && !beta)) {
            continue;
        }
        if (!best_version || CompareVersions(*version, *best_version) > 0) {
            best_version = version;
            best = std::move(release);
        }
    }
    return best;
}

std::string AssetNameFor(std::string_view platform_key) {
    if (platform_key == "windows-x86_64") {
        return "EmuDEN-windows-x64.zip";
    }
    if (platform_key == "linux-x86_64") {
        return "EmuDEN-x86_64.AppImage";
    }
    if (platform_key == "android-arm64") {
        return "EmuDEN-android-arm64.apk";
    }
    return {};
}

std::map<std::string, std::string, std::less<>> ParseSums(std::string_view text) {
    std::map<std::string, std::string, std::less<>> sums;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string line = Trim(text.substr(start, end - start));
        start = end + 1;
        const std::size_t space = line.find_first_of(" \t");
        if (space == std::string::npos || !IsHex64(std::string_view(line).substr(0, space))) {
            continue;
        }
        std::string name = Trim(std::string_view(line).substr(space));
        if (name.starts_with("*")) {
            name.erase(0, 1); // binary mode of sha256sum
        }
        if (!name.empty()) {
            sums[name] = ToLower(line.substr(0, space));
        }
    }
    return sums;
}

std::string PlainNotes(std::string_view markdown) {
    std::string out;
    std::size_t start = 0;
    bool blank = true; // the text so far ends with an empty line (or is empty)
    while (start <= markdown.size()) {
        std::size_t end = markdown.find('\n', start);
        if (end == std::string_view::npos) {
            end = markdown.size();
        }
        std::string_view line = markdown.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        std::string text = Trim(line);
        if (text.starts_with("```") || text == "---" || text == "***") {
            continue;
        }
        if (text.empty()) {
            if (!blank) {
                out += '\n';
                blank = true;
            }
            continue;
        }
        std::string prefix;
        if (text.starts_with("#")) {
            text = Trim(std::string_view(text).substr(text.find_first_not_of('#')));
        } else if (text.starts_with("- ") || text.starts_with("* ") || text.starts_with("+ ")) {
            prefix = "- ";
            text = Trim(std::string_view(text).substr(2));
        } else if (text.starts_with(">")) {
            text = Trim(std::string_view(text).substr(1));
        }
        out += prefix + Inline(text) + '\n';
        blank = false;
    }
    while (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    return out;
}

std::string DateText(std::string_view iso) {
    static const char* const MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                         "July",    "August",   "September", "October", "November", "December"};
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') {
        return std::string(iso);
    }
    const auto digits = [&](std::size_t from, std::size_t count) {
        int value = 0;
        for (std::size_t i = from; i < from + count; ++i) {
            if (iso[i] < '0' || iso[i] > '9') {
                return -1;
            }
            value = value * 10 + (iso[i] - '0');
        }
        return value;
    };
    const int year = digits(0, 4), month = digits(5, 2), day = digits(8, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31) {
        return std::string(iso);
    }
    return std::to_string(day) + " " + MONTHS[month - 1] + " " + std::to_string(year);
}

bool CheckDue(std::int64_t last_check, std::int64_t now, std::int64_t interval) {
    return last_check <= 0 || now < last_check || now - last_check >= interval;
}

} // namespace den::update
