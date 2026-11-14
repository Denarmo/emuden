// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sources besides the official one: the list in the app settings, each source's cached
// index and state under plugins/sources/<folder>/, adding one with its key pinned, removing
// one with what it installed.

#include <format>

#include "den/base/hash.h"
#include "real_host.h"

namespace den::plugins {

namespace {

constexpr std::size_t MAX_KEY_FILE = 4096;

// A folder name for a source: readable, and unique through a piece of its address's hash.
std::string FolderOf(const std::string& address) {
    std::string name;
    for (const char c : ToLower(address.substr(address.find(':') + 1))) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (plain || !name.ends_with("-")) {
            name += plain ? c : '-';
        }
    }
    while (name.starts_with("-")) {
        name.erase(0, 1);
    }
    return name.substr(0, 48) + "-" + Sha256Hex(address.data(), address.size()).substr(0, 8);
}

} // namespace

void RealHost::LoadSources() {
    Result<SourceAddress> official = ParseSourceAddress(deps_.source);
    SourceView view;
    SourceState state;
    if (official) {
        view.address = official->text;
        view.index_url = official->index_url;
        state.key_url = official->key_url;
    } else {
        view.address = deps_.source;
        log::Warn("plugins", "the official source {}: {}", deps_.source, official.error());
    }
    view.label = "EmuDEN";
    view.official = true;
    state.keys = deps_.keys;
    state.dir = plugins_dir_ / "cache";
    views_.push_back(std::move(view));
    states_.push_back(std::move(state));
    if (config_.settings == nullptr) {
        return;
    }
    for (const std::string& line : config_.settings->GetList(SOURCES_KEY)) {
        const std::optional<SourceEntry> entry = ParseSourceEntry(line);
        if (!entry || FindView(views_, entry->address) != views_.size()) {
            log::Warn("plugins", "a plugin source in the settings is not used: {}", line);
            continue;
        }
        const SourceAddress address = ParseSourceAddress(entry->address).take();
        SourceView added;
        added.address = address.text;
        added.label = address.label;
        added.ns = address.ns;
        added.enabled = entry->enabled;
        added.index_url = address.index_url;
        SourceState added_state;
        added_state.keys = entry->keys;
        added_state.key_url = address.key_url;
        added_state.dir = plugins_dir_ / "sources" / PathFromUtf8(FolderOf(address.text));
        const auto text = ReadTextFile(added_state.dir / "state.json");
        if (const auto root = text ? ParseJson(*text) : std::nullopt) {
            added_state.last_sequence = static_cast<std::int64_t>(json::Number(*root, "last_sequence", 0));
            added_state.fetched = static_cast<std::int64_t>(json::Number(*root, "fetched", 0));
            added_state.error = root->String("error");
        }
        views_.push_back(std::move(added));
        states_.push_back(std::move(added_state));
    }
}

void RealHost::SaveSources() const {
    if (config_.settings == nullptr) {
        return;
    }
    std::vector<std::string> lines;
    for (std::size_t i = 1; i < views_.size(); ++i) {
        lines.push_back(FormatSourceEntry({views_[i].address, states_[i].keys, views_[i].enabled}));
    }
    config_.settings->SetList(SOURCES_KEY, lines);
}

// What a removed source left behind because it was in use: its plugin folders and cores.
void RealHost::RemoveOrphans() {
    std::error_code ec;
    for (const fs::path& dir : ListDirectory(plugins_dir_)) {
        const std::string origin = ReadOrigin(dir);
        if (!origin.empty() && FindView(views_, origin) == views_.size()) {
            log::Info("plugins", "{}: its source {} was removed", PathToUtf8(dir.filename()), origin);
            fs::remove(config_.data_dir / "plugin-data" / dir.filename() / "grant.json", ec);
            fs::remove_all(dir, ec);
        }
    }
    std::vector<std::string> cores;
    for (const auto& [key, record] : ledger_.Records()) {
        if (record.origin != "official" && record.origin != "bundled" && !record.origin.empty() &&
            FindView(views_, record.origin) == views_.size()) {
            cores.push_back(key);
        }
    }
    for (const std::string& key : cores) {
        ledger_.Remove(key);
    }
    if (!cores.empty()) {
        ledger_.Save();
    }
}

// The cached index is checked like a downloaded one before it is believed.
void RealHost::LoadCachedIndex(std::size_t source) {
    const SourceState& state = states_[source];
    const auto bytes = ReadTextFile(state.dir / "index.json");
    const auto signature = ReadTextFile(state.dir / "index.json.sig");
    if (!bytes || !signature) {
        return;
    }
    Index index;
    const IndexVerdict verdict = VerifyIndex(*bytes, *signature, state.keys, state.last_sequence, deps_.now(), index);
    if (verdict.Usable()) {
        views_[source].index = std::move(index);
        views_[source].expired = verdict.check == IndexCheck::Expired;
    }
}

