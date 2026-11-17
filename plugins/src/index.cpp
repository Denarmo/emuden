// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/language.h"
#include "den/plugins/index.h"
#include "den/plugins/signature.h"
#include "den/plugins/sources.h"
#include "json_util.h"

namespace den::plugins {

namespace {

bool IsName(std::string_view text) {
    return !text.empty() && text.size() <= 100 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '_' || c == '.';
    });
}

} // namespace

std::optional<std::int64_t> ParseUtcTime(std::string_view text) {
    // YYYY-MM-DDTHH:MM:SSZ
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[19] != 'Z') {
        return std::nullopt;
    }
    const auto number = [&](std::size_t at, std::size_t count) -> int {
        int value = 0;
        for (std::size_t i = at; i < at + count; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return -1;
            }
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };
    const int year = number(0, 4), month = number(5, 2), day = number(8, 2);
    const int hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 60) {
        return std::nullopt;
    }
    // Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
    const int y = month <= 2 ? year - 1 : year;
    const int era = y / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

const IndexPlugin* Index::Find(std::string_view plugin_id) const {
    const auto it = std::find_if(plugins.begin(), plugins.end(),
                                 [&](const IndexPlugin& p) { return p.id == plugin_id; });
    return it == plugins.end() ? nullptr : &*it;
}

std::optional<std::string> Index::RevokedReason(std::string_view plugin_id,
                                                const Version& version) const {
    for (const Revocation& revocation : revoked) {
        if (revocation.id == plugin_id && VersionInRange(version, revocation.versions)) {
            return revocation.reason.empty() ? std::string("withdrawn by its publisher")
                                             : revocation.reason;
        }
    }
    return std::nullopt;
}

std::optional<std::string> Index::BlockedReason(std::string_view sha256) const {
    for (const BlockedHash& blocked : blocked_sha256) {
        if (blocked.sha256 == sha256) {
            return blocked.reason.empty() ? std::string("blocked by its source") : blocked.reason;
        }
    }
    return std::nullopt;
}

std::optional<std::string> Index::RevokedKeyReason(std::string_view fingerprint) const {
    const std::string wanted = CompactFingerprint(fingerprint);
    for (const RevokedKey& key : revoked_keys) {
        if (!wanted.empty() && CompactFingerprint(key.fingerprint) == wanted) {
            return key.reason.empty() ? std::string("withdrawn by EmuDEN") : key.reason;
        }
    }
    return std::nullopt;
}

Result<Index> ParseIndex(std::string_view text) {
    if (text.size() > MAX_INDEX_SIZE) {
        return Status::Fail("the plugin list is too large");
    }
    const std::optional<JsonValue> root = ParseJson(text);
    if (!root || root->kind != JsonValue::Kind::Object) {
        return Status::Fail("the plugin list is not JSON");
    }
    if (json::Number(*root, "index_version", 0) != 1) {
        return Status::Fail("the plugin list needs a newer EmuDEN");
    }
    Index index;
    if (const JsonValue* source = root->Find("source")) {
        index.source_name = source->String("name");
    }
    index.sequence = static_cast<std::int64_t>(json::Number(*root, "sequence", -1));
    if (index.sequence < 0) {
        return Status::Fail("the plugin list has no sequence number");
    }
    for (const auto& [key, target] :
         {std::pair{"generated", &index.generated}, std::pair{"expires", &index.expires}}) {
        const std::string date = root->String(key);
        if (date.empty()) {
            continue;
        }
        const auto time = ParseUtcTime(date);
        if (!time) {
            return Status::Fail("the plugin list has a bad date");
        }
        *target = *time;
    }
    if (const JsonValue* plugins = root->Find("plugins")) {
        for (const JsonValue& value : plugins->items) {
            IndexPlugin plugin;
            plugin.id = value.String("id");
            plugin.name = value.String("name");
            const auto version = Version::Parse(value.String("version"));
            const JsonValue* package = value.Find("package");
            if (plugin.id.empty() || !version || package == nullptr) {
                return Status::Fail("the plugin list has an incomplete entry");
            }
            plugin.version = *version;
            plugin.package.url = package->String("url");
            plugin.package.sha256 = ToLower(package->String("sha256"));
            plugin.package.size = static_cast<std::uint64_t>(json::Number(*package, "size", 0));
            if (plugin.package.url.empty() || !IsSha256Hex(plugin.package.sha256) ||
                plugin.package.size == 0) {
                return Status::Fail("the plugin list has a package without a checksum");
            }
            plugin.kind = value.String("kind");
            plugin.engine = value.String("engine");
            plugin.platforms = json::Strings(value, "platforms", false);
            if (plugin.kind.empty()) {
                plugin.kind = "console";
            }
            if (plugin.kind == "feature" || plugin.kind == "interface") {
                plugin.publisher = value.String("publisher");
                plugin.summary = value.String("summary");
                plugin.permissions = json::Strings(value, "permissions", false);
                plugin.http_hosts = json::Strings(value, "http_hosts", false);
                plugin.open_hosts = json::Strings(value, "open_hosts", false);
                plugin.browse_hosts = json::Strings(value, "browse_hosts", false);
                plugin.needs = json::Strings(value, "needs", false);
                plugin.language = NormalizeLanguage(value.String("language"));
                plugin.language_name = value.String("language_name");
                // Not "systems": in an entry that field lists console objects, and readers
                // older than feature plugins expect it to.
                plugin.systems = json::Strings(value, "feature_systems", false);
            } else if (const JsonValue* systems = value.Find("systems")) {
                for (const JsonValue& system : systems->items) {
                    if (const std::string id = system.String("id"); !id.empty()) {
                        plugin.systems.push_back(id);
                    }
                }
            }
            index.plugins.push_back(std::move(plugin));
        }
    }
    if (const JsonValue* revoked = root->Find("revoked")) {
        for (const JsonValue& value : revoked->items) {
            index.revoked.push_back(
                {value.String("id"), value.String("versions"), value.String("reason")});
        }
    }
    if (const JsonValue* blocked = root->Find("blocked_sha256")) {
        for (const JsonValue& value : blocked->items) {
            if (value.kind == JsonValue::Kind::String) {
                index.blocked_sha256.push_back({ToLower(value.string), {}});
            } else {
                index.blocked_sha256.push_back(
                    {ToLower(value.String("sha256")), value.String("reason")});
            }
        }
    }
    if (const JsonValue* keys = root->Find("revoked_keys")) {
        for (const JsonValue& value : keys->items) {
            index.revoked_keys.push_back({value.String("fingerprint"), value.String("reason")});
        }
    }
    return index;
}

