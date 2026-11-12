// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the plugin host: which consoles exist, which core plays a system, and installing
// it from the signed plugin index. The implementation behind CreateHost is a fake
// (src/fake_host.cpp) until the real one replaces it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"

namespace den::store {
class Settings;
}

namespace den::plugins {

/// How a console's games are run.
enum class Runner {
    Core,   // a libretro core inside the app (den::player)
    Engine, // an out-of-app engine started by den::engine (Switch 2)
};

/// A console, from a plugin manifest's systems[].
struct Console {
    std::string id;         // system id: "gb", "gba", "nds", "psx", "switch2"
    std::string name;       // "Game Boy Advance"
    std::string short_name; // "GBA"
    std::string plugin_id;  // the plugin that provides it
    std::vector<std::string> extensions; // lower case, without the dot
    Runner runner = Runner::Core;
    bool installed = false; // its core or engine is on disk and verified
};

/// The core that plays a system on this device.
struct CoreInfo {
    std::string id;     // "mgba"
    std::string name;   // "mGBA"
    std::string system;
    std::filesystem::path library;    // the core file to load; empty until installed
    std::filesystem::path system_dir; // where the core looks for BIOS files
    bool installed = false;
    bool needs_gl = false; // the core renders with OpenGL itself
    /// Option values the manifest recommends, applied before the user's own.
    std::vector<std::pair<std::string, std::string>> option_hints;
    /// BIOS files the core needs and does not find (empty = ready).
    std::vector<std::string> missing_bios;
};

class Host {
public:
    virtual ~Host() = default;

    /// Every console the installed manifests and the index describe.
    virtual std::vector<Console> Consoles() const = 0;
    virtual std::optional<Console> FindConsole(const std::string& system) const = 0;
    /// The core for a system on this device; nullopt for engine consoles and unknown systems.
    virtual std::optional<CoreInfo> CoreFor(const std::string& system) const = 0;
    virtual bool Installed(const std::string& system) const = 0;

    /// Downloads, verifies (SHA-256 and size from the signed index) and unpacks what the
    /// system needs. Progress and cancel through the job; the failure text is user-facing.
    virtual JobPtr Install(const std::string& system) = 0;
    virtual JobPtr Remove(const std::string& system) = 0;
    /// Fetches the signed index of every source again.
    virtual JobPtr Refresh() = 0;
    /// Grows whenever consoles or their installed state changed.
    virtual std::uint64_t Revision() const = 0;
};

struct Config {
    std::filesystem::path data_dir; // plugins live under data_dir / "plugins", cores under "cores"
    Jobs* jobs = nullptr;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Host> CreateHost(const Config& config);

} // namespace den::plugins
