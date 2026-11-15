// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The folder store: the remote side as plain files in a folder (Syncthing replicates it).
// A file's rev is its size and time; a write over a file that changed since is refused.

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/sync/plan.h"
#include "den/sync/provider.h"

namespace den::sync {

namespace {

namespace fs = std::filesystem;

// Files other programs keep in a synced folder while they work, and our own temporary ones.
bool Ignored(const std::string& name) {
    return name.starts_with(".") || name.starts_with("~syncthing~") || name.ends_with(".tmp") ||
           name.ends_with(".sync-part") || name.ends_with(".part");
}

std::string RevOf(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    const auto time = fs::last_write_time(file, ec).time_since_epoch().count();
    return std::to_string(size) + "-" + std::to_string(static_cast<long long>(time));
}

class FolderStore final : public Provider {
public:
    FolderStore(std::string id, fs::path root) : id_(std::move(id)), root_(std::move(root)) {}

    std::string Id() const override {
        return id_;
    }

    Result<std::vector<RemoteFile>> List(const std::string& prefix) override {
        std::error_code ec;
        if (!fs::is_directory(root_, ec)) {
            return Status::Fail("the sync folder " + PathToUtf8(root_) + " is not there");
        }
        std::vector<RemoteFile> out;
        const fs::path start = prefix.empty() ? root_ : root_ / PathFromUtf8(prefix);
        for (fs::recursive_directory_iterator it(start, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::string name = PathToUtf8(it->path().filename());
            if (Ignored(name)) {
                if (it->is_directory(ec)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            std::string path = PathToUtf8(it->path().lexically_relative(root_));
            std::replace(path.begin(), path.end(), '\\', '/');
            if (!IsPortablePath(path)) {
                continue;
            }
            RemoteFile file;
            file.path = std::move(path);
            file.size = it->file_size(ec);
            file.modified = FileTime(it->path());
            file.rev = RevOf(it->path());
            out.push_back(std::move(file));
        }
        return out;
    }

    Status Get(const std::string& path, const fs::path& local, const Progress&) override {
        std::error_code ec;
        fs::copy_file(Remote(path), local, fs::copy_options::overwrite_existing, ec);
        return ec ? Status::Fail("could not copy " + path + ": " + ec.message()) : Status();
    }

    Result<std::string> Put(const std::string& path, const fs::path& local, const std::string& rev,
                            std::int64_t modified) override {
        const fs::path target = Remote(path);
        std::error_code ec;
        if (!rev.empty() && fs::is_regular_file(target, ec) && RevOf(target) != rev) {
            return Status::Fail("conflict");
        }
        fs::create_directories(target.parent_path(), ec);
        fs::path part = target;
        part += ".sync-part";
        fs::copy_file(local, part, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            SetFileTime(part, modified);
            fs::rename(part, target, ec);
        }
        if (ec) {
            fs::remove(part, ec);
            return Status::Fail("could not write " + path + " to the sync folder");
        }
        return RevOf(target);
    }

    Status Delete(const std::string& path) override {
        std::error_code ec;
        fs::remove(Remote(path), ec);
        return ec ? Status::Fail("could not delete " + path) : Status();
    }

    JsonValue Info() override {
        std::error_code ec;
        const fs::space_info space = fs::space(root_, ec);
        JsonValue info = JsonObject({{"folder", JsonString(PathToUtf8(root_))}});
        if (!ec) {
            info.members.emplace_back("free", JsonNumber(static_cast<double>(space.available)));
        }
        return info;
    }

private:
    fs::path Remote(const std::string& path) const {
        return root_ / PathFromUtf8(path);
    }

    std::string id_;
    fs::path root_;
};

} // namespace

std::unique_ptr<Provider> CreateFolderStore(std::string id, fs::path root) {
    return std::make_unique<FolderStore>(std::move(id), std::move(root));
}

} // namespace den::sync
