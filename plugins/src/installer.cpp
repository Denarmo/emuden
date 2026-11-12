// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/sha256.h"
#include "den/base/zip.h"
#include "den/plugins/installer.h"
#include "den/plugins/manifest.h"
#include "json_util.h"

namespace den::plugins {

namespace fs = std::filesystem;

Status DownloadPinned(const HttpGetter& http, const std::string& url, std::string_view sha256,
                      std::uint64_t size, const fs::path& destination, Job* job,
                      const std::function<void(float)>& progress) {
    if (!url.starts_with("https://")) {
        return Status::Fail("Refused a download that is not https.");
    }
    if (!IsSha256Hex(sha256) || size == 0) {
        return Status::Fail("Refused a download without a checksum.");
    }
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    fs::remove(destination, ec);

    pal::HttpRequest request;
    request.url = url;
    request.to_file = destination;
    request.timeout_seconds = 60;
    bool too_large = false;
    request.progress = [&](std::uint64_t received, std::uint64_t) {
        if (received > size) {
            too_large = true;
            return false;
        }
        if (progress) {
            progress(static_cast<float>(received) / static_cast<float>(size));
        }
        return job == nullptr || !job->CancelRequested();
    };
    const pal::HttpResponse response = http(request);
    const auto fail = [&](std::string message) {
        fs::remove(destination, ec);
        fs::path part = destination;
        part += ".part";
        fs::remove(part, ec);
        return Status::Fail(std::move(message));
    };
    if (job != nullptr && job->CancelRequested()) {
        return fail("Cancelled.");
    }
    if (too_large) {
        return fail("The download is larger than its source said. Nothing was installed.");
    }
    if (!response.ok()) {
        return fail("The download failed" +
                    (response.status != 0 ? " (HTTP " + std::to_string(response.status) + ")"
                                          : std::string()) +
                    (response.error.empty() ? std::string(".") : ": " + response.error));
    }
    const std::uintmax_t on_disk = fs::file_size(destination, ec);
    if (ec || on_disk != size) {
        return fail("The download has the wrong size. Nothing was installed.");
    }
    const auto hash = Sha256File(destination);
    if (!hash || *hash != sha256) {
        return fail("The download does not match its checksum. Nothing was installed.");
    }
    return {};
}

Status UnpackEntry(const fs::path& archive, std::string_view entry, const fs::path& destination,
                   std::uint64_t max_size, Job* job) {
    const auto entries = ListZip(archive);
    if (!entries) {
        return Status::Fail("The download is not a zip archive.");
    }
    const auto it = std::find_if(entries->begin(), entries->end(),
                                 [&](const ZipEntry& e) { return e.name == entry; });
    if (it == entries->end()) {
        return Status::Fail("The archive does not hold " + std::string(entry) + ".");
    }
    if (it->symlink || it->encrypted || !CanExtractZipEntry(*it)) {
        return Status::Fail("The archive member " + std::string(entry) + " cannot be unpacked.");
    }
    if (max_size != 0 && it->size > max_size) {
        return Status::Fail("The archive member " + std::string(entry) + " is too large.");
    }
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    return ExtractZipEntry(archive, *it, destination,
                           [job] { return job != nullptr && job->CancelRequested(); });
}

CoreLedger::CoreLedger(fs::path cores_dir) : cores_dir_(std::move(cores_dir)) {
    const auto text = ReadTextFile(cores_dir_ / "cores.json");
    const auto root = text ? ParseJson(*text) : std::nullopt;
    const JsonValue* cores = root ? root->Find("cores") : nullptr;
    if (cores == nullptr) {
        return;
    }
    for (const auto& [id, value] : cores->members) {
        CoreRecord record;
        record.file = value.String("file");
        record.file_sha256 = value.String("file_sha256");
        record.download_sha256 = value.String("download_sha256");
        record.url = value.String("url");
        record.version = value.String("version");
        record.origin = value.String("origin");
        record.owners = json::Strings(value, "owners", false);
        // The file name is joined to the cores folder, so it must be a bare name.
        if (!record.file.empty() && record.file.find_first_of("/\\:") == std::string::npos &&
            IsSha256Hex(record.file_sha256)) {
            records_.emplace(id, std::move(record));
        }
    }
}

const CoreRecord* CoreLedger::Find(std::string_view core_id) const {
    const auto it = records_.find(core_id);
    return it == records_.end() ? nullptr : &it->second;
}

const CoreRecord* CoreLedger::FindByFile(std::string_view file_name) const {
    for (const auto& [id, record] : records_) {
        if (EqualsNoCase(record.file, file_name)) {
            return &record;
        }
    }
    return nullptr;
}

Status CoreLedger::Record(std::string_view core_id, CoreRecord record) {
    const auto hash = Sha256File(cores_dir_ / PathFromUtf8(record.file));
    if (!hash) {
        return Status::Fail("The core file " + record.file + " cannot be read.");
    }
    record.file_sha256 = *hash;
    records_[std::string(core_id)] = std::move(record);
    return {};
}

void CoreLedger::AddOwner(std::string_view core_id, std::string_view plugin_id) {
    const auto it = records_.find(core_id);
    if (it != records_.end() && std::find(it->second.owners.begin(), it->second.owners.end(),
                                          plugin_id) == it->second.owners.end()) {
        it->second.owners.emplace_back(plugin_id);
    }
}

Status CoreLedger::Remove(std::string_view core_id) {
    const auto it = records_.find(core_id);
    if (it == records_.end()) {
        return {};
    }
    const fs::path file = cores_dir_ / PathFromUtf8(it->second.file);
    std::error_code ec;
    fs::remove(file, ec);
    if (fs::exists(file, ec)) {
        return Status::Fail("The core is in use. Close the game and try again.");
    }
    records_.erase(it);
    return {};
}

Status CoreLedger::VerifyForLoad(
    const fs::path& core_file,
    const std::function<std::string(std::string_view sha256)>& blocked) const {
    const CoreRecord* record = FindByFile(PathToUtf8(core_file.filename()));
    std::error_code ec;
    if (record == nullptr || !fs::equivalent(core_file, cores_dir_ / PathFromUtf8(record->file), ec)) {
        return Status::Fail("This core was not installed by EmuDEN.");
    }
    const auto hash = Sha256File(core_file);
    if (!hash) {
        return Status::Fail("The core file cannot be read.");
    }
    if (*hash != record->file_sha256) {
        return Status::Fail("The core file changed since it was installed. Install it again.");
    }
    if (blocked) {
        for (const std::string_view sum : {std::string_view(*hash),
                                           std::string_view(record->download_sha256)}) {
            const std::string reason = sum.empty() ? std::string() : blocked(sum);
            if (!reason.empty()) {
                return Status::Fail("This core must not be used: " + reason);
            }
        }
    }
    return {};
}

bool CoreLedger::Save() const {
    std::vector<std::pair<std::string, JsonValue>> cores;
    for (const auto& [id, record] : records_) {
        cores.emplace_back(id, JsonObject({
                                   {"file", JsonString(record.file)},
                                   {"file_sha256", JsonString(record.file_sha256)},
                                   {"download_sha256", JsonString(record.download_sha256)},
                                   {"url", JsonString(record.url)},
                                   {"version", JsonString(record.version)},
                                   {"origin", JsonString(record.origin)},
                                   {"owners", json::StringArray(record.owners)},
                               }));
    }
    std::error_code ec;
    fs::create_directories(cores_dir_, ec);
    return WriteFileAtomic(cores_dir_ / "cores.json",
                           WriteJson(JsonObject({{"cores", JsonObject(std::move(cores))}})));
}

} // namespace den::plugins