void RealHost::SaveSourceState(std::size_t source) const {
    if (source == 0) {
        SaveState();
        return;
    }
    const SourceState& state = states_[source];
    std::error_code ec;
    fs::create_directories(state.dir, ec);
    WriteFileAtomic(state.dir / "state.json",
                    WriteJson(JsonObject({
                        {"address", JsonString(views_[source].address)},
                        {"last_sequence", JsonNumber(static_cast<double>(state.last_sequence))},
                        {"fetched", JsonNumber(static_cast<double>(state.fetched))},
                        {"error", JsonString(state.error)},
                    })));
}

// The official index may withdraw an added source's key; nothing of that source is used then.
void RealHost::UpdateRevokedKeys() {
    const std::optional<Index>& official = views_[0].index;
    for (std::size_t i = 1; i < views_.size(); ++i) {
        views_[i].revoked.clear();
        for (const std::string& key : states_[i].keys) {
            if (const auto reason = official ? official->RevokedKeyReason(KeyFingerprint(key)) : std::nullopt) {
                views_[i].revoked = *reason;
            }
        }
    }
    checked_.clear();
}

std::vector<SourceInfo> RealHost::Sources() const {
    std::lock_guard lock(mutex_);
    const Catalogue catalogue = MergeCatalogue(views_);
    std::vector<SourceInfo> out;
    for (std::size_t i = 0; i < views_.size(); ++i) {
        const SourceView& view = views_[i];
        SourceInfo info;
        info.address = view.address;
        info.label = view.label;
        info.fingerprint = states_[i].keys.empty() ? std::string() : KeyFingerprint(states_[i].keys.front());
        info.official = view.official;
        info.enabled = view.enabled;
        info.checked = states_[i].fetched;
        info.error = view.revoked.empty() ? states_[i].error : "EmuDEN withdrew this source's key: " + view.revoked;
        for (const Offer& offer : catalogue.offers) {
            info.offered += offer.source == i ? 1 : 0;
        }
        for (const SourceNote& note : catalogue.notes) {
            if (note.source == i) {
                info.notes.push_back(note.text);
            }
        }
        info.notes.insert(info.notes.end(), states_[i].notes.begin(), states_[i].notes.end());
        for (const Loaded& loaded : manifests_) {
            if (i == 0 || loaded.origin != view.address) {
                continue;
            }
            // A console the official source or an earlier source has is theirs.
            for (const SystemDef& system : loaded.manifest.systems) {
                const Found found = Find(system.console.id);
                if (found.loaded != nullptr && found.loaded != &loaded) {
                    const std::size_t owner = OriginOf(*found.loaded);
                    info.notes.push_back(std::format("{} ({}) comes from {}; this source's is not used",
                                                     system.console.name, loaded.manifest.id, views_[owner].label));
                }
            }
        }
        if (i > 0) {
            for (const fs::path& dir : ListDirectory(plugins_dir_)) {
                info.installed += ReadOrigin(dir) == view.address ? 1 : 0;
            }
        }
        out.push_back(std::move(info));
    }
    return out;
}

JobPtr RealHost::ProbeSource(const std::string& text, std::shared_ptr<SourceProbe> out) {
    return config_.jobs->Run("Check a plugin source", [this, text, out](Job& job) -> Status {
        Result<SourceRequest> request = ParseSourceRequest(text);
        if (!request) {
            return Status::Fail("That is not a plugin source: " + request.error() + ".");
        }
        const SourceAddress& address = request->address;
        std::optional<Index> official;
        {
            std::lock_guard lock(mutex_);
            const std::size_t i = FindView(views_, address.text);
            if (i == 0) {
                return Status::Fail("That is EmuDEN's own source. It is always there.");
            }
            if (i < views_.size()) {
                return Status::Fail(address.label + " was added already.");
            }
            official = views_[0].index;
        }
        const auto get = [&](const std::string& url, std::size_t max) {
            pal::HttpRequest http;
            http.url = url;
            http.max_body = max;
            http.progress = [&job](std::uint64_t, std::uint64_t) { return !job.CancelRequested(); };
            return deps_.http(http);
        };
        job.SetMessage("Getting the source's key");
        const pal::HttpResponse key = get(address.key_url, MAX_KEY_FILE);
        if (!key.ok()) {
            return Status::Fail(key.status == 404 ? address.label + " publishes no key (keys/source.pub)."
                                                  : address.label + " could not be reached" +
                                                        (key.error.empty() ? std::string(".") : ": " + key.error));
        }
        SourceProbe probe;
        probe.address = address.text;
        probe.label = address.label;
        probe.ns = address.ns;
        probe.key = Trim(key.body);
        probe.fingerprint = KeyFingerprint(probe.key);
        if (probe.fingerprint.empty()) {
            return Status::Fail("The key of " + address.label + " is not an Ed25519 public key.");
        }
        if (!request->fingerprint.empty()) {
            if (CompactFingerprint(probe.fingerprint) != request->fingerprint) {
                return Status::Fail("The key of " + address.label + " is " + probe.fingerprint + ", not the " +
                                    request->fingerprint + " you gave. The source was not added.");
            }
            probe.pinned = true;
        }
        if (const auto reason = official ? official->RevokedKeyReason(probe.fingerprint) : std::nullopt) {
            return Status::Fail("EmuDEN withdrew the key of " + address.label + ": " + *reason);
        }
        job.SetMessage("Getting the source's plugin list");
        const pal::HttpResponse list = get(address.index_url, MAX_INDEX_SIZE);
        const pal::HttpResponse signature = list.ok() ? get(address.index_url + ".sig", MAX_KEY_FILE) : list;
        if (!list.ok() || !signature.ok()) {
            return Status::Fail(address.label + " has no plugin list (index.json and index.json.sig).");
        }
        Index index;
        const std::vector<std::string> keys = {probe.key};
        const IndexVerdict verdict = VerifyIndex(list.body, signature.body, keys, 0, deps_.now(), index);
        if (!verdict.Usable()) {
            return Status::Fail(address.label + ": " + verdict.message);
        }
        probe.name = index.source_name;
        probe.plugins = index.plugins.size();
        *out = std::move(probe);
        return {};
    });
}

