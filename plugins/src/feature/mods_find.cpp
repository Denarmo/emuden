// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.mods: the paths a plugin may name in a game's folder, and looking for files there.

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/sha256.h"
#include "den/plugins/manifest.h"
#include "feature/mods.h"

namespace den::plugins::feature::mods {

namespace {

// Directory entries looked at before a search gives up.
constexpr std::size_t MAX_VISITED = 200'000;

struct Pattern {
    std::string before, after; // lower case, around the "*"
    bool star = false;

    bool Matches(const std::string& name) const {
        if (!star) {
            return name == before;
        }
        return name.size() >= before.size() + after.size() && name.starts_with(before) && name.ends_with(after);
    }
};

struct Search {
    std::vector<Pattern> patterns;
    const std::atomic<bool>* cancel = nullptr;
    std::vector<Found> found;
    std::size_t visited = 0;

    bool Over() const {
        return found.size() >= MAX_FOUND || visited >= MAX_VISITED || (cancel != nullptr && cancel->load());
    }

    void Walk(const fs::path& dir, const std::string& prefix, int depth) {
        std::vector<fs::path> entries = ListDirectory(dir);
        std::sort(entries.begin(), entries.end());
        std::vector<std::pair<fs::path, std::string>> folders;
        for (const fs::path& entry : entries) {
            if (Over()) {
                return;
            }
            ++visited;
            std::error_code ec;
            const fs::file_status status = fs::symlink_status(entry, ec);
            const std::string name = PathToUtf8(entry.filename());
            if (fs::is_directory(status)) {
                folders.emplace_back(entry, prefix + name + "/");
            } else if (fs::is_regular_file(status)) {
                const std::string lower = ToLower(name);
                if (std::any_of(patterns.begin(), patterns.end(), [&](const Pattern& p) { return p.Matches(lower); })) {
                    Found file;
                    file.path = prefix + name;
                    file.size = fs::file_size(entry, ec);
                    if (!ec && file.size <= MAX_HASHED) {
                        file.sha256 = Sha256File(entry).value_or(std::string());
                    }
                    found.push_back(std::move(file));
                }
            }
        }
        if (depth >= MAX_DEPTH) {
            return;
        }
        for (const auto& [folder, inner] : folders) {
            if (Over()) {
                return;
            }
            Walk(folder, inner, depth + 1);
        }
    }
};

} // namespace

bool IsAppId(std::string_view text) {
    return !text.empty() && text.size() <= 10 &&
           std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool IsPlainName(std::string_view name) {
    return !name.empty() && name.size() <= 200 && name != "." && name != ".." && name.back() != '.' &&
           name.back() != ' ' && name.find_first_of("/\\:") == std::string_view::npos &&
           std::none_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

bool IsInnerPath(std::string_view path) {
    if (!IsSafeRelativePath(path) || path.back() == '/') {
        return false;
    }
    std::size_t at = 0;
    while (at < path.size()) {
        const std::size_t end = std::min(path.find('/', at), path.size());
        if (!IsPlainName(path.substr(at, end - at))) {
            return false;
        }
        at = end + 1;
    }
    return true;
}

Result<fs::path> Target(const fs::path& game_dir, std::string_view to) {
    const std::string shown(to);
    if (!IsInnerPath(to)) {
        return Status::Fail(shown + " is not a path inside the game's folder");
    }
    std::error_code ec;
    const fs::path root = fs::canonical(game_dir, ec);
    if (ec || !fs::is_directory(root, ec)) {
        return Status::Fail("the game's folder is missing");
    }
    fs::path at = root;
    std::size_t start = 0;
    while (start < to.size()) {
        const std::size_t end = std::min(to.find('/', start), to.size());
        const bool last = end == to.size();
        at /= PathFromUtf8(to.substr(start, end - start));
        start = end + 1;
        const fs::file_status status = fs::symlink_status(at, ec);
        if (!fs::exists(status)) {
            continue;
        }
        if (fs::is_symlink(status)) {
            return Status::Fail(shown + " passes a link");
        }
        if (last ? !fs::is_regular_file(status) : !fs::is_directory(status)) {
            return Status::Fail(shown + (last ? " is not a file" : " is not inside a folder"));
        }
    }
    // Whatever else resolves elsewhere (a junction, a mount) is caught here.
    const fs::path real = fs::weakly_canonical(at, ec);
    const fs::path relative = real.lexically_relative(root);
    if (ec || relative.empty() || *relative.begin() == "..") {
        return Status::Fail(shown + " leaves the game's folder");
    }
    return at;
}

Status CheckNames(const std::vector<std::string>& names) {
    if (names.empty() || names.size() > MAX_NAMES) {
        return Status::Fail("find takes 1 to 16 file names");
    }
    for (const std::string& name : names) {
        const bool plain = !name.empty() && name.size() <= 120 && name.find_first_of("/\\:") == std::string::npos &&
                           name != "." && name != "..";
        if (!plain || std::count(name.begin(), name.end(), '*') > 1) {
            return Status::Fail("a name is a file name with at most one *");
        }
    }
    return {};
}

std::vector<Found> Find(const fs::path& dir, const std::vector<std::string>& names, const std::atomic<bool>* cancel) {
    Search search;
    search.cancel = cancel;
    for (const std::string& name : names) {
        const std::string lower = ToLower(name);
        const std::size_t star = lower.find('*');
        Pattern pattern;
        pattern.star = star != std::string::npos;
        pattern.before = lower.substr(0, star);
        pattern.after = pattern.star ? lower.substr(star + 1) : std::string();
        search.patterns.push_back(std::move(pattern));
    }
    std::error_code ec;
    if (!search.patterns.empty() && fs::is_directory(fs::symlink_status(dir, ec))) {
        search.Walk(dir, {}, 0);
    }
    return std::move(search.found);
}

} // namespace den::plugins::feature::mods
