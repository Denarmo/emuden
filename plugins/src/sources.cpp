// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/plugins/signature.h"
#include "den/plugins/sources.h"

namespace den::plugins {

namespace {

constexpr const char* HOW = "a source is owner/repository, or an https address";

bool IsHost(std::string_view host) {
    if (host.size() < 3 || host.size() > 120 || host.find('.') == std::string_view::npos ||
        host.front() == '.' || host.back() == '.' || host.find("..") != std::string_view::npos) {
        return false;
    }
    return std::all_of(host.begin(), host.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
    });
}

bool IsUrlPath(std::string_view path) {
    if (path.find("/../") != std::string_view::npos || path.find("/./") != std::string_view::npos) {
        return false;
    }
    return std::all_of(path.begin(), path.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '_' || c == '.' || c == '/' || c == '~';
    });
}

Result<SourceAddress> ParseUrlSource(std::string_view text) {
    std::string rest(text.substr(8)); // after https://
    const std::size_t cut = rest.find_first_of("?#");
    if (cut != std::string::npos) {
        rest.erase(cut);
    }
    const std::size_t slash = rest.find('/');
    const std::string host = ToLower(rest.substr(0, slash));
    std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (path.ends_with("/index.json")) {
        path.resize(path.size() - 10);
    }
    if (!path.ends_with("/")) {
        path += "/";
    }
    if (!IsHost(host) || !IsUrlPath(path)) {
        return Status::Fail(HOW);
    }
    SourceAddress address;
    address.text = "https://" + host + path;
    address.label = host + (path == "/" ? std::string() : path.substr(0, path.size() - 1));
    address.ns = host;
    address.index_url = address.text + "index.json";
    address.key_url = address.text + "keys/source.pub";
    return address;
}

// "owner/repo", "owner/repo@ref" or "owner/repo/tree/ref" (the rest of a github.com link).
Result<SourceAddress> ParseGithubPath(std::string path) {
    while (path.ends_with("/")) {
        path.pop_back();
    }
    std::string ref;
    if (const std::size_t at = path.find('@'); at != std::string::npos) {
        ref = path.substr(at + 1);
        path.erase(at);
    }
    std::vector<std::string> parts;
    for (std::size_t start = 0; start <= path.size();) {
        const std::size_t slash = std::min(path.find('/', start), path.size());
        parts.push_back(path.substr(start, slash - start));
        start = slash + 1;
    }
    if (parts.size() == 4 && parts[2] == "tree" && ref.empty()) {
        ref = parts[3];
        parts.resize(2);
    }
    if (parts.size() != 2) {
        return Status::Fail(HOW);
    }
    if (parts[1].ends_with(".git")) {
        parts[1].resize(parts[1].size() - 4);
    }
    return ParseSourceAddress("github:" + parts[0] + "/" + parts[1] + "@" + (ref.empty() ? "main" : ref));
}

} // namespace

Result<SourceAddress> ParseSourceAddress(std::string_view text) {
    const std::string trimmed = Trim(text);
    if (trimmed.starts_with("https://")) {
        return ParseUrlSource(trimmed);
    }
    Result<SourceLocation> location = ParseSourceUrl(trimmed);
    if (!location) {
        return Status::Fail(HOW);
    }
    SourceAddress address;
    address.text = "github:" + location->owner + "/" + location->repo + "@" + location->ref;
    address.label = location->owner + "/" + location->repo + (location->ref == "main" ? "" : "@" + location->ref);
    address.ns = ToLower(location->owner);
    address.index_url = location->index_url;
    address.key_url = address.index_url.substr(0, address.index_url.size() - 10) + "keys/source.pub";
    return address;
}

