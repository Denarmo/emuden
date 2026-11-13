// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// index.json: what a plugin source offers. A remote index is only ever used after its
// signature, its sequence number and its dates were checked.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "den/plugins/manifest.h"

namespace den::plugins {

inline constexpr std::int64_t MAX_INDEX_LIFETIME_SECONDS = 120LL * 24 * 3600;
inline constexpr std::size_t MAX_INDEX_SIZE = 8u << 20;

struct FileRef {
    std::string url; // absolute https, or relative to the index
    std::string sha256;
    std::uint64_t size = 0;
};

struct IndexPlugin {
    std::string id;
    std::string name;
    Version version;
    FileRef package;
    std::string kind; // "console" (also when the entry names none) or "feature"
    /// Feature plugins: what the package will ask for, shown before any download.
    std::string publisher;
    std::string summary;
    std::vector<std::string> permissions;
    std::vector<std::string> http_hosts;
    std::vector<std::string> open_hosts;
    std::vector<std::string> systems;
};

struct Revocation {
    std::string id;
    std::string versions;
    std::string reason;
};

struct BlockedHash {
    std::string sha256;
    std::string reason;
};

struct Index {
    std::string source_name;
    std::int64_t sequence = 0;
    std::int64_t generated = 0; // Unix seconds; 0 when the index has no such date
    std::int64_t expires = 0;
    std::vector<IndexPlugin> plugins;
    std::vector<Revocation> revoked;
    std::vector<BlockedHash> blocked_sha256;

    const IndexPlugin* Find(std::string_view plugin_id) const;
    std::optional<std::string> RevokedReason(std::string_view plugin_id,
                                             const Version& version) const;
    std::optional<std::string> BlockedReason(std::string_view sha256) const;
};

Result<Index> ParseIndex(std::string_view json);

enum class IndexCheck {
    Ok,
    /// Good signature and sequence, but past its `expires` date: installed plugins keep
    /// working and revocations apply; nothing new is installed from the source.
    Expired,
    BadSignature,
    Rollback, // the sequence is lower than the last one accepted
    Malformed,
};

struct IndexVerdict {
    IndexCheck check = IndexCheck::Malformed;
    std::string message;

    bool Usable() const {
        return check == IndexCheck::Ok || check == IndexCheck::Expired;
    }
};

/// Checks a downloaded index: the detached signature against any of `keys` first, then that
/// it is well formed, that `sequence` is not below `last_sequence`, that it expires at most
/// 120 days after it was generated, and whether it has expired. `out` is set when usable.
IndexVerdict VerifyIndex(std::string_view index_bytes, std::string_view signature_b64,
                         std::span<const std::string> keys, std::int64_t last_sequence,
                         std::int64_t now_unix, Index& out);

/// "2026-11-12T19:34:28Z" as Unix seconds.
std::optional<std::int64_t> ParseUtcTime(std::string_view text);

/// Where a source's index is. Accepts "github:owner/repo[@ref]".
struct SourceLocation {
    std::string owner;
    std::string repo;
    std::string ref;
    std::string index_url; // index.json; index.json.sig sits beside it
};
Result<SourceLocation> ParseSourceUrl(std::string_view text);

/// The URL of a file a source names: absolute https as it is, a relative path beside the
/// index. Empty for anything else.
std::string ResolveSourceUrl(std::string_view index_url, std::string_view reference);

} // namespace den::plugins