Status RealHost::AddSource(const SourceProbe& probe) {
    Result<SourceAddress> address = ParseSourceAddress(probe.address);
    if (!address || address->text != probe.address || KeyFingerprint(probe.key).empty()) {
        return Status::Fail("That is not a plugin source.");
    }
    std::lock_guard lock(mutex_);
    const std::size_t i = FindView(views_, address->text);
    if (i == 0) {
        return Status::Fail("That is EmuDEN's own source. It is always there.");
    }
    if (i < views_.size()) {
        return Status::Fail(address->label + " was added already.");
    }
    if (const auto reason = views_[0].index ? views_[0].index->RevokedKeyReason(KeyFingerprint(probe.key))
                                            : std::nullopt) {
        return Status::Fail("EmuDEN withdrew the key of " + address->label + ": " + *reason);
    }
    SourceView view;
    view.address = address->text;
    view.label = address->label;
    view.ns = address->ns;
    view.index_url = address->index_url;
    SourceState state;
    state.keys = {probe.key};
    state.key_url = address->key_url;
    state.dir = plugins_dir_ / "sources" / PathFromUtf8(FolderOf(address->text));
    std::error_code ec;
    fs::remove_all(state.dir, ec); // a source added again starts afresh
    views_.push_back(std::move(view));
    states_.push_back(std::move(state));
    SaveSources();
    log::Info("plugins", "added the source {} (key {})", address->text, KeyFingerprint(probe.key));
    ++revision_;
    return {};
}

Status RealHost::SetSourceEnabled(const std::string& address, bool enabled) {
    std::lock_guard lock(mutex_);
    const std::size_t i = FindView(views_, address);
    if (i == 0) {
        return Status::Fail("EmuDEN's own source is always on.");
    }
    if (i == views_.size()) {
        return Status::Fail("There is no such source.");
    }
    views_[i].enabled = enabled;
    SaveSources();
    ++revision_;
    return {};
}

JobPtr RealHost::RemoveSource(const std::string& address) {
    return config_.jobs->Run("Remove a plugin source", [this, address](Job&) { return DoRemoveSource(address); });
}

Status RealHost::DoRemoveSource(const std::string& address) {
    std::lock_guard lock(mutex_);
    const std::size_t i = FindView(views_, address);
    if (i == 0) {
        return Status::Fail("EmuDEN's own source cannot be removed.");
    }
    if (i == views_.size()) {
        return Status::Fail("There is no such source.");
    }
    Status result;
    // Its cores first, while their keys can still be worked out.
    for (const Loaded& loaded : manifests_) {
        if (loaded.origin != address) {
            continue;
        }
        for (const Component& component : loaded.manifest.components) {
            const std::string key = LedgerKey(loaded, component);
            if (component.type != TYPE_CORE || ledger_.Find(key) == nullptr) {
                continue;
            }
            if (const Status removed = ledger_.Remove(key); !removed.ok) {
                result.notes.push_back(component.name + ": " + removed.error);
            }
            checked_.erase(key);
        }
        for (auto it = system_files_.begin(); it != system_files_.end();) {
            it = it->first.starts_with(loaded.manifest.id + "/") ? system_files_.erase(it) : std::next(it);
        }
    }
    std::erase_if(manifests_, [&](const Loaded& loaded) { return loaded.origin == address; });
    // Console and feature plugins alike: plugins/<id>/ with its source.json. Their data stays,
    // but not what was granted, so installing again asks again.
    std::error_code ec;
    for (const fs::path& dir : ListDirectory(plugins_dir_)) {
        if (ReadOrigin(dir) != address) {
            continue;
        }
        fs::remove(config_.data_dir / "plugin-data" / dir.filename() / "grant.json", ec);
        fs::remove_all(dir, ec);
        if (fs::exists(dir, ec)) {
            result.notes.push_back(PathToUtf8(dir.filename()) + " is in use; it is switched off and goes when EmuDEN starts again.");
        }
    }
    fs::remove_all(states_[i].dir, ec);
    log::Info("plugins", "removed the source {}", address);
    views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(i));
    states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(i));
    SaveSources();
    ledger_.Save();
    SaveState();
    ++revision_;
    return result;
}

} // namespace den::plugins
