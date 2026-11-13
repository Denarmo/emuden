// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The user's own browser and Downloads folder, for systems without a web engine the app can
// show a page with. The app opens an address in the user's browser and afterwards notices
// the files that appear in the Downloads folder; it never looks at what was there before.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::pal {

/// Opens an http or https address in the user's browser (xdg-open, the shell, an intent).
Status OpenInBrowser(const std::string& url);

/// The folders a browser saves to that exist here: XDG_DOWNLOAD_DIR when set, the system's
/// Downloads folder, and Downloads in the home folder. Empty when there is none to list.
std::vector<std::filesystem::path> DownloadFolders();

/// Notices files that appear in folders, by listing them from time to time. Nothing is
/// opened or read, only names, sizes and times.
class DownloadWatch {
public:
    struct File {
        std::filesystem::path path;
        std::string name;
        std::uint64_t size = 0;
    };
    /// Whether a file of this name is wanted at all.
    using Wanted = std::function<bool(const std::string& name)>;

    /// Notes what the folders hold now: those files are never reported.
    DownloadWatch(std::vector<std::filesystem::path> folders, Wanted wanted);

    /// Lists the folders again. Returns the wanted files that are new since the watch began
    /// (or were written again since) and whose size and time held still from the previous
    /// Poll to this one, so a file still arriving is not reported. Each file is reported once.
    std::vector<File> Poll();

    const std::vector<std::filesystem::path>& Folders() const {
        return folders_;
    }

private:
    struct Stamp {
        std::uint64_t size = 0;
        std::int64_t time = 0;
        bool operator==(const Stamp&) const = default;
    };
    using Listing = std::map<std::filesystem::path, Stamp>;
    Listing List() const;

    std::vector<std::filesystem::path> folders_;
    Wanted wanted_;
    Listing known_;   // there when the watch began, or already reported
    Listing pending_; // new at the previous Poll
};

} // namespace den::pal
