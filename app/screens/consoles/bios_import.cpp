// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/app/bios_import.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/base/files.h"

namespace den::app {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t MAX_FILE = 32ull << 20; // a BIOS is at most a few MiB
constexpr int MAX_SEEN = 20000;
constexpr int MAX_DEPTH = 4;

int g_imports = 0;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool Small(const fs::path& file) {
    std::error_code error;
    return fs::is_regular_file(file, error) && fs::file_size(file, error) <= MAX_FILE && !error;
}

bool Copy(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    return fs::copy_file(from, to, fs::copy_options::overwrite_existing, error) && !error;
}

// Every small file under `root` by lower-case name; the shallowest copy wins.
std::map<std::string, fs::path> FilesByName(const fs::path& root) {
    std::map<std::string, fs::path> files;
    std::error_code error;
    int seen = 0;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end && seen < MAX_SEEN; it.increment(error), ++seen) {
        if (it.depth() >= MAX_DEPTH) {
            it.disable_recursion_pending();
        }
        if (Small(it->path())) {
            files.emplace(Lower(PathToUtf8(it->path().filename())), it->path());
        }
    }
    return files;
}

// For a BIOS folder ("pcsx2/bios/"): a sub-folder of that name if there is one, else the
// chosen folder itself; its files are copied.
int CopyFolder(const fs::path& root, const fs::path& target) {
    fs::path source = root;
    const std::string name = Lower(PathToUtf8(target.filename()));
    std::error_code error;
    for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (it->is_directory(error) && Lower(PathToUtf8(it->path().filename())) == name) {
            source = it->path();
            break;
        }
    }
    int copied = 0;
    for (fs::directory_iterator it(source, error), end; !error && it != end; it.increment(error)) {
        if (Small(it->path()) && Copy(it->path(), target / it->path().filename())) {
            ++copied;
        }
    }
    return copied;
}

int CopyBios(const plugins::Console& console, const fs::path& root, const fs::path& system_dir) {
    const std::map<std::string, fs::path> files = FilesByName(root);
    int copied = 0;
    for (const plugins::BiosSet& set : console.bios) {
        for (const plugins::BiosFile& file : set.files) {
            if (!file.path.empty() && file.path.back() == '/') {
                const fs::path folder = system_dir / PathFromUtf8(file.path.substr(0, file.path.size() - 1));
                copied += CopyFolder(root, folder);
                continue;
            }
            const fs::path target = system_dir / PathFromUtf8(file.path);
            const auto found = files.find(Lower(PathToUtf8(target.filename())));
            if (found != files.end() && Copy(found->second, target)) {
                ++copied;
            }
        }
    }
    return copied;
}

} // namespace

void ImportBios(const plugins::Console& console, const fs::path& system_dir) {
    PickOptions options;
    options.title = "Choose the folder with the BIOS files";
    Pick(std::move(options), [console, system_dir](const std::string& path) {
        const int copied = CopyBios(console, PathFromUtf8(path), system_dir);
        Svc().stage->Toast(copied ? "BIOS files copied: " + std::to_string(copied)
                                  : std::string("No BIOS file of this console is in that folder"),
                           4.0f);
        ++g_imports;
    });
}

int BiosImports() {
    return g_imports;
}

} // namespace den::app
