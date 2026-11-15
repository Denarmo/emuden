// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Where the remote side of sync is kept: a plugin's plugin.provider (docs/sync.md, section 5)
// or the host's folder store (plain files in a folder another program replicates).

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "den/base/json.h"
#include "den/base/result.h"

namespace den::plugins {
class FeatureHost;
}

namespace den::sync {

struct RemoteFile {
    std::string path;            // portable name
    std::uint64_t size = 0;
    std::int64_t modified = 0;   // Unix seconds, the file's own time as the provider keeps it
    std::string rev;             // changes whenever the file does
};

/// done and total bytes; total 0 when unknown.
using Progress = std::function<void(double done, double total)>;

/// One provider. Calls block; the engine makes them from its job, one at a time.
class Provider {
public:
    virtual ~Provider() = default;
    virtual std::string Id() const = 0;
    /// The remote files under `prefix` (a category folder, "saves/"); "" lists everything.
    virtual Result<std::vector<RemoteFile>> List(const std::string& prefix) = 0;
    /// Writes the remote file into `local` (a temporary file the engine made).
    virtual Status Get(const std::string& path, const std::filesystem::path& local, const Progress& progress) = 0;
    /// Uploads `local` as `path` and returns the new rev. `rev` is the one the journal knows;
    /// a provider that refuses to write over a newer version fails with the error "conflict".
    virtual Result<std::string> Put(const std::string& path, const std::filesystem::path& local,
                                    const std::string& rev, std::int64_t modified) = 0;
    virtual Status Delete(const std::string& path) = 0;
    /// Free space, who is signed in: whatever the provider says, for the sync page.
    virtual JsonValue Info() {
        return JsonObject();
    }
};

/// The folder store: the remote side as plain files under `root`.
std::unique_ptr<Provider> CreateFolderStore(std::string id, std::filesystem::path root);

/// A plugin's plugin.provider, called on the plugin's script. `cancel`, when set, stops a
/// call that waits. Refuses every file of a `pc` system, and the Google Drive provider every
/// file that names one.
std::unique_ptr<Provider> CreatePluginProvider(plugins::FeatureHost* host, std::string id,
                                               std::set<std::string> pc, const std::atomic<bool>* cancel);

} // namespace den::sync
