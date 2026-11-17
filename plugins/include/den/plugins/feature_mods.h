// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What den.mods of feature plugins needs from the app (docs/game-mods-api.md): where a Steam
// game is installed and where this computer's Vulkan layers are listed. The app implements
// it over library::steam; tests use a fake. Every call may block and is made from the
// plugin's own thread or a helper thread.

#pragma once

#include <filesystem>
#include <string>

#include "den/base/result.h"

namespace den::plugins {

struct ModGame {
    std::string appid, name;
    std::filesystem::path dir; // the game's own folder
};

class ModServices {
public:
    virtual ~ModServices() = default;
    /// An installed Steam game (not a shortcut), or why there is none.
    virtual Result<ModGame> SteamGame(const std::string& appid) = 0;
    /// Where the user's Vulkan implicit layer manifests go
    /// ($XDG_DATA_HOME/vulkan/implicit_layer.d); empty when this computer has no such place.
    virtual std::filesystem::path LayerManifestDir() = 0;
};

} // namespace den::plugins