Result<SourceRequest> ParseSourceRequest(std::string_view text) {
    std::string rest = Trim(text);
    SourceRequest request;
    if (const std::size_t hash = rest.rfind('#'); hash != std::string::npos) {
        request.fingerprint = CompactFingerprint(rest.substr(hash + 1));
        if (request.fingerprint.empty()) {
            return Status::Fail("a key fingerprint is 16 hex digits after the #");
        }
        rest = Trim(rest.substr(0, hash));
    }
    const std::string lower = ToLower(rest);
    if (lower.starts_with("http://")) {
        return Status::Fail("a source address must start with https://");
    }
    Result<SourceAddress> address = Status::Fail(HOW);
    for (const std::string_view prefix : {"https://github.com/", "https://www.github.com/", "github.com/"}) {
        if (lower.starts_with(prefix)) {
            address = ParseGithubPath(rest.substr(prefix.size()));
            break;
        }
    }
    if (!address && lower.starts_with("https://") && !lower.starts_with("https://github.com/") &&
        !lower.starts_with("https://www.github.com/")) {
        address = ParseUrlSource(rest);
    } else if (!address && lower.starts_with("github:")) {
        address = ParseGithubPath(rest.substr(7));
    } else if (!address && rest.find(':') == std::string::npos) {
        address = ParseGithubPath(rest);
    }
    if (!address) {
        return address.status();
    }
    request.address = address.take();
    return request;
}

std::string KeyFingerprint(std::string_view key_b64) {
    const auto key = DecodeBase64(key_b64);
    if (!key || key->size() != 32) {
        return {};
    }
    const std::string digest = Sha256Hex(key->data(), key->size()).substr(0, 16);
    return digest.substr(0, 4) + " " + digest.substr(4, 4) + " " + digest.substr(8, 4) + " " +
           digest.substr(12, 4);
}

std::string CompactFingerprint(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == ' ' || c == ':' || c == '-') {
            continue;
        }
        const char lower = c >= 'A' && c <= 'F' ? static_cast<char>(c - 'A' + 'a') : c;
        if (!((lower >= '0' && lower <= '9') || (lower >= 'a' && lower <= 'f'))) {
            return {};
        }
        out += lower;
    }
    return out.size() == 16 ? out : std::string();
}

std::string FormatSourceEntry(const SourceEntry& entry) {
    std::string keys;
    for (const std::string& key : entry.keys) {
        keys += (keys.empty() ? "" : ",") + key;
    }
    return entry.address + " " + keys + (entry.enabled ? " on" : " off");
}

std::optional<SourceEntry> ParseSourceEntry(std::string_view line) {
    std::vector<std::string> words;
    for (std::size_t start = 0; start < line.size();) {
        const std::size_t space = std::min(line.find(' ', start), line.size());
        if (space > start) {
            words.emplace_back(line.substr(start, space - start));
        }
        start = space + 1;
    }
    if (words.size() != 3 || (words[2] != "on" && words[2] != "off")) {
        return std::nullopt;
    }
    Result<SourceAddress> address = ParseSourceAddress(words[0]);
    if (!address || address->text != words[0]) {
        return std::nullopt;
    }
    SourceEntry entry;
    entry.address = words[0];
    entry.enabled = words[2] == "on";
    for (std::size_t start = 0; start <= words[1].size();) {
        const std::size_t comma = std::min(words[1].find(',', start), words[1].size());
        const std::string key = words[1].substr(start, comma - start);
        if (KeyFingerprint(key).empty()) {
            return std::nullopt;
        }
        entry.keys.push_back(key);
        start = comma + 1;
    }
    return entry;
}

Status CheckSourceNamespace(std::string_view plugin_id, std::string_view ns) {
    if (plugin_id.starts_with("emuden.")) {
        return Status::Fail(std::string(plugin_id) + ": only EmuDEN publishes emuden. plugins");
    }
    if (ns.empty() || plugin_id.size() <= ns.size() + 1 || !plugin_id.starts_with(ns) ||
        plugin_id[ns.size()] != '.') {
        return Status::Fail(std::string(plugin_id) + ": this source publishes only " + std::string(ns) +
                            ". plugins");
    }
    return {};
}

