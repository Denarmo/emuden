// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The plugin's folder as its scripts name it, files the host lends to a provider call, the
// storage quota, and the server the user confirmed.

#include <system_error>

#include "den/base/files.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace fs = std::filesystem;

Status Runner::Resolve(std::string_view script_path, bool package, fs::path& out) const {
    fs::path root = config_.data_dir;
    if (script_path == "package" || script_path.starts_with("package/")) {
        if (!package) {
            return Status::Fail("the package cannot be changed");
        }
        root = config_.package_dir;
        script_path.remove_prefix(script_path.size() > 7 ? 8 : 7);
    }
    if (script_path == ".") {
        script_path = {};
    }
    if (!script_path.empty() && !IsSafeRelativePath(script_path)) {
        return Status::Fail("the path leaves the plugin's folder");
    }
    out = script_path.empty() ? root : root / PathFromUtf8(script_path);
    // A link inside the folder must not lead out of it either.
    std::error_code ec;
    const fs::path real_root = fs::weakly_canonical(root, ec);
    const fs::path real = fs::weakly_canonical(out, ec);
    const fs::path relative = real.lexically_relative(real_root);
    if (ec || relative.empty() || *relative.begin() == "..") {
        return Status::Fail("the path leaves the plugin's folder");
    }
    return {};
}

std::uint64_t Runner::StorageLeft() const {
    std::uint64_t used = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(config_.data_dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            used += it->file_size(ec);
        }
    }
    const std::uint64_t limit = static_cast<std::uint64_t>(config_.manifest.storage_mb) << 20;
    return used >= limit ? 0 : limit - used;
}

bool Runner::SaveStorage() {
    return WriteFileAtomic(config_.data_dir / "storage.json", CompactJson(storage_));
}

Status Runner::ResolveFile(std::string_view script_path, fs::path& out, bool& lent) const {
    lent = false;
    if (script_path.starts_with("sync:")) {
        const auto file = config_.lent ? config_.lent(std::string(script_path)) : std::nullopt;
        if (!file) {
            return Status::Fail("that file was lent for another call");
        }
        out = *file;
        lent = true;
        return {};
    }
    return Resolve(script_path, false, out);
}

std::string Runner::UserServer() const {
    std::lock_guard lock(mutex_);
    return user_server_;
}

bool Runner::SetUserServer(const std::string& origin) {
    {
        std::lock_guard lock(mutex_);
        user_server_ = origin;
    }
    return WriteFileAtomic(config_.data_dir / "server.json",
                           CompactJson(JsonObject({{"server", JsonString(origin)}})));
}

} // namespace den::plugins::feature
