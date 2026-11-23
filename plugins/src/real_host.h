// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The plugin host: consoles from the manifests, cores from pinned downloads, plugins from the
// official source and the sources the user added. The queries are in host_query.cpp,
// fetching and installing in host_install.cpp, the source list in host_sources.cpp.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <system_error>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/plugins/index.h"
#include "den/plugins/installer.h"
#include "den/plugins/manifest.h"
#include "den/plugins/signature.h"
#include "den/platform/engine_apps.h"
#include "den/plugins/sources.h"
#include "den/store/settings.h"
#include "feature/index_source.h"
#include "json_util.h"

namespace den::plugins {

namespace fs = std::filesystem;

inline constexpr std::int64_t POLL_INTERVAL_SECONDS = 6 * 3600;
inline constexpr std::size_t MAX_SYSTEM_FILES = 8192;
inline constexpr std::uint64_t MAX_SYSTEM_FILES_BYTES = 512ull << 20;

/// The origin of a core installed for a plugin of the developer folder.
inline constexpr const char* DEVELOPER_ORIGIN = "developer";

struct Loaded {
    Manifest manifest;
    bool from_index = false; // its package came from a signed index, not from the app
    std::string origin;      // the address of its source (the official one for the developer folder)
    bool developer = false;  // from <data>/plugins-dev, unsigned
    bool local = false;      // installed from a file, unsigned (<data>/plugins-local)
};

/// The origin of a core installed for an unsigned plugin installed from a file.
inline constexpr const char* LOCAL_ORIGIN = "local";

inline std::string FileNameOf(const Download& download) {
    const std::string& path = download.archive == "zip" ? download.entry : download.url;
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool BiosPresent(const fs::path& dir, const BiosFile& file);
std::vector<std::string> MissingBios(const fs::path& dir, const Console& console,
                                     std::string_view core_id = {});

class RealHost final : public Host, public IndexSource {
public:
    RealHost(const Config& config, HostDeps deps);

    std::vector<SourceView> SourceViews() const override {
        std::lock_guard lock(mutex_);
        return views_;
    }

    std::vector<Console> Consoles() const override;
    std::optional<Console> FindConsole(const std::string& system) const override;
    std::optional<CoreInfo> CoreFor(const std::string& system) const override;
    std::vector<CoreInfo> CoresFor(const std::string& system) const override;
    void SelectCore(const std::string& system, const std::string& core_id) override;
    std::vector<SettingSpec> SettingsFor(const std::string& system) const override;
    bool Installed(const std::string& system) const override;
    Status VerifyCore(const fs::path& library) const override;

    JobPtr Install(const std::string& system) override {
        return config_.jobs->Run("Install " + system,
                                 [this, system](Job& job) { return DoInstall(job, system); });
    }
    JobPtr InstallCore(const std::string& system, const std::string& core_id) override {
        return config_.jobs->Run("Install " + core_id,
                                 [this, system, core_id](Job& job) { return DoInstall(job, system, core_id); });
    }
    JobPtr Remove(const std::string& system) override {
        return config_.jobs->Run("Remove " + system,
                                 [this, system](Job&) { return DoRemove(system); });
    }
    JobPtr Refresh() override {
        return config_.jobs->Run("Check for plugin updates", [this](Job& job) {
            job.SetMessage("Checking the plugin list");
            Status status = FetchAll(job, true);
            if (status.ok) {
                const Status synced = SyncPackages(job, {});
                status.notes.insert(status.notes.end(), synced.notes.begin(), synced.notes.end());
            }
            return status;
        });
    }
    std::uint64_t Revision() const override {
        return revision_.load();
    }

    void ReloadDeveloper() override;
    std::filesystem::path DeveloperDir() const override {
        return dev_dir_;
    }

    // host_sources.cpp
    std::vector<SourceInfo> Sources() const override;
    JobPtr ProbeSource(const std::string& text, std::shared_ptr<SourceProbe> out) override;
    Status AddSource(const SourceProbe& probe) override;
    Status SetSourceEnabled(const std::string& address, bool enabled) override;
    JobPtr RemoveSource(const std::string& address) override;

    // host_local.cpp
    Result<PackageProbe> ProbePackage(const fs::path& path) const override;
    Status InstallPackage(const PackageProbe& probe) override;
    std::vector<CarryOver> CarryOvers() const override;
    JobPtr SettleCarryOver(const std::string& plugin_id, bool keep) override;

private:
    struct Found {
        const Loaded* loaded = nullptr;
        const SystemDef* system = nullptr;
    };

    struct Checked {
        std::uintmax_t size = 0;
        fs::file_time_type time;
        bool good = false;
    };

    // What the host keeps of each source beside its view; same order as views_.
    struct SourceState {
        std::vector<std::string> keys;
        std::string key_url;
        fs::path dir; // its cached index.json and .sig, and its state.json
        std::int64_t last_sequence = 0;
        std::int64_t fetched = 0; // last good check
        std::string error;
        std::vector<std::string> notes; // its manifests that were refused, and why
    };

    // host_query.cpp. Everything below needs mutex_.
    std::size_t OriginOf(const Loaded& loaded) const {
        return FindView(views_, loaded.origin);
    }
    bool Usable(const Loaded& loaded) const;
    Found Find(const std::string& system) const;
    const Component* ChosenCore(const Loaded& loaded, const SystemDef& system) const;
    /// The core's key in the ledger: its id, prefixed with its plugin's for an added source.
    std::string LedgerKey(const Loaded& loaded, const Component& core) const;
    std::function<std::string(std::string_view)> Blocked(std::size_t origin) const;
    bool CoreGood(const Loaded& loaded, const Component& core) const;
    static std::string SystemFilesKey(const Manifest& manifest, const Component& files) {
        return manifest.id + "/" + files.id;
    }
    bool SystemFilesGood(const Manifest& manifest, const Component& files) const;
    Console Describe(const Loaded& loaded, const SystemDef& system) const;
    /// The core's language option, given the app's language ("ui.language"), before other hints.
    void AddLanguageHint(const Component& core, CoreInfo& info) const;
    Texts TranslationsFor(std::string_view language) const override;
    void AddSettingHints(const Loaded& loaded, const SystemDef& system, const Component& core, CoreInfo& info) const;
    CoreInfo Describe(const Loaded& loaded, const SystemDef& system, const Component& core) const;
    void AddManifest(Manifest manifest, bool from_index, const std::string& origin, bool developer = false,
                     bool local = false);
    /// Every other console plugin it depends on is there, in a version of its range.
    bool DependsMet(const Manifest& manifest) const;
    /// The console plugins of the developer folder, when developer mode is on.
    void LoadDeveloperManifests();
    /// The rules of the manifest's source; a note on that source when it breaks one.
    bool Allowed(const Manifest& manifest, std::size_t origin);
    void LoadManifests();
    void LoadState();

    // host_local.cpp
    /// The unsigned console plugins installed from files.
    void LoadLocalManifests();
    /// On the first start, the cached official list names the console plugins the app had
    /// before: those it has no manifest of now were built in and are carried over.
    void FindCarryOvers();
    void SaveCarryOvers() const;
    Status KeepCarryOver(Job& job, const std::string& plugin_id);
    void SaveState() const;

    // host_sources.cpp
    void LoadSources();
    void SaveSources() const;
    void RemoveOrphans();
    void LoadCachedIndex(std::size_t source);
    void SaveSourceState(std::size_t source) const;
    void UpdateRevokedKeys();
    Status DoRemoveSource(const std::string& address);

    // host_install.cpp
    /// Fetches every switched-on source that is due (all with `force`). Fails only when the
    /// official source fails; an added source's failure is a note and stays on its row.
    Status FetchAll(Job& job, bool force);
    Status FetchSource(Job& job, const std::string& address, bool force);

    // Brings the manifests the sources offer in a newer version (or that the app does not
    // have at all) from their packages. `only` limits it to one plugin, where a failure counts.
    Status SyncPackages(Job& job, const std::string& only);
    Status FetchPackage(Job& job, const IndexPlugin& offered, const std::string& address);
    Status DoInstall(Job& job, const std::string& system, const std::string& core_id = {});
    Status InstallCore(Job& job, const Loaded& loaded, const Component& core,
                       const Download& download);
    // Puts the files a core needs into the system folder. A failure is a note on `result`:
    // the core is installed and most games do not need them.
    void InstallSystemFiles(Job& job, const Loaded& loaded, const std::string& core_id,
                            Status& result);
    Status DoRemove(const std::string& system);

    // Engine apps as cores (engine_app_install.cpp). Their records are keyed like the
    // ledger's (LedgerKey), and their folders are named after that key.
    struct EngineRecord {
        std::string executable; // relative to the engine's folder
        std::string executable_sha256;
        std::string download_sha256;
        std::string version;
        std::string origin; // the address of the source it came from
    };
    fs::path EnginesDir() const;
    fs::path EngineDir(const std::string& key) const;
    void LoadEngines() const;
    bool SaveEngines() const;
    pal::engine_apps::Installed QueryApp(const std::string& package, bool fresh) const;
    static bool AppMatches(const Component& core, const pal::engine_apps::Installed& app);
    bool EngineAppGood(const Loaded& loaded, const Component& core) const;
    bool EngineAppUpdate(const Loaded& loaded, const Component& core) const;
    CoreInfo DescribeEngineApp(const Loaded& loaded, const SystemDef& system, const Component& core) const;
    Status InstallEngineApp(Job& job, const Loaded& loaded, const Component& core, Status result);
    Status InstallAndroidApp(Job& job, const Component& core, Status result);
    Status RemoveEngineApp(const Loaded& loaded, const Component& core);
    // The desktop files of an engine app, or of the built-in engine a console plugin downloads
    // (type engine), under engines/<key>/. The built-in engine's key is its system's id, the
    // folder engine::Locate looks in.
    bool EngineFilesGood(const std::string& key, std::size_t origin) const;
    bool EngineFilesUpdate(const std::string& key, const Component& core) const;
    Status InstallEngineFiles(Job& job, const Loaded& loaded, const std::string& key, const Component& core,
                              bool own_folder, Status result);
    Status RemoveEngineFiles(const std::string& key, const std::string& name);

    Config config_;
    HostDeps deps_;
    fs::path plugins_dir_;
    fs::path dev_dir_; // <data>/plugins-dev/<id>/plugin.json, shared with the feature host
    fs::path local_dir_; // <data>/plugins-local/<id>/plugin.json
    fs::path bios_dir_;

    mutable std::mutex mutex_;
    std::vector<Loaded> manifests_; // the official source's first, then by source
    CoreLedger ledger_;
    std::vector<SourceView> views_; // [0] is the official source
    std::vector<SourceState> states_;
    std::map<std::string, std::string, std::less<>> system_files_; // plugin/component -> sha256
    mutable std::map<std::string, Checked, std::less<>> checked_;
    std::vector<CarryOver> carry_overs_;
    mutable bool engines_loaded_ = false;
    mutable std::map<std::string, EngineRecord, std::less<>> engines_;
    mutable std::map<std::string, std::pair<std::uint64_t, pal::engine_apps::Installed>, std::less<>> apps_;
    std::atomic<std::uint64_t> revision_{1};
};

} // namespace den::plugins