Status CheckThirdPartyManifest(const Manifest& manifest, std::string_view ns) {
    if (Status status = CheckSourceNamespace(manifest.id, ns); !status.ok) {
        return status;
    }
    if (manifest.engine != "libretro") {
        return Status::Fail(manifest.id + ": the " + manifest.engine + " engine is only for EmuDEN's own plugins");
    }
    // An engine app is started outside EmuDEN: an added source offers only its own, named
    // "<ns>.", "io.github.<ns>." or the reversed host ("org.example." for example.org).
    std::string reversed;
    for (std::size_t end = ns.size(); end != std::string_view::npos;) {
        const std::size_t dot = ns.rfind('.', end == 0 ? 0 : end - 1);
        const std::size_t start = dot == std::string_view::npos ? 0 : dot + 1;
        reversed += (reversed.empty() ? "" : ".") + std::string(ns.substr(start, end - start));
        end = dot == std::string_view::npos ? std::string_view::npos : dot;
    }
    const auto own = [&](const std::string& name) {
        for (const std::string& prefix : {std::string(ns), "io.github." + std::string(ns), reversed}) {
            if (!prefix.empty() && name.size() > prefix.size() + 1 && name.starts_with(prefix) &&
                name[prefix.size()] == '.') {
                return true;
            }
        }
        return false;
    };
    for (const Component& component : manifest.components) {
        if (component.type != TYPE_ENGINE_APP) {
            continue;
        }
        const EngineAppSpec& spec = component.engine_app;
        if (!own(spec.ecp_id) || (!spec.package.empty() && !own(spec.package))) {
            return Status::Fail(manifest.id + ": the engine app " + component.id + " is not in this source's " +
                                std::string(ns) + " namespace");
        }
    }
    for (const SystemDef& system : manifest.systems) {
        if (!system.console.scan.scanner.empty()) {
            return Status::Fail(manifest.id + ": only EmuDEN's own plugins scan with an engine");
        }
        for (const BiosSet& set : system.console.bios) {
            for (const BiosFile& file : set.files) {
                if (file.path.starts_with("@")) {
                    return Status::Fail(manifest.id + ": only an engine answers " + file.path);
                }
            }
        }
    }
    return {};
}

const Offer* Catalogue::Find(std::string_view plugin_id) const {
    const auto it = std::find_if(offers.begin(), offers.end(),
                                 [&](const Offer& offer) { return offer.plugin->id == plugin_id; });
    return it == offers.end() ? nullptr : &*it;
}

Catalogue MergeCatalogue(std::span<const SourceView> views) {
    Catalogue catalogue;
    for (std::size_t i = 0; i < views.size(); ++i) {
        const SourceView& view = views[i];
        if (!view.Offers()) {
            continue;
        }
        for (const IndexPlugin& plugin : view.index->plugins) {
            if (!view.official) {
                if (Status status = CheckSourceNamespace(plugin.id, view.ns); !status.ok) {
                    catalogue.notes.push_back({i, status.error});
                    continue;
                }
                if (plugin.engine == "switch2" || plugin.engine == "pcstore") {
                    catalogue.notes.push_back(
                        {i, plugin.id + ": the " + plugin.engine + " engine is only for EmuDEN's own plugins"});
                    continue;
                }
            }
            if (RevokedFor(views, i, plugin.id, plugin.version)) {
                continue;
            }
            if (const Offer* first = catalogue.Find(plugin.id)) {
                if (first->source != i) {
                    catalogue.notes.push_back(
                        {i, plugin.id + " is also offered by " + views[first->source].label + ", which is used"});
                }
                continue;
            }
            catalogue.offers.push_back({&plugin, i});
        }
    }
    return catalogue;
}

std::optional<std::string> RevokedFor(std::span<const SourceView> views, std::size_t origin,
                                      std::string_view plugin_id, const Version& version) {
    if (origin < views.size() && !views[origin].revoked.empty()) {
        return "EmuDEN withdrew the key of " + views[origin].label + ": " + views[origin].revoked;
    }
    for (std::size_t i = 0; i < views.size(); ++i) {
        if ((views[i].official || i == origin) && views[i].index) {
            if (auto reason = views[i].index->RevokedReason(plugin_id, version)) {
                return reason;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> BlockedFor(std::span<const SourceView> views, std::size_t origin,
                                      std::string_view sha256) {
    for (std::size_t i = 0; i < views.size(); ++i) {
        if ((views[i].official || i == origin) && views[i].index) {
            if (auto reason = views[i].index->BlockedReason(sha256)) {
                return reason;
            }
        }
    }
    return std::nullopt;
}

std::size_t FindView(std::span<const SourceView> views, std::string_view address) {
    for (std::size_t i = 0; i < views.size(); ++i) {
        if (views[i].address == address) {
            return i;
        }
    }
    return views.size();
}

} // namespace den::plugins
