// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Where the folder picker starts on Windows: the user's profile and the drive letters.

#include "den/base/files.h"
#include "den/platform/pal.h"

namespace den::pal {

std::vector<std::filesystem::path> StorageRoots() {
    std::vector<std::filesystem::path> roots;
    std::error_code ec;
    for (const char* variable : {"USERPROFILE", "HOME"}) {
        const std::string home = GetEnv(variable);
        if (!home.empty() && std::filesystem::is_directory(PathFromUtf8(home), ec)) {
            roots.push_back(PathFromUtf8(home));
            break;
        }
    }
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        const std::filesystem::path root = PathFromUtf8(std::string(1, letter) + ":\\");
        if (std::filesystem::exists(root, ec)) {
            roots.push_back(root);
        }
    }
    return roots;
}

OsReleaseInfo OsRelease() {
    return {};
}

} // namespace den::pal
