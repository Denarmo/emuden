// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the feature host needs from outside, replaceable in tests: the network, the clock,
// and the patching and library calls behind den.patch and den.library.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "den/base/result.h"
#include "den/plugins/feature_host.h"
#include "den/plugins/index.h"
#include "den/plugins/installer.h"

namespace den::plugins {

struct PatchFacts {
    std::string format; // "bps", "xdelta", ...
    std::optional<std::uint64_t> source_size, target_size;
    std::optional<std::uint32_t> source_crc32, target_crc32;
};

struct GameFacts {
    std::string id, name, system;
    std::string cover;   // a local image file, or empty
    std::string hack_of; // the base game's id when this is a hack
    std::uint64_t size = 0;
};

struct HackFacts {
    std::string id, name, base, version, source;
    std::int64_t installed = 0;
};

struct BaseWanted {
    std::vector<std::string> systems; // the systems to look in
    std::optional<std::uint32_t> crc32;
    std::string sha1, md5; // lower-case hex, empty = not asked
    std::optional<std::uint64_t> size;
};

struct HackOrder {
    std::string base_id;
    std::filesystem::path patch;
    std::string title, version, description, plugin, source;
    std::filesystem::path cover;
    bool keep_old = false;
};

struct HackMade {
    GameFacts game;
    std::filesystem::path path;
};

/// den.patch and den.library reach the patching module and the library through this. The
/// real one wraps den::patching and den::library::Hacks; tests use a fake.
class FeatureServices {
public:
    virtual ~FeatureServices() = default;
    virtual Result<PatchFacts> InspectPatch(const std::filesystem::path& patch) = 0;
    /// Blocking (hashes candidates): called from a worker thread.
    virtual std::optional<GameFacts> FindBase(const BaseWanted& wanted,
                                              const std::atomic<bool>& cancel) = 0;
    /// The games of these systems ("*" = every system).
    virtual std::vector<GameFacts> Games(const std::vector<std::string>& systems) = 0;
    virtual std::optional<GameFacts> Game(const std::string& id) = 0;
    virtual std::vector<HackFacts> Hacks(const std::string& plugin) = 0;
    virtual Result<HackMade> InstallHack(const HackOrder& order,
                                         const std::function<bool(float)>& progress) = 0;
    virtual Status UpdateHack(const std::string& id, const std::string& plugin,
                              const std::optional<std::string>& title,
                              const std::optional<std::string>& description,
                              const std::filesystem::path& cover) = 0;
    virtual Status RemoveHack(const std::string& id, const std::string& plugin) = 0;
};

/// The services over the app's library, or null while the build has no patching module.
std::shared_ptr<FeatureServices> CreateLibraryServices(library::Library* library);

struct FeatureDeps {
    HttpGetter http;                           // default: pal::HttpGet
    std::shared_ptr<FeatureServices> services; // default: CreateLibraryServices
    std::filesystem::path bundled_dir;         // default: pal::AssetsDir() / "plugins"
    std::filesystem::path downloads_dir;       // the device's Downloads folder; empty = none
    std::string platform;                      // den.device.platform; default: pal::GetCaps().os
    std::function<std::int64_t()> now;         // Unix seconds
    /// The signed index of the plugin source; default: the one the console host verified.
    std::function<std::optional<Index>()> index;
    std::string index_url;
    FeaturePlugin::Trust source_trust = FeaturePlugin::Trust::Official;
    std::string source_owner; // a third-party source's namespace
    bool defaults = true;     // false: leave http, bundled_dir and platform as given (tests)
    int idle_seconds = 60;    // a closed plugin's script is dropped after this
    long long instruction_limit = 200'000'000;
};

std::unique_ptr<FeatureHost> CreateFeatureHostWith(const FeatureConfig& config, FeatureDeps deps);

} // namespace den::plugins
