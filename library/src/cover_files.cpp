// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>

#include "cover_files.h"
#include "den/base/files.h"
#include "den/platform/pal.h"
#include "den/store/settings.h"

namespace den::library {

namespace fs = std::filesystem;

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// When the file was last written, as unix seconds; 0 when it cannot be told.
std::int64_t WrittenUnix(const fs::path& file) {
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    if (ec) {
        return 0;
    }
    const auto age = std::chrono::duration_cast<std::chrono::seconds>(
        fs::file_time_type::clock::now() - written);
    return UnixNow() - age.count();
}

HttpAnswer PlatformGetWithHeaders(const std::string& url, const HttpHeaders& headers) {
    pal::HttpRequest request;
    request.url = url;
    request.headers = headers;
    request.timeout_seconds = 20;
    request.max_body = 16u << 20;
    pal::HttpResponse response = pal::HttpGet(request);
    return {response.error.empty() ? response.status : 0, std::move(response.body)};
}

HttpAnswer PlatformGet(const std::string& url) {
    return PlatformGetWithHeaders(url, {});
}

HackCoverOptions ReadHackOptions(const store::Settings* settings) {
    HackCoverOptions options;
    options.collections = DefaultHackCollections();
    if (settings != nullptr) {
        options.online = settings->GetBool("library.hack_covers.online", true);
        if (settings->Has("library.hack_covers.collections")) {
            options.collections = settings->GetList("library.hack_covers.collections");
        }
        options.retroachievements_key = settings->GetString("library.hack_covers.retroachievements_key");
        options.steamgriddb_key = settings->GetString("library.hack_covers.steamgriddb_key");
    }
    return options;
}

fs::path OwnCoverStem(const fs::path& library_dir, const Entry& entry) {
    return library_dir / "covers" / PathFromUtf8(entry.system) /
           PathFromUtf8(entry.original_name.empty() ? entry.name : entry.original_name);
}

void RemoveOwnCover(const fs::path& stem) {
    for (const char* extension : {".png", ".jpg", ".jpeg", ".webp"}) {
        fs::path file = stem;
        file += extension;
        std::error_code ec;
        fs::remove(file, ec);
    }
}

Status WriteOwnCover(const fs::path& stem, const std::string& image_path, fs::path& file) {
    const auto data = ReadBinaryFile(PathFromUtf8(image_path));
    const char* extension = nullptr;
    if (data && data->size() > 8 && (*data)[0] == 0x89 && (*data)[1] == 'P' && (*data)[2] == 'N') {
        extension = ".png";
    } else if (data && data->size() > 3 && (*data)[0] == 0xFF && (*data)[1] == 0xD8) {
        extension = ".jpg";
    }
    if (extension == nullptr) {
        return Status::Fail("That is not a PNG or JPEG image");
    }
    RemoveOwnCover(stem);
    file = stem;
    file += extension;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (!WriteFileAtomic(file, std::string_view(reinterpret_cast<const char*>(data->data()), data->size()))) {
        return Status::Fail("Could not write " + PathToUtf8(file));
    }
    return {};
}

} // namespace den::library
