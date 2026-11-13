// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <SDL3/SDL.h>

#include "den/base/files.h"
#include "den/platform/downloads.h"
#include "den/platform/webview.h"

namespace den::pal {

Status OpenInBrowser(const std::string& url) {
    const std::string scheme = WebUrlScheme(url);
    if ((scheme != "https" && scheme != "http") || WebUrlHost(url).empty()) {
        return Status::Fail("only web addresses are opened in the browser");
    }
    if (!SDL_OpenURL(url.c_str())) {
        return Status::Fail(std::string("the browser could not be opened: ") + SDL_GetError());
    }
    return {};
}

std::vector<std::filesystem::path> DownloadFolders() {
    std::vector<std::filesystem::path> folders;
    const auto add = [&](const std::string& text) {
        if (text.empty()) {
            return;
        }
        std::error_code ec;
        std::filesystem::path folder = PathFromUtf8(text);
        if (!std::filesystem::is_directory(folder, ec)) {
            return;
        }
        if (const auto same = std::filesystem::weakly_canonical(folder, ec); !ec) {
            folder = same;
        }
        if (std::find(folders.begin(), folders.end(), folder) == folders.end()) {
            folders.push_back(folder);
        }
    };
    add(GetEnv("XDG_DOWNLOAD_DIR"));
    if (const char* system = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)) {
        add(system);
    }
    if (const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME)) {
        add(PathToUtf8(PathFromUtf8(home) / "Downloads"));
    }
    return folders;
}

} // namespace den::pal
