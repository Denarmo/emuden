// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What den.mods of feature plugins reaches in the app: the folder of an installed Steam
// game (library::steam) and the user's Vulkan implicit layer folder.

#include "den/app/feature_api.h"
#include "den/base/files.h"
#include "den/library/steam.h"
#include "den/platform/pal.h"
#include "den/plugins/feature_mods.h"

namespace den::app {

namespace {

namespace steam = library::steam;

class AppMods final : public plugins::ModServices {
public:
    Result<plugins::ModGame> SteamGame(const std::string& appid) override {
        const auto root = steam::FindRoot();
        if (!root) {
            return Status::Fail("Steam was not found on this device");
        }
        const auto folder = steam::Install(*root).FolderOf(appid);
        if (!folder) {
            return Status::Fail("that Steam game is not installed on this device");
        }
        return plugins::ModGame{appid, folder->name.empty() ? "Steam app " + appid : folder->name, folder->dir};
    }

    std::filesystem::path LayerManifestDir() override {
        if (pal::GetCaps().os != "linux") {
            return {};
        }
        // Where the Vulkan loader looks for the user's own layers.
        std::string base = GetEnv("XDG_DATA_HOME");
        if (base.empty()) {
            const std::string home = GetEnv("HOME");
            if (home.empty()) {
                return {};
            }
            base = home + "/.local/share";
        }
        return PathFromUtf8(base) / "vulkan" / "implicit_layer.d";
    }
};

} // namespace

std::shared_ptr<plugins::ModServices> CreateModServices() {
    return std::make_shared<AppMods>();
}

} // namespace den::app
