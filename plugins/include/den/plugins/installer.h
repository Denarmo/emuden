// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Downloads with a pinned size and SHA-256, the ledger of installed cores, and the host's
// replaceable dependencies.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/platform/pal.h"
#include "den/plugins/host.h"

namespace den::plugins {

using HttpGetter = std::function<pal::HttpResponse(const pal::HttpRequest&)>;

/// Downloads `url` (https only) to `destination`. The download is stopped when it grows past
/// `size`, and the file is kept only when its size and SHA-256 are the stated ones.
/// `progress` gets 0..1; the job's cancel is honoured.
Status DownloadPinned(const HttpGetter& http, const std::string& url, std::string_view sha256,
                      std::uint64_t size, const std::filesystem::path& destination,
                      Job* job = nullptr, const std::function<void(float)>& progress = {});

/// Unpacks the one member `entry` of a zip to `destination`. Links and encrypted members are
/// refused, and a member larger than `max_size` (0 = any).
Status UnpackEntry(const std::filesystem::path& archive, std::string_view entry,
                   const std::filesystem::path& destination, std::uint64_t max_size = 0,
                   Job* job = nullptr);

/// Unpacks every member under the folder `prefix` ("Name/") of a zip into `root`, keeping
/// the paths (root/Name/...). Fails without a member, and for links, encrypted members, unsafe
/// names, or more than `max_files` files or `max_bytes` bytes.
Status UnpackFolder(const std::filesystem::path& archive, std::string_view prefix,
                    const std::filesystem::path& root, std::size_t max_files,
                    std::uint64_t max_bytes, Job* job = nullptr);

struct CoreRecord {
    std::string file; // the library's name in the cores folder
    std::string file_sha256;
    std::string download_sha256;
    std::string url;
    std::string version;
    std::string origin; // "official" (manifest from the signed index), "bundled", or an added source's address
    std::vector<std::string> owners; // the plugins it was installed for
};

/// cores/cores.json: what each installed core is, where it came from and its checksum.
class CoreLedger {
public:
    explicit CoreLedger(std::filesystem::path cores_dir);

    const std::filesystem::path& Directory() const {
        return cores_dir_;
    }
    const std::map<std::string, CoreRecord, std::less<>>& Records() const {
        return records_;
    }
    const CoreRecord* Find(std::string_view core_id) const;
    const CoreRecord* FindByFile(std::string_view file_name) const;

    /// Records a core file that is in the cores folder, hashing it.
    Status Record(std::string_view core_id, CoreRecord record);
    void AddOwner(std::string_view core_id, std::string_view plugin_id);
    /// Forgets the core and deletes its file. Fails when the file is in use.
    Status Remove(std::string_view core_id);
    /// Forgets the core and leaves its file (another core's took its place).
    void Forget(std::string_view core_id);

    /// Hashes the whole file and compares it with the ledger. `blocked` gives the reason
    /// when a checksum must not be loaded (empty = fine).
    Status VerifyForLoad(
        const std::filesystem::path& core_file,
        const std::function<std::string(std::string_view sha256)>& blocked = {}) const;

    bool Save() const;

private:
    std::filesystem::path cores_dir_;
    std::map<std::string, CoreRecord, std::less<>> records_;
};

/// What the host needs from outside, replaceable in tests.
struct HostDeps {
    HttpGetter http;                   // default: pal::HttpGet
    std::string source;                // default: OFFICIAL_SOURCE
    std::vector<std::string> keys;     // default: OfficialKeys()
    std::string platform_key;          // default: pal::GetCaps().platform_key
    std::function<std::int64_t()> now; // Unix seconds; default: the system clock
    bool bundled = true;               // false: start without the compiled-in manifests
};

std::unique_ptr<Host> CreateHostWith(const Config& config, HostDeps deps);

} // namespace den::plugins
