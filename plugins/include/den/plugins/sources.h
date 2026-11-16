// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin sources besides the official one: their addresses, pinned keys, the rules a
// third-party source's plugins follow, and how the catalogues of all sources are merged.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"
#include "den/plugins/index.h"
#include "den/plugins/manifest.h"

namespace den::plugins {

/// The app setting with the added sources, a list of FormatSourceEntry lines.
inline constexpr const char* SOURCES_KEY = "plugins.sources";

/// Where a source is. Either "github:owner/repo@ref" (files of the repository on
/// raw.githubusercontent.com) or an https base URL that serves index.json, index.json.sig
/// and keys/source.pub.
struct SourceAddress {
    std::string text;  // canonical, the source's identity: "github:jdoe/consoles@main", "https://x.org/emu/"
    std::string label; // shown as its name: "jdoe/consoles", "jdoe/consoles@dev", "x.org/emu"
    std::string ns;    // its plugins' ids start with ns + ".": the owner, or the host, lower case
    std::string index_url;
    std::string key_url;
};

/// A canonical address as stored ("github:owner/repo@ref" or "https://...").
Result<SourceAddress> ParseSourceAddress(std::string_view text);

/// What a user types to add a source: "owner/repo", "owner/repo@ref", "github:owner/repo",
/// a github.com link, or an https URL; "#<16 hex digits>" at the end pins the key.
struct SourceRequest {
    SourceAddress address;
    std::string fingerprint; // 16 lower-case hex digits, or empty
};
Result<SourceRequest> ParseSourceRequest(std::string_view text);

/// The first 16 hex digits of the SHA-256 of the raw public key, as "3fa9 12c4 77e0 b5d1".
/// Empty for anything that is not a 32-byte key in base64.
std::string KeyFingerprint(std::string_view key_b64);
/// "3fa9 12c4 77e0 b5d1" or "3FA912C477E0B5D1" as "3fa912c477e0b5d1"; empty when not 16 hex digits.
std::string CompactFingerprint(std::string_view text);

/// One added source: "<address> <key>[,<key>...] on|off".
struct SourceEntry {
    std::string address;
    std::vector<std::string> keys; // pinned Ed25519 public keys, base64
    bool enabled = true;
};
std::string FormatSourceEntry(const SourceEntry& entry);
std::optional<SourceEntry> ParseSourceEntry(std::string_view line);

/// A plugin id a third-party source may publish: it starts with "<ns>." and is not EmuDEN's.
Status CheckSourceNamespace(std::string_view plugin_id, std::string_view ns);
/// The rules for a console plugin from a third-party source: its namespace, the libretro
/// engine only (switch2 is EmuDEN's), no scanner of its own, no file that an engine answers,
/// and engine apps of its own namespace.
Status CheckThirdPartyManifest(const Manifest& manifest, std::string_view ns);
/// The same rules without the namespace, for a console plugin of the developer folder.
Status CheckDeveloperManifest(const Manifest& manifest);

/// One source as the hosts see it: the official one first, then the added ones in the order
/// they were added.
struct SourceView {
    std::string address;
    std::string label;
    std::string ns; // empty for the official source
    bool official = false;
    bool enabled = true;
    std::string revoked; // EmuDEN withdrew the source's key: why. Nothing of it is used.
    std::optional<Index> index;
    bool expired = false;
    std::string index_url;

    bool Offers() const {
        return enabled && revoked.empty() && index && !expired;
    }
};

/// The plugins every source offers, one per id. The first source to publish an id wins, the
/// official one first; a third-party entry outside its namespace, for an engine of EmuDEN's
/// own or withdrawn by its source is left out. Every entry left out gets a note on its source.
struct Offer {
    const IndexPlugin* plugin = nullptr;
    std::size_t source = 0; // into the views
};
struct SourceNote {
    std::size_t source = 0;
    std::string text;
};
struct Catalogue {
    std::vector<Offer> offers;
    std::vector<SourceNote> notes;

    const Offer* Find(std::string_view plugin_id) const;
};
Catalogue MergeCatalogue(std::span<const SourceView> views);

/// Why a plugin from `views[origin]` must not be used: the official source revoked the id
/// (it may for any source), its own source revoked it, or its source's key was withdrawn.
std::optional<std::string> RevokedFor(std::span<const SourceView> views, std::size_t origin,
                                      std::string_view plugin_id, const Version& version);
/// The same for a download's checksum.
std::optional<std::string> BlockedFor(std::span<const SourceView> views, std::size_t origin,
                                      std::string_view sha256);

/// The view of a source by address; views.size() when there is none.
std::size_t FindView(std::span<const SourceView> views, std::string_view address);

} // namespace den::plugins