IndexVerdict VerifyIndex(std::string_view index_bytes, std::string_view signature_b64,
                         std::span<const std::string> keys, std::int64_t last_sequence,
                         std::int64_t now_unix, Index& out) {
    const std::span<const std::uint8_t> data(
        reinterpret_cast<const std::uint8_t*>(index_bytes.data()), index_bytes.size());
    const bool is_signed = std::any_of(keys.begin(), keys.end(), [&](const std::string& key) {
        return VerifyEd25519(data, signature_b64, key);
    });
    if (!is_signed) {
        return {IndexCheck::BadSignature, "The plugin list is not signed by its source."};
    }
    Result<Index> index = ParseIndex(index_bytes);
    if (!index) {
        return {IndexCheck::Malformed, index.error()};
    }
    if (index->sequence < last_sequence) {
        return {IndexCheck::Rollback, "The plugin list is older than the one already seen."};
    }
    if (index->expires == 0 || index->generated == 0 || index->expires < index->generated ||
        index->expires - index->generated > MAX_INDEX_LIFETIME_SECONDS) {
        return {IndexCheck::Malformed, "The plugin list has no valid expiry date."};
    }
    out = index.take();
    if (now_unix > out.expires) {
        return {IndexCheck::Expired, "The plugin list has expired."};
    }
    return {IndexCheck::Ok, {}};
}

Result<SourceLocation> ParseSourceUrl(std::string_view text) {
    std::string source = Trim(text);
    if (!source.starts_with("github:")) {
        return Status::Fail("a plugin source is written github:owner/repository");
    }
    source.erase(0, 7);
    SourceLocation location;
    location.ref = "main";
    if (const std::size_t at = source.find('@'); at != std::string::npos) {
        location.ref = source.substr(at + 1);
        source.erase(at);
    }
    const std::size_t slash = source.find('/');
    if (slash == std::string::npos) {
        return Status::Fail("a plugin source is written github:owner/repository");
    }
    location.owner = source.substr(0, slash);
    location.repo = source.substr(slash + 1);
    if (!IsName(location.owner) || !IsName(location.repo) || !IsName(location.ref)) {
        return Status::Fail("a plugin source is written github:owner/repository");
    }
    location.index_url = "https://raw.githubusercontent.com/" + location.owner + "/" +
                         location.repo + "/" + location.ref + "/index.json";
    return location;
}

std::string ResolveSourceUrl(std::string_view index_url, std::string_view reference) {
    if (reference.starts_with("https://")) {
        return std::string(reference);
    }
    const std::size_t slash = index_url.rfind('/');
    if (!IsSafeRelativePath(reference) || slash == std::string_view::npos) {
        return {};
    }
    return std::string(index_url.substr(0, slash + 1)) + std::string(reference);
}

} // namespace den::plugins
