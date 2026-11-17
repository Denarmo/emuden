// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/update/updater.h"

#include "den/base/files.h"
#include "den/base/sha256.h"
#include "den/platform/self_update.h"

namespace den::update {

namespace fs = std::filesystem;

namespace {

pal::HttpResponse Get(const Source& source, pal::HttpRequest request) {
    // Only what GitHub needs: no token, no cookie, nothing about the device.
    request.headers.emplace_back("User-Agent", "EmuDEN-updater");
    return source.http ? source.http(request) : pal::HttpGet(request);
}

std::string HttpError(const pal::HttpResponse& response, const std::string& what) {
    if (response.status == 0) {
        return what + " failed" + (response.error.empty() ? std::string(".") : ": " + response.error);
    }
    return what + " failed (HTTP " + std::to_string(response.status) + ").";
}

bool UrlAllowed(const Source& source, const std::string& url) {
    return url.starts_with("https://") || (source.allow_loopback && IsLoopbackUrl(url));
}

} // namespace

bool IsLoopbackUrl(std::string_view url) {
    std::string_view rest;
    if (url.starts_with("http://")) {
        rest = url.substr(7);
    } else if (url.starts_with("https://")) {
        rest = url.substr(8);
    } else {
        return false;
    }
    std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    if (authority.find('@') != std::string_view::npos || authority.find('\\') != std::string_view::npos) {
        return false; // user info could hide the real host
    }
    std::string_view host;
    if (authority.starts_with("[")) {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        host = authority.substr(0, close + 1);
        authority.remove_prefix(close + 1);
        if (!authority.empty() && authority.front() != ':') {
            return false;
        }
    } else {
        const std::size_t colon = authority.find(':');
        host = authority.substr(0, colon);
        authority = colon == std::string_view::npos ? std::string_view() : authority.substr(colon);
    }
    if (!authority.empty()) { // ":port", digits only
        const std::string_view port = authority.substr(1);
        if (port.empty() || port.find_first_not_of("0123456789") != std::string_view::npos) {
            return false;
        }
    }
    const std::string name = ToLower(host);
    return name == "127.0.0.1" || name == "localhost" || name == "[::1]";
}

TestOverrides AllowedTestOverrides(std::string_view release_version, std::string version, std::string url) {
    if (!release_version.empty()) {
        return {};
    }
    TestOverrides out;
    out.version = std::move(version);
    if (IsLoopbackUrl(url)) {
        out.url = std::move(url);
    }
    return out;
}

Result<CheckResult> FetchLatest(const Source& source, bool beta) {
    pal::HttpRequest request;
    const std::string repo = !source.repo.empty() ? source.repo : UpdateRepo();
    request.url = !source.url.empty() ? source.url : beta ? ListUrl(repo) : LatestUrl(repo);
    request.headers.emplace_back("Accept", "application/vnd.github+json");
    request.headers.emplace_back("X-GitHub-Api-Version", "2022-11-28");
    request.max_body = 4u << 20;
    request.timeout_seconds = 20;
    const pal::HttpResponse response = Get(source, request);
    if (response.status == 404) {
        return Result<CheckResult>::Fail("No release was found on GitHub.");
    }
    if (response.status == 403 || response.status == 429) {
        return Result<CheckResult>::Fail("GitHub asked to wait before checking again.");
    }
    if (!response.ok()) {
        return Result<CheckResult>::Fail(HttpError(response, "Checking for updates"));
    }
    CheckResult result;
    result.body = response.body;
    result.release = PickRelease(response.body, beta);
    if (!result.release && !ParseJson(response.body)) {
        return Result<CheckResult>::Fail("GitHub's answer could not be read.");
    }
    return result;
}

Status DownloadAsset(const Source& source, const Release& release, const std::string& asset_name,
                     const fs::path& file, Job* job) {
    const Asset* asset = release.Find(asset_name);
    const Asset* sums_asset = release.Find(SUMS_NAME);
    if (asset == nullptr) {
        return Status::Fail("EmuDEN " + release.version + " has no " + asset_name + ".");
    }
    if (sums_asset == nullptr) {
        return Status::Fail("EmuDEN " + release.version + " has no checksums. Nothing was downloaded.");
    }
    if (!UrlAllowed(source, asset->url) || !UrlAllowed(source, sums_asset->url)) {
        return Status::Fail("Refused a download that is not https.");
    }
    if (asset->size == 0) {
        return Status::Fail("The release does not say how large " + asset_name + " is.");
    }

    pal::HttpRequest sums_request;
    sums_request.url = sums_asset->url;
    sums_request.max_body = 64u << 10;
    const pal::HttpResponse sums_response = Get(source, sums_request);
    if (!sums_response.ok()) {
        return Status::Fail(HttpError(sums_response, "Downloading the checksums"));
    }
    const auto sums = ParseSums(sums_response.body);
    const auto expected = sums.find(asset_name);
    if (expected == sums.end()) {
        return Status::Fail("SHA256SUMS.txt has no line for " + asset_name + ". Nothing was downloaded.");
    }
    if (!asset->sha256.empty() && asset->sha256 != expected->second) {
        return Status::Fail("GitHub's checksum of " + asset_name + " is not the release's. Nothing was downloaded.");
    }

    std::error_code ec;
    if (IsRegularFile(file) && fs::file_size(file, ec) == asset->size && Sha256File(file) == expected->second) {
        return {}; // downloaded before (the installer was not allowed yet)
    }
    fs::create_directories(file.parent_path(), ec);
    fs::remove(file, ec);
    pal::HttpRequest request;
    request.url = asset->url;
    request.to_file = file;
    request.timeout_seconds = 60;
    bool too_large = false;
    request.progress = [&](std::uint64_t received, std::uint64_t) {
        if (received > asset->size) {
            too_large = true;
            return false;
        }
        if (job != nullptr) {
            job->SetProgress(static_cast<float>(received) / static_cast<float>(asset->size));
        }
        return job == nullptr || !job->CancelRequested();
    };
    const pal::HttpResponse response = Get(source, request);
    const auto fail = [&](std::string message) {
        fs::remove(file, ec);
        fs::path part = file;
        part += ".part";
        fs::remove(part, ec);
        return Status::Fail(std::move(message));
    };
    if (job != nullptr && job->CancelRequested()) {
        return fail("Cancelled.");
    }
    if (too_large) {
        return fail("The download is larger than the release says. Nothing was installed.");
    }
    if (!response.ok()) {
        return fail(HttpError(response, "The download"));
    }
    if (fs::file_size(file, ec) != asset->size || ec) {
        return fail("The download has the wrong size. Nothing was installed.");
    }
    if (Sha256File(file) != expected->second) {
        return fail("The download does not match its checksum. Nothing was installed.");
    }
    return {};
}

Status ReplaceAppImage(const fs::path& downloaded, const fs::path& target) {
    if (downloaded.parent_path() != target.parent_path()) {
        return Status::Fail("The new AppImage must be beside the old one.");
    }
    if (const Status executable = pal::self_update::MakeExecutable(downloaded); !executable) {
        return executable;
    }
    std::error_code ec;
    fs::rename(downloaded, target, ec);
    if (ec) {
        return Status::Fail("The AppImage could not be replaced: " + ec.message());
    }
    return {};
}

} // namespace den::update
