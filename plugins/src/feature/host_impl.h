// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The feature host: which feature plugins are installed and offered (feature_catalog.cpp),
// installing and removing them (feature_install.cpp), and running them (feature_host.cpp).

#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "den/plugins/feature_host.h"
#include "den/plugins/feature_manifest.h"
#include "den/plugins/feature_services.h"
#include "den/plugins/sources.h"
#include "feature/runner.h"

namespace den::plugins::feature {

namespace fs = std::filesystem;

inline constexpr std::size_t MAX_PACKAGE_FILES = 200;
inline constexpr std::uint64_t MAX_PACKAGE_BYTES = 8ull << 20;

/// Unpacks a feature package (a zip) into `dir`: at most 200 files and 8 MiB, only .json,
/// .lua, .png, .jpg, .txt and .md, safe relative names, no links, nothing encrypted.
Status UnpackFeaturePackage(const fs::path& archive, const fs::path& dir, Job* job);

/// A version of a plugin on this device that can run.
struct Package {
    FeatureManifest manifest;
    fs::path dir;
    FeaturePlugin::Trust trust = FeaturePlugin::Trust::Official;
    bool bundled = false;       // ships with the app; cannot be removed
    FeatureAccess granted;      // grant.json
    std::uint64_t data_bytes = 0;
    std::string origin; // the address of the source it came from
};

class RealFeatureHost final : public FeatureHost {
public:
    RealFeatureHost(const FeatureConfig& config, FeatureDeps deps);
    ~RealFeatureHost() override;

    std::vector<FeaturePlugin> Plugins() const override;
    std::uint64_t Revision() const override {
        return revision_.load();
    }
    JobPtr Install(const std::string& id, bool consent) override;
    JobPtr Remove(const std::string& id, bool delete_data, bool delete_hacks) override;
    Status Open(const std::string& id) override;
    void Close(const std::string& id) override;
    void Reload(const std::string& id) override;
    std::vector<PageModel> Pages(const std::string& id, std::uint64_t& revision) const override;
    void Send(const std::string& id, UiEvent event) override;
    void Back(const std::string& id) override;
    std::vector<Ask> TakeAsks(const std::string& id) override;
    void Answer(std::uint64_t ask_id, bool accepted, const std::string& value) override;
    fs::path ImagePath(const std::string& id, const std::string& image) override;
    std::string RouteFile(const fs::path& file) override;
    void Pump() override;
    void SetSyncLink(std::shared_ptr<SyncLink> link) override;
    void Invoke(const std::string& id, std::vector<std::string> path, std::vector<JsonValue> args,
                std::function<void(Result<std::vector<JsonValue>>)> done,
                std::function<void(double, double)> progress) override;
    std::string LendFile(const std::string& id, const fs::path& file) override;
    void ReturnFile(const std::string& token) override;

private:
    struct Running {
        std::shared_ptr<Runner> runner;
        bool open = false;
        std::chrono::steady_clock::time_point closed_at;
    };

    // feature_catalog.cpp
    void Scan();
    /// The sources, the official one first; one made of the deps' index in tests.
    std::vector<SourceView> Views() const;
    /// The view a package's revocations and updates come from (bundled and developer
    /// packages: the first); views.size() when its source is gone.
    static std::size_t ViewOf(const std::vector<SourceView>& views, const Package& package);
    FeatureAccess ReadGrant(const std::string& id) const;
    bool WriteGrant(const std::string& id, const FeatureAccess& access) const;
    fs::path DataDir(const std::string& id) const {
        return data_root_ / PathOf(id);
    }
    static fs::path PathOf(const std::string& id);

    // feature_install.cpp
    Status DoInstall(Job& job, const std::string& id, bool consent);
    Status DoRemove(const std::string& id, bool delete_data, bool delete_hacks);

    // feature_host.cpp
    std::shared_ptr<Runner> RunnerOf(const std::string& id) const;
    /// Starts the plugin's script when it is not running. Needs mutex_.
    Result<std::shared_ptr<Runner>> Start(const std::string& id);
    /// Lets plugin.close run, then drops the script, off the main thread.
    void Retire(std::shared_ptr<Runner> runner);
    std::shared_ptr<Runner> TakeRunner(const std::string& id);

    FeatureConfig config_;
    FeatureDeps deps_;
    fs::path plugins_dir_; // <data>/plugins/<id>/<version>/
    fs::path dev_dir_;     // <data>/plugins-dev/<id>/
    fs::path data_root_;   // <data>/plugin-data/<id>/

    mutable std::mutex mutex_;
    std::map<std::string, Package> installed_;
    std::map<std::string, Running> running_;
    std::set<std::string> fetching_images_;
    std::shared_ptr<SyncLink> sync_link_;
    std::map<std::string, std::pair<std::string, fs::path>> lent_; // token -> plugin, file
    std::uint64_t next_lend_ = 1;
    std::uint64_t host_revision_ = 0; // the console host's, when the catalogue was last read
    std::string state_signature_;
    std::atomic<std::uint64_t> revision_{1};
};

} // namespace den::plugins::feature
