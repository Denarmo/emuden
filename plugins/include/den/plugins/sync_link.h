// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What den.sync of a sync provider plugin reaches (docs/sync.md, section 5): the sync engine,
// which the app hands to the feature host. The plugins module only knows this interface.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "den/base/json.h"
#include "den/base/result.h"

namespace den::plugins {

class SyncLink {
public:
    virtual ~SyncLink() = default;
    /// den.sync.status() of the provider, as the table docs/sync.md describes.
    virtual JsonValue StatusOf(const std::string& provider) = 0;
    virtual Status SyncNow(const std::string& provider) = 0;
    virtual std::vector<std::string> EnabledCategories(const std::string& provider) = 0;
    /// The folder of the host's folder store for the provider; empty when it has none.
    virtual std::filesystem::path Folder(const std::string& provider) = 0;
    /// Whether a folder may hold the folder store (not inside the app's data, not a game
    /// folder of the library, not a drive's root).
    virtual Status CheckFolder(const std::filesystem::path& folder) = 0;
    virtual Status UseFolder(const std::string& provider, const std::filesystem::path& folder) = 0;
    /// What "~/" stands for: the user's home on a desktop, the shared storage on Android.
    virtual std::filesystem::path Home() = 0;
    /// Blocking, off the main thread: fetches the remote files of one game (portable names
    /// under games/) into the games folder and adds them to the library.
    virtual Status DownloadGame(const std::string& provider, const std::vector<std::string>& paths) = 0;
};

} // namespace den::plugins
