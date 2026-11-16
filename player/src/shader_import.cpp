// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/player/shader_import.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <system_error>
#include <vector>

#include "den/base/files.h"
#include "den/base/names.h"
#include "den/base/zip.h"
#include "den/platform/pal.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t MAX_FILES = 5000;
constexpr std::uint64_t MAX_BYTES = 400ull << 20;
constexpr std::string_view WORK = ".import";

// A relative path with '/' between its parts that stays inside the folder it is unpacked to.
bool StaysInside(std::string_view name) {
    if (name.empty() || name.front() == '/' ||
        std::any_of(name.begin(), name.end(), [](char c) { return IsRefusedNameChar(c, /*allow_slash=*/true); })) {
        return false;
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = std::min(name.find('/', start), name.size());
        if (name.substr(start, slash - start) == "..") {
            return false;
        }
        start = slash + 1;
    }
    return true;
}

bool HasPresets(const fs::path& folder) {
    std::error_code error;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        const std::string ext = PathToUtf8(it->path().extension());
        if (ext == ".slangp" || ext == ".glslp") {
            return true;
        }
    }
    return false;
}

fs::path FreshWorkDir(const fs::path& dir) {
    const fs::path work = dir / PathFromUtf8(WORK);
    std::error_code error;
    fs::remove_all(work, error);
    fs::create_directories(work, error);
    return work;
}

} // namespace

std::string ShaderFolderName(std::string_view text) {
    std::string name;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
            name += c;
        }
    }
    while (!name.empty() && name.front() == '.') {
        name.erase(name.begin());
    }
    return name.substr(0, 60);
}

std::optional<ShaderSource> GithubShaderSource(std::string link) {
    // Phone keyboards like to write "GitHub.com".
    for (const std::string_view prefix : {"https://", "http://", "www.", "github.com/"}) {
        const bool starts = link.size() >= prefix.size() &&
                            std::equal(prefix.begin(), prefix.end(), link.begin(), [](char a, char b) {
                                return a == std::tolower(static_cast<unsigned char>(b));
                            });
        if (starts) {
            link.erase(0, prefix.size());
        }
    }
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= link.size()) {
        const std::size_t slash = std::min(link.find('/', start), link.size());
        if (slash > start) {
            parts.push_back(link.substr(start, slash - start));
        }
        start = slash + 1;
    }
    if (parts.size() < 2) {
        return std::nullopt;
    }
    const std::string owner = ShaderFolderName(parts[0]);
    std::string repo = parts[1];
    if (repo.ends_with(".git")) {
        repo.resize(repo.size() - 4);
    }
    repo = ShaderFolderName(repo);
    std::string branch = "HEAD";
    if (parts.size() >= 4 && parts[2] == "tree") {
        branch = parts[3];
    }
    if (owner.empty() || repo.empty() || owner != parts[0]) {
        return std::nullopt;
    }
    return ShaderSource{std::format("https://codeload.github.com/{}/{}/zip/{}", owner, repo, branch), repo};
}

Status UnpackShaderZip(const fs::path& archive, const fs::path& out, const std::function<bool()>& cancelled) {
    const auto entries = ListZip(archive);
    if (!entries || entries->empty()) {
        return Status::Fail("The download is not a zip.");
    }
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    std::error_code error;
    for (const ZipEntry& entry : *entries) {
        if (entry.symlink || entry.encrypted || entry.name.ends_with("/")) {
            continue;
        }
        const std::size_t slash = entry.name.find('/');
        const std::string inner = slash == std::string::npos ? entry.name : entry.name.substr(slash + 1);
        if (!StaysInside(inner) || !CanExtractZipEntry(entry)) {
            continue;
        }
        if (++files > MAX_FILES || (bytes += entry.size) > MAX_BYTES) {
            return Status::Fail("The repository is too large to import.");
        }
        const fs::path destination = out / PathFromUtf8(inner);
        fs::create_directories(destination.parent_path(), error);
        if (Status done = ExtractZipEntry(archive, entry, destination, cancelled); !done) {
            return cancelled() ? Status::Fail("Cancelled.") : done;
        }
    }
    return Status{};
}

Status InstallShaderFolder(const fs::path& from, const fs::path& dir, const std::string& name) {
    if (!HasPresets(from)) {
        return Status::Fail("There is no .slangp or .glslp preset in it.");
    }
    std::error_code error;
    const fs::path to = dir / PathFromUtf8(name);
    fs::remove_all(to, error);
    fs::rename(from, to, error);
    if (error) {
        return Status::Fail("The shaders could not be put in place: " + error.message());
    }
    return Status{};
}

Status DownloadShaderPack(const ShaderSource& source, const fs::path& dir, Job& job) {
    const fs::path work = FreshWorkDir(dir);
    const fs::path archive = work / "download.zip";
    job.SetMessage("Downloading");
    pal::HttpRequest request;
    request.url = source.url;
    request.to_file = archive;
    request.timeout_seconds = 120;
    request.progress = [&job](std::uint64_t received, std::uint64_t total) {
        if (total > 0) {
            job.SetProgress(static_cast<float>(received) / static_cast<float>(total));
        }
        return received <= MAX_BYTES && !job.CancelRequested();
    };
    const pal::HttpResponse response = pal::HttpGet(request);
    Status result;
    if (job.CancelRequested()) {
        result = Status::Fail("Cancelled.");
    } else if (!response.ok()) {
        result = Status::Fail(response.status == 404
                                  ? "GitHub has no such repository (or it is private)."
                                  : "The download failed: " + (response.error.empty() ? std::to_string(response.status)
                                                                                     : response.error));
    } else {
        job.SetMessage("Unpacking");
        result = UnpackShaderZip(archive, work / "files", [&job] { return job.CancelRequested(); });
        if (result) {
            result = InstallShaderFolder(work / "files", dir, source.name);
        }
    }
    std::error_code error;
    fs::remove_all(work, error);
    return result;
}

Status CopyShaderFolder(const fs::path& from, const std::string& name, const fs::path& dir, Job& job) {
    const fs::path work = FreshWorkDir(dir);
    job.SetMessage("Copying");
    std::error_code error;
    fs::copy(from, work / "files", fs::copy_options::recursive | fs::copy_options::skip_symlinks, error);
    Status result = error ? Status::Fail("The folder could not be copied: " + error.message())
                          : InstallShaderFolder(work / "files", dir, name);
    fs::remove_all(work, error);
    return result;
}

} // namespace den::player
