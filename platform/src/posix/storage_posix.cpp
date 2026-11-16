// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Where the folder picker starts on Linux and Android (the home folder, the root and the
// mount points of removable media), and the distribution from /etc/os-release.

#include "den/base/files.h"
#include "den/platform/pal.h"

namespace den::pal {

std::vector<std::filesystem::path> StorageRoots() {
    std::vector<std::filesystem::path> roots;
    std::error_code ec;
    if (const std::string home = GetEnv("HOME"); !home.empty() && std::filesystem::is_directory(PathFromUtf8(home), ec)) {
        roots.push_back(PathFromUtf8(home));
    }
    roots.emplace_back("/");
    for (const char* mount : {"/run/media", "/media", "/mnt", "/storage"}) {
        if (std::filesystem::is_directory(mount, ec)) {
            roots.emplace_back(mount);
        }
    }
    return roots;
}

OsReleaseInfo OsRelease() {
    const auto text = ReadTextFile("/etc/os-release");
    OsReleaseInfo info;
    std::size_t start = 0;
    while (text && start < text->size()) {
        std::size_t end = text->find('\n', start);
        end = end == std::string::npos ? text->size() : end;
        const std::string line = text->substr(start, end - start);
        start = end + 1;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string value = line.substr(equals + 1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (line.starts_with("PRETTY_NAME=")) {
            info.pretty_name = value;
        } else if (line.starts_with("ID=")) {
            info.id = ToLower(value);
        }
    }
    return info;
}

} // namespace den::pal
