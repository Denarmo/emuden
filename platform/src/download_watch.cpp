// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/base/files.h"
#include "den/platform/downloads.h"

namespace den::pal {

DownloadWatch::DownloadWatch(std::vector<std::filesystem::path> folders, Wanted wanted)
    : folders_(std::move(folders)), wanted_(std::move(wanted)) {
    known_ = List();
}

DownloadWatch::Listing DownloadWatch::List() const {
    Listing listing;
    for (const std::filesystem::path& folder : folders_) {
        for (const std::filesystem::path& file : ListDirectory(folder)) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec) || ec) {
                continue;
            }
            Stamp stamp;
            stamp.size = std::filesystem::file_size(file, ec);
            if (ec) {
                continue;
            }
            const auto time = std::filesystem::last_write_time(file, ec);
            if (ec) {
                continue;
            }
            stamp.time = static_cast<std::int64_t>(time.time_since_epoch().count());
            listing[file] = stamp;
        }
    }
    return listing;
}

std::vector<DownloadWatch::File> DownloadWatch::Poll() {
    std::vector<File> found;
    Listing pending;
    for (const auto& [file, stamp] : List()) {
        if (const auto known = known_.find(file); known != known_.end() && known->second == stamp) {
            continue;
        }
        const std::string name = PathToUtf8(file.filename());
        if (stamp.size == 0 || (wanted_ && !wanted_(name))) {
            continue;
        }
        if (const auto before = pending_.find(file); before != pending_.end() && before->second == stamp) {
            known_[file] = stamp;
            found.push_back({file, name, stamp.size});
        } else {
            pending[file] = stamp;
        }
    }
    pending_ = std::move(pending);
    return found;
}

} // namespace den::pal
