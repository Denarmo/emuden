// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Custom shaders: RetroArch shader presets (.slangp, .glslp) imported from a GitHub
// repository or a folder on the device, each into a folder of its own under
// player::ImportedShaderDir(), where the picture presets list them.

#include <algorithm>
#include <cctype>
#include <format>
#include <system_error>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/zip.h"
#include "den/platform/pal.h"
#include "den/player/session.h"

namespace den::app {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t MAX_FILES = 5000;
constexpr std::uint64_t MAX_BYTES = 400ull << 20;

JobPtr g_import;

struct Source {
    std::string url;  // of the zip
    std::string name; // the folder it goes into
};

std::string FolderName(std::string text) {
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

/// "https://github.com/owner/repo", "github.com/owner/repo/tree/branch" or "owner/repo".
std::optional<Source> GithubSource(std::string link) {
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
    const std::string owner = FolderName(parts[0]);
    std::string repo = parts[1];
    if (repo.ends_with(".git")) {
        repo.resize(repo.size() - 4);
    }
    repo = FolderName(repo);
    std::string branch = "HEAD";
    if (parts.size() >= 4 && parts[2] == "tree") {
        branch = parts[3];
    }
    if (owner.empty() || repo.empty() || owner != parts[0]) {
        return std::nullopt;
    }
    return Source{std::format("https://codeload.github.com/{}/{}/zip/{}", owner, repo, branch), repo};
}

bool SafeName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = std::min(name.find('/', start), name.size());
        if (name.compare(start, slash - start, "..") == 0 && slash - start == 2) {
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

/// Moves the unpacked or copied folder into place, over an earlier import of the same name.
Status Install(const fs::path& from, const std::string& name) {
    if (!HasPresets(from)) {
        return Status::Fail("There is no .slangp or .glslp preset in it.");
    }
    std::error_code error;
    const fs::path to = player::ImportedShaderDir() / PathFromUtf8(name);
    fs::remove_all(to, error);
    fs::rename(from, to, error);
    if (error) {
        return Status::Fail("The shaders could not be put in place: " + error.message());
    }
    return Status{};
}

Status Download(const Source& source, Job& job) {
    const fs::path dir = player::ImportedShaderDir();
    const fs::path work = dir / ".import";
    std::error_code error;
    fs::remove_all(work, error);
    fs::create_directories(work, error);
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
    const auto fail = [&](std::string message) {
        fs::remove_all(work, error);
        return Status::Fail(std::move(message));
    };
    if (job.CancelRequested()) {
        return fail("Cancelled.");
    }
    if (!response.ok()) {
        return fail(response.status == 404 ? "GitHub has no such repository (or it is private)."
                                           : "The download failed: " +
                                                 (response.error.empty() ? std::to_string(response.status)
                                                                         : response.error));
    }
    const auto entries = ListZip(archive);
    if (!entries || entries->empty()) {
        return fail("The download is not a zip.");
    }
    job.SetMessage("Unpacking");
    const fs::path out = work / "files";
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    for (const ZipEntry& entry : *entries) {
        if (entry.symlink || entry.encrypted || entry.name.ends_with("/")) {
            continue;
        }
        // GitHub puts everything in one folder "<repo>-<branch>/".
        const std::size_t slash = entry.name.find('/');
        const std::string inner = slash == std::string::npos ? entry.name : entry.name.substr(slash + 1);
        if (inner.empty() || !SafeName(inner) || !CanExtractZipEntry(entry)) {
            continue;
        }
        if (++files > MAX_FILES || (bytes += entry.size) > MAX_BYTES) {
            return fail("The repository is too large to import.");
        }
        const fs::path destination = out / PathFromUtf8(inner);
        fs::create_directories(destination.parent_path(), error);
        if (const Status done = ExtractZipEntry(archive, entry, destination, [&job] { return job.CancelRequested(); });
            !done) {
            return fail(job.CancelRequested() ? "Cancelled." : done.error);
        }
    }
    const Status installed = Install(out, source.name);
    fs::remove_all(work, error);
    return installed;
}

Status CopyFolder(const fs::path& from, const std::string& name, Job& job) {
    const fs::path work = player::ImportedShaderDir() / ".import";
    std::error_code error;
    fs::remove_all(work, error);
    fs::create_directories(work, error);
    job.SetMessage("Copying");
    fs::copy(from, work / "files", fs::copy_options::recursive | fs::copy_options::skip_symlinks, error);
    if (error) {
        fs::remove_all(work, error);
        return Status::Fail("The folder could not be copied: " + error.message());
    }
    const Status installed = Install(work / "files", name);
    fs::remove_all(work, error);
    return installed;
}

void StartImport(std::string name, std::function<Status(Job&)> work) {
    if (g_import && !g_import->Finished()) {
        Svc().stage->Toast("Already importing");
        return;
    }
    g_import = Svc().jobs->Run("Importing shaders", std::move(work), [name](Job& job) {
        if (job.State() == JobState::Done) {
            player::RefreshImportedPresets();
            Svc().stage->Toast("Imported " + name + ". Its presets are under Imported in the picture list.", 5.0f);
        } else if (job.State() == JobState::Failed) {
            Svc().stage->Toast(job.Error(), 6.0f);
        }
        RebuildTop();
    });
    RebuildTop();
}

void ImportFromGithub() {
    ui::TextRequest request;
    request.title = "GitHub repository";
    request.placeholder = "github.com/owner/repository";
    request.max_length = 200;
    Svc().stage->AskText(std::move(request), [](const std::string& text) {
        const auto source = GithubSource(text);
        if (!source) {
            Svc().stage->Toast("That is not a GitHub repository link", 4.0f);
            return;
        }
        StartImport(source->name, [source = *source](Job& job) { return Download(source, job); });
    });
}

void ImportFromFolder() {
    PickOptions options;
    options.title = "Choose a shader folder";
    Pick(std::move(options), [](const std::string& path) {
        const fs::path from = PathFromUtf8(path);
        std::string name = FolderName(PathToUtf8(from.filename()));
        if (name.empty()) {
            name = "shaders";
        }
        StartImport(name, [from, name](Job& job) { return CopyFolder(from, name, job); });
    });
}

std::vector<ui::RowSpec> ShaderRows() {
    std::vector<ui::RowSpec> rows;
    ui::RowSpec github = ValueRow(
        "shaders.github", "Import from GitHub", [] { return JobText(g_import, ""); }, ImportFromGithub);
    github.description = "A repository of RetroArch shaders (.slangp or .glslp), by its link";
    rows.push_back(std::move(github));
    rows.push_back(ActionRow("shaders.folder", "Import a folder", ImportFromFolder,
                             "RetroArch shaders already on this device"));

    std::vector<fs::path> folders;
    std::error_code error;
    for (fs::directory_iterator it(player::ImportedShaderDir(), error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_directory(error) && PathToUtf8(it->path().filename()) != ".import") {
            folders.push_back(it->path());
        }
    }
    std::sort(folders.begin(), folders.end());
    int index = 0;
    for (const fs::path& folder : folders) {
        const std::string name = PathToUtf8(folder.filename());
        rows.push_back(ValueRow(
            std::format("shaders.imported.{}", index++), name, [] { return std::string("Remove"); },
            [folder, name] {
                Svc().stage->Confirm("Remove " + name + "?", "Its presets leave the picture list.", "Remove",
                                     [folder] {
                                         std::error_code ignored;
                                         fs::remove_all(folder, ignored);
                                         player::RefreshImportedPresets();
                                         RebuildTop();
                                     });
            }));
    }
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(26, "Custom shaders", ShaderRows);

} // namespace den::app
