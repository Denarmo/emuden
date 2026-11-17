// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/plugins/feature_manifest.h"
#include "real_host.h"

namespace den::plugins {

bool BiosPresent(const fs::path& dir, const BiosFile& file) {
    std::error_code ec;
    const fs::path path = dir / PathFromUtf8(file.path);
    if (file.path.ends_with("/")) {
        return fs::is_directory(path, ec) && !fs::is_empty(path, ec);
    }
    return fs::is_regular_file(path, ec);
}

std::vector<std::string> MissingBios(const fs::path& dir, const Console& console, std::string_view core_id) {
    std::vector<std::string> missing;
    for (const BiosSet& set : console.bios) {
        if (!set.cores.empty() && std::find(set.cores.begin(), set.cores.end(), core_id) == set.cores.end()) {
            continue; // another core's
        }
        bool any = false;
        std::vector<std::string> required;
        for (const BiosFile& file : set.files) {
            if (file.path.starts_with("@")) {
                continue; // answered by the engine
            }
            const bool present = BiosPresent(dir, file);
            any = any || present;
            if (!present && !file.optional) {
                required.push_back(file.path);
            }
        }
        if (set.any_one) {
            if (!any && !required.empty()) {
                missing.push_back(set.label.empty() ? required.front() : set.label);
            }
        } else {
            missing.insert(missing.end(), required.begin(), required.end());
        }
    }
    return missing;
}

std::string ReadOrigin(const fs::path& plugin_dir) {
    const auto text = ReadTextFile(plugin_dir / ORIGIN_FILE);
    const auto root = text ? ParseJson(*text) : std::nullopt;
    return root ? root->String("source") : std::string();
}

bool WriteOrigin(const fs::path& plugin_dir, const std::string& address) {
    std::error_code ec;
    fs::create_directories(plugin_dir, ec);
    return WriteFileAtomic(plugin_dir / ORIGIN_FILE, WriteJson(JsonObject({{"source", JsonString(address)}})));
}

RealHost::RealHost(const Config& config, HostDeps deps)
    : config_(config), deps_(std::move(deps)), plugins_dir_(config.data_dir / "plugins"),
      dev_dir_(config.data_dir / "plugins-dev"), local_dir_(config.data_dir / "plugins-local"), bios_dir_(config.data_dir / "bios"), ledger_(config.data_dir / "cores") {
    if (!deps_.http) {
        deps_.http = &pal::HttpGet;
    }
    if (deps_.source.empty()) {
        deps_.source = OFFICIAL_SOURCE;
    }
    if (deps_.keys.empty()) {
        deps_.keys = OfficialKeys();
    }
    if (deps_.platform_key.empty()) {
        deps_.platform_key = pal::GetCaps().platform_key;
    }
    if (!deps_.now) {
        deps_.now = [] {
            return static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
        };
    }
    std::error_code ec;
    fs::create_directories(plugins_dir_, ec);
    fs::create_directories(bios_dir_, ec);
    fs::create_directories(ledger_.Directory(), ec);
    LoadSources();
    RemoveOrphans();
    LoadState();
    for (std::size_t i = 0; i < views_.size(); ++i) {
        LoadCachedIndex(i);
    }
    UpdateRevokedKeys();
    LoadManifests();
    FindCarryOvers();
}

std::vector<Console> RealHost::Consoles() const {
    std::lock_guard lock(mutex_);
    std::vector<Console> out;
    for (const Loaded& loaded : manifests_) {
        if (!Usable(loaded)) {
            continue;
        }
        for (const SystemDef& system : loaded.manifest.systems) {
            // The first plugin of a console has it: the official source's before any other.
            if (std::none_of(out.begin(), out.end(), [&](const Console& c) { return c.id == system.console.id; })) {
                out.push_back(Describe(loaded, system));
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Console& a, const Console& b) { return a.order < b.order; });
    return out;
}

std::optional<Console> RealHost::FindConsole(const std::string& system) const {
    std::lock_guard lock(mutex_);
    const Found found = Find(system);
    return found.system ? std::optional<Console>(Describe(*found.loaded, *found.system)) : std::nullopt;
}

std::optional<CoreInfo> RealHost::CoreFor(const std::string& system) const {
    std::lock_guard lock(mutex_);
    const Found found = Find(system);
    const Component* core = found.system ? ChosenCore(*found.loaded, *found.system) : nullptr;
    return core ? std::optional<CoreInfo>(Describe(*found.loaded, *found.system, *core)) : std::nullopt;
}

std::vector<CoreInfo> RealHost::CoresFor(const std::string& system) const {
    std::lock_guard lock(mutex_);
    std::vector<CoreInfo> out;
    if (const Found found = Find(system); found.system != nullptr) {
        for (const Component* core : found.loaded->manifest.CoresFor(*found.system, deps_.platform_key)) {
            out.push_back(Describe(*found.loaded, *found.system, *core));
        }
    }
    return out;
}

void RealHost::SelectCore(const std::string& system, const std::string& core_id) {
    if (config_.settings != nullptr) {
        config_.settings->SetString("plugins.core." + system, core_id);
        ++revision_;
    }
}

std::vector<SettingSpec> RealHost::SettingsFor(const std::string& system) const {
    std::lock_guard lock(mutex_);
    std::vector<SettingSpec> out;
    if (const Found found = Find(system); found.system != nullptr) {
        const Component* core = ChosenCore(*found.loaded, *found.system);
        for (const ManifestSetting& setting : found.loaded->manifest.settings) {
            if (setting.Applies(system, deps_.platform_key, core != nullptr ? core->id : std::string())) {
                out.push_back(setting.spec);
            }
        }
    }
    return out;
}

bool RealHost::Installed(const std::string& system) const {
    std::lock_guard lock(mutex_);
    const Found found = Find(system);
    if (found.system == nullptr) {
        return false;
    }
    if (found.system->console.runner == Runner::Engine) {
        return true;
    }
    const Component* core = ChosenCore(*found.loaded, *found.system);
    return core != nullptr && CoreGood(*found.loaded, *core);
}

Status RealHost::VerifyCore(const fs::path& library) const {
    std::lock_guard lock(mutex_);
    const CoreRecord* record = ledger_.FindByFile(PathToUtf8(library.filename()));
    std::size_t origin = 0;
    if (record != nullptr && !record->origin.empty() && record->origin != "official" && record->origin != "bundled" &&
        record->origin != DEVELOPER_ORIGIN && record->origin != LOCAL_ORIGIN) {
        origin = FindView(views_, record->origin);
        if (origin == views_.size()) {
            return Status::Fail("This core's source was removed.");
        }
        if (!views_[origin].revoked.empty()) {
            return Status::Fail("This core must not be used: EmuDEN withdrew the key of " + views_[origin].label +
                                ": " + views_[origin].revoked);
        }
    }
    return ledger_.VerifyForLoad(library, Blocked(origin));
}

// A manifest of a source that was removed, or whose key EmuDEN withdrew, is not used, nor
// one whose dependencies are missing.
bool RealHost::Usable(const Loaded& loaded) const {
    const std::size_t origin = OriginOf(loaded);
    return origin < views_.size() && views_[origin].revoked.empty() && DependsMet(loaded.manifest);
}

bool RealHost::DependsMet(const Manifest& manifest) const {
    return std::all_of(manifest.depends.begin(), manifest.depends.end(), [&](const Dependency& dependency) {
        return std::any_of(manifests_.begin(), manifests_.end(), [&](const Loaded& other) {
            const std::size_t origin = OriginOf(other);
            return other.manifest.id == dependency.id && origin < views_.size() && views_[origin].revoked.empty() &&
                   VersionInRange(other.manifest.version, dependency.range);
        });
    });
}

RealHost::Found RealHost::Find(const std::string& system) const {
    for (const Loaded& loaded : manifests_) {
        if (!Usable(loaded)) {
            continue;
        }
        for (const SystemDef& def : loaded.manifest.systems) {
            if (def.console.id == system) {
                return {&loaded, &def};
            }
        }
    }
    return {};
}

const Component* RealHost::ChosenCore(const Loaded& loaded, const SystemDef& system) const {
    const auto cores = loaded.manifest.CoresFor(system, deps_.platform_key);
    if (cores.empty()) {
        return nullptr;
    }
    if (config_.settings != nullptr) {
        const std::string chosen = config_.settings->GetString("plugins.core." + system.console.id);
        for (const Component* core : cores) {
            if (core->id == chosen) {
                return core;
            }
        }
    }
    for (const Component* core : cores) {
        if (core->id == system.default_core) {
            return core;
        }
    }
    return cores.front();
}

std::string RealHost::LedgerKey(const Loaded& loaded, const Component& core) const {
    if (loaded.developer || loaded.local) {
        return std::string(loaded.local ? LOCAL_ORIGIN : DEVELOPER_ORIGIN) + "/" + loaded.manifest.id + "/" + core.id;
    }
    return OriginOf(loaded) == 0 ? core.id : loaded.manifest.id + "/" + core.id;
}

std::function<std::string(std::string_view)> RealHost::Blocked(std::size_t origin) const {
    return [this, origin](std::string_view sha256) {
        return BlockedFor(views_, origin, sha256).value_or(std::string());
    };
}

// The core is in the ledger and its file is the one that was installed. The file is
// hashed once and again whenever its size or time changes.
bool RealHost::CoreGood(const Loaded& loaded, const Component& core) const {
    if (core.type == TYPE_ENGINE_APP) {
        return EngineAppGood(loaded, core);
    }
    const std::string key = LedgerKey(loaded, core);
    const CoreRecord* record = ledger_.Find(key);
    if (record == nullptr) {
        return false;
    }
    const fs::path file = ledger_.Directory() / PathFromUtf8(record->file);
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec) {
        return false;
    }
    const fs::file_time_type time = fs::last_write_time(file, ec);
    Checked& checked = checked_[key];
    if (checked.size != size || checked.time != time) {
        checked = {size, time, ledger_.VerifyForLoad(file, Blocked(OriginOf(loaded))).ok};
    }
    return checked.good;
}

// The files were unpacked from the download the manifest names and their folder is there.
bool RealHost::SystemFilesGood(const Manifest& manifest, const Component& files) const {
    const Download* download = files.DownloadFor(deps_.platform_key);
    const auto it = system_files_.find(SystemFilesKey(manifest, files));
    return download != nullptr && it != system_files_.end() && it->second == download->sha256 &&
           BiosPresent(bios_dir_, {download->entry, false});
}

Console RealHost::Describe(const Loaded& loaded, const SystemDef& system) const {
    Console console = system.console;
    console.developer = loaded.developer;
    const std::size_t origin = OriginOf(loaded);
    if (origin > 0) {
        console.source = views_[origin].label;
    } else if (loaded.local) {
        console.source = "a file";
    }
    if (console.runner == Runner::Engine) {
        console.installed = true;
    } else {
        const Component* core = ChosenCore(loaded, system);
        console.available = core != nullptr;
        console.installed = core != nullptr && CoreGood(loaded, *core);
        if (console.installed && core->type == TYPE_ENGINE_APP) {
            console.update_available = EngineAppUpdate(loaded, *core);
        } else if (console.installed) {
            const CoreRecord* record = ledger_.Find(LedgerKey(loaded, *core));
            const Download* download = core->DownloadFor(deps_.platform_key);
            console.update_available = record != nullptr && download != nullptr && download->file.empty() &&
                                       record->download_sha256 != download->sha256;
            for (const Component* files : loaded.manifest.SystemFilesFor(core->id, deps_.platform_key)) {
                console.update_available = console.update_available || !SystemFilesGood(loaded.manifest, *files);
            }
            // A core chosen for some games (GBA DS) that came after the console was installed.
            for (const std::string& id : system.cores) {
                const Component* other = loaded.manifest.FindComponent(id);
                if (other != nullptr && other != core && other->type == TYPE_CORE && !other->games.Empty() &&
                    other->DownloadFor(deps_.platform_key) != nullptr && !CoreGood(loaded, *other)) {
                    console.update_available = true;
                }
            }
        }
    }
    // Only the plugin's own source updates it.
    const SourceView& view = views_[origin];
    if (const IndexPlugin* offered = view.Offers() ? view.index->Find(loaded.manifest.id) : nullptr) {
        console.update_available =
            console.update_available || (console.installed && offered->version > loaded.manifest.version);
    }
    return console;
}

// The manifest's settings that set a core option (or an engine app's setting), after the forced ones.
void RealHost::AddSettingHints(const Loaded& loaded, const SystemDef& system, const Component& core,
                               CoreInfo& info) const {
    for (const ManifestSetting& setting : loaded.manifest.settings) {
        if (!setting.core_option.empty() && setting.Applies(system.console.id, deps_.platform_key, core.id)) {
            const std::string key = "console." + system.console.id + "." + setting.spec.key;
            const std::string value = config_.settings != nullptr
                                          ? config_.settings->GetString(key, setting.spec.default_value)
                                          : setting.spec.default_value;
            info.option_hints.emplace_back(setting.core_option, setting.OptionValue(value));
        }
    }
}

void RealHost::AddLanguageHint(const Component& core, CoreInfo& info) const {
    const std::string app = config_.settings != nullptr ? config_.settings->GetString("ui.language", "en") : "en";
    if (const std::string value = core.LanguageValue(app); !value.empty()) {
        info.option_hints.emplace_back(core.language_option, value);
    }
}

CoreInfo RealHost::Describe(const Loaded& loaded, const SystemDef& system, const Component& core) const {
    if (core.type == TYPE_ENGINE_APP) {
        return DescribeEngineApp(loaded, system, core);
    }
    CoreInfo info;
    info.id = core.id;
    info.name = core.name;
    info.system = system.console.id;
    info.system_dir = bios_dir_;
    info.needs_gl = core.opengl;
    AddLanguageHint(core, info); // first, so the manifest's own hints and the user's options win
    info.option_hints.insert(info.option_hints.end(), core.option_hints.begin(), core.option_hints.end());
    info.option_hints.insert(info.option_hints.end(), core.forced_core_options.begin(), core.forced_core_options.end());
    AddSettingHints(loaded, system, core, info);
    info.screens = core.screens.empty() ? system.console.screens : core.screens;
    info.netplay = core.netplay;
    info.options_name = core.options_name.empty() ? core.id : core.options_name;
    info.tuning = core.tuning;
    info.widescreen_options = core.widescreen_options;
    info.widescreen_restart = core.widescreen_restart;
    info.games = core.games;
    info.missing_bios = MissingBios(bios_dir_, system.console, core.id);
    info.installed = CoreGood(loaded, core);
    if (info.installed) {
        info.library = ledger_.Directory() / PathFromUtf8(ledger_.Find(LedgerKey(loaded, core))->file);
    }
    return info;
}

// Manifests stay in the order of their sources, so the official source's console wins a
// system id over an added source's, and the source added first over a later one.
void RealHost::AddManifest(Manifest manifest, bool from_index, const std::string& origin, bool developer,
                           bool local) {
    const auto it = std::find_if(manifests_.begin(), manifests_.end(),
                                 [&](const Loaded& l) { return l.manifest.id == manifest.id; });
    if (it == manifests_.end()) {
        manifests_.push_back({std::move(manifest), from_index, origin, developer, local});
    } else if (local != it->local && !developer) {
        // An unsigned file never stands in for a signed plugin, nor the other way round.
        log::Warn("plugins", "{} installed from a file is not used: another copy came first", manifest.id);
        return;
    } else if (it->origin != origin) {
        log::Warn("plugins", "{} from {} is not used: it came from {} first", manifest.id, origin, it->origin);
        return;
    } else if (developer || (!it->developer && manifest.version >= it->manifest.version)) {
        // The developer folder's copy is the one used, whatever its version.
        *it = {std::move(manifest), from_index, origin, developer, local};
    }
    std::stable_sort(manifests_.begin(), manifests_.end(), [this](const Loaded& a, const Loaded& b) {
        return OriginOf(a) < OriginOf(b);
    });
}

bool RealHost::Allowed(const Manifest& manifest, std::size_t origin) {
    if (origin == 0 || origin >= views_.size()) {
        return origin == 0;
    }
    const Status status = CheckThirdPartyManifest(manifest, views_[origin].ns);
    if (!status.ok) {
        log::Warn("plugins", "{}: {}", views_[origin].label, status.error);
        std::vector<std::string>& notes = states_[origin].notes;
        if (std::find(notes.begin(), notes.end(), status.error) == notes.end()) {
            notes.push_back(status.error);
        }
    }
    return status.ok;
}

void RealHost::LoadManifests() {
    if (deps_.bundled) {
        std::size_t count = 0;
        const BundledManifest* bundled = BundledManifests(count);
        for (std::size_t i = 0; i < count; ++i) {
            Result<Manifest> manifest = ParseManifest(
                std::string_view(reinterpret_cast<const char*>(bundled[i].data), bundled[i].size));
            if (manifest) {
                AddManifest(manifest.take(), false, views_[0].address);
            } else {
                log::Warn("plugins", "bundled {}: {}", bundled[i].id, manifest.error());
            }
        }
    }
    for (const fs::path& dir : ListDirectory(plugins_dir_)) {
        const auto text = ReadTextFile(dir / "plugin.json");
        if (!text) {
            continue;
        }
        const std::string named = ReadOrigin(dir);
        const std::string origin = named.empty() ? views_[0].address : named;
        const std::size_t source = FindView(views_, origin);
        if (source == views_.size()) {
            log::Warn("plugins", "{}: its source {} was removed", PathToUtf8(dir.filename()), origin);
            continue;
        }
        Result<Manifest> manifest = ParseManifest(*text);
        if (manifest && manifest->id == PathToUtf8(dir.filename())) {
            if (Allowed(*manifest, source)) {
                AddManifest(manifest.take(), true, origin);
            }
        } else {
            log::Warn("plugins", "{}: {}", PathToUtf8(dir.filename()),
                      manifest ? "the folder is not named after the plugin" : manifest.error());
        }
    }
    LoadLocalManifests();
    LoadDeveloperManifests();
}

void RealHost::LoadDeveloperManifests() {
    if (config_.settings == nullptr || !config_.settings->GetBool(DEVELOPER_KEY, false)) {
        return;
    }
    for (const fs::path& dir : ListDirectory(dev_dir_)) {
        const auto text = ReadTextFile(dir / "plugin.json");
        if (!text || IsFeatureManifest(*text)) {
            continue; // a feature or interface plugin is the feature host's
        }
        Result<Manifest> manifest = ParseManifest(*text, true);
        Status status = manifest ? CheckDeveloperManifest(*manifest) : manifest.status();
        if (status.ok && manifest->id != PathToUtf8(dir.filename())) {
            status = Status::Fail("the folder is not named after the plugin");
        }
        if (!status.ok) {
            log::Warn("plugins", "developer folder, {}: {}", PathToUtf8(dir.filename()), status.error);
            continue;
        }
        AddManifest(manifest.take(), false, views_[0].address, true);
    }
}

void RealHost::ReloadDeveloper() {
    std::lock_guard lock(mutex_);
    // Read again from the start: a developer copy may have replaced the official one of its id.
    manifests_.clear();
    LoadManifests();
    checked_.clear();
    ++revision_;
}

void RealHost::LoadState() {
    const auto text = ReadTextFile(plugins_dir_ / "state.json");
    if (const auto root = text ? ParseJson(*text) : std::nullopt) {
        states_[0].last_sequence = static_cast<std::int64_t>(json::Number(*root, "last_sequence", 0));
        states_[0].fetched = static_cast<std::int64_t>(json::Number(*root, "fetched", 0));
        if (const JsonValue* files = root->Find("system_files")) {
            for (const auto& [key, sha256] : files->members) {
                if (sha256.kind == JsonValue::Kind::String) {
                    system_files_[key] = sha256.string;
                }
            }
        }
    }
}

void RealHost::SaveState() const {
    std::vector<std::pair<std::string, JsonValue>> files;
    for (const auto& [key, sha256] : system_files_) {
        files.emplace_back(key, JsonString(sha256));
    }
    WriteFileAtomic(plugins_dir_ / "state.json",
                    WriteJson(JsonObject({
                        {"last_sequence", JsonNumber(static_cast<double>(states_[0].last_sequence))},
                        {"fetched", JsonNumber(static_cast<double>(states_[0].fetched))},
                        {"system_files", JsonObject(std::move(files))},
                    })));
}

} // namespace den::plugins
