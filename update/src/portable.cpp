// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/update/portable.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "den/base/files.h"
#include "den/base/zip.h"
#include "den/platform/pal.h"
#include "den/platform/self_update.h"

namespace den::update {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t MAX_FILES = 20000;
constexpr std::uint64_t MAX_BYTES = 4ull << 30;
constexpr int EXIT_WAIT_MS = 60 * 1000;

// Never moved by an update: the portable data, and the update's own folder.
bool Kept(const std::string& name) {
    return EqualsNoCase(name, "user") || EqualsNoCase(name, ".update");
}

// A relative name with forward slashes and no ".", ".." or drive in it.
bool SafeName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t end = name.find('/', start);
        if (end == std::string::npos) {
            end = name.size();
        }
        const std::string part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

// A folder path as the platform gives it may end in a separator.
fs::path Folder(fs::path path) {
    return path.has_filename() ? path : path.parent_path();
}

void Log(const PortableLayout& layout, const std::string& line) {
    std::ofstream out(layout.log, std::ios::app | std::ios::binary);
    out << line << "\n";
}

void WriteResult(const PortableLayout& layout, bool ok, const std::string& version, const std::string& error) {
    WriteFileAtomic(layout.result, std::string(ok ? "ok" : "failed") + "\n" + version + "\n" + error + "\n");
}

} // namespace

PortableLayout LayoutFor(const fs::path& install) {
    PortableLayout layout;
    layout.install = Folder(install);
    layout.work = layout.install / ".update";
    layout.download = layout.work / "download";
    layout.staged = layout.work / "staged";
    layout.previous = layout.work / "previous";
    layout.helper = layout.work / "helper";
    layout.result = layout.work / "result.txt";
    layout.log = layout.work / "update-log.txt";
    return layout;
}

Status StageZip(const fs::path& zip, const fs::path& staged, Job* job) {
    const auto entries = ListZip(zip);
    if (!entries) {
        return Status::Fail("The download is not a zip archive.");
    }
    // Windows PowerShell writes its zips with backslashes.
    std::vector<std::pair<std::string, const ZipEntry*>> files;
    for (const ZipEntry& entry : *entries) {
        std::string name = entry.name;
        for (char& c : name) {
            c = c == '\\' ? '/' : c;
        }
        if (name.ends_with("/")) {
            continue; // a folder
        }
        if (entry.symlink || entry.encrypted || !CanExtractZipEntry(entry) || !SafeName(name)) {
            return Status::Fail("The archive member " + entry.name + " cannot be unpacked.");
        }
        files.emplace_back(std::move(name), &entry);
    }
    // emuden.exe at the top, or in the one folder at the top.
    std::string prefix;
    bool top = false;
    for (const auto& [name, entry] : files) {
        top = top || EqualsNoCase(name, "emuden.exe");
    }
    if (!top) {
        for (const auto& [name, entry] : files) {
            const std::size_t slash = name.find('/');
            if (slash != std::string::npos && EqualsNoCase(name.substr(slash + 1), "emuden.exe")) {
                prefix = name.substr(0, slash + 1);
            }
        }
        if (prefix.empty()) {
            return Status::Fail("The archive holds no emuden.exe.");
        }
    }
    std::uint64_t bytes = 0;
    std::size_t count = 0;
    for (const auto& [name, entry] : files) {
        if (!name.starts_with(prefix)) {
            return Status::Fail("The archive holds more than the program's folder.");
        }
        bytes += entry->size;
        ++count;
    }
    if (count > MAX_FILES || bytes > MAX_BYTES) {
        return Status::Fail("The archive holds more than an EmuDEN release may.");
    }

    std::error_code ec;
    fs::remove_all(staged, ec);
    fs::create_directories(staged, ec);
    if (ec) {
        return Status::Fail("The staging folder could not be made: " + ec.message());
    }
    std::size_t done = 0;
    for (const auto& [name, entry] : files) {
        const std::string relative = name.substr(prefix.size());
        const std::size_t slash = relative.find('/');
        if (Kept(relative.substr(0, slash))) {
            continue;
        }
        const fs::path destination = staged / PathFromUtf8(relative);
        fs::create_directories(destination.parent_path(), ec);
        const Status status = ExtractZipEntry(zip, *entry, destination,
                                              [job] { return job != nullptr && job->CancelRequested(); });
        if (!status) {
            fs::remove_all(staged, ec);
            return status;
        }
        if (job != nullptr) {
            job->SetProgress(static_cast<float>(++done) / static_cast<float>(files.size()));
        }
    }
    return {};
}

bool MoveWithRetries(const fs::path& from, const fs::path& to) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        std::error_code ec;
        fs::rename(from, to, ec);
        if (!ec) {
            return true;
        }
        pal::SleepMs(250);
    }
    return false;
}

Status ApplySwap(const fs::path& install, const fs::path& staged, const fs::path& previous, const Mover& move) {
    std::error_code ec;
    fs::remove_all(previous, ec);
    if (fs::exists(previous)) {
        return Status::Fail("The copy of the version before could not be removed.");
    }
    fs::create_directories(previous, ec);
    std::vector<fs::path> entries;
    for (const fs::path& entry : ListDirectory(staged)) {
        if (!Kept(PathToUtf8(entry.filename()))) {
            entries.push_back(entry.filename());
        }
    }
    if (entries.empty()) {
        return Status::Fail("Nothing was staged.");
    }
    std::sort(entries.begin(), entries.end());
    std::vector<fs::path> set_aside; // old entries now in previous
    std::vector<fs::path> placed;    // new entries now in the install
    const auto undo = [&](const std::string& what) {
        std::string error = what;
        for (auto it = placed.rbegin(); it != placed.rend(); ++it) {
            if (!move(install / *it, staged / *it)) {
                error += " " + PathToUtf8(*it) + " could not be taken out again.";
            }
        }
        for (auto it = set_aside.rbegin(); it != set_aside.rend(); ++it) {
            if (!move(previous / *it, install / *it)) {
                error += " " + PathToUtf8(*it) + " could not be put back; it is in .update/previous.";
            }
        }
        return Status::Fail(error);
    };
    for (const fs::path& name : entries) {
        if (fs::exists(install / name, ec)) {
            if (!move(install / name, previous / name)) {
                return undo(PathToUtf8(name) + " is in use and could not be replaced.");
            }
            set_aside.push_back(name);
        }
        if (!move(staged / name, install / name)) {
            return undo("The new " + PathToUtf8(name) + " could not be moved in.");
        }
        placed.push_back(name);
    }
    return {};
}

Status StartHelper(const PortableLayout& layout, const fs::path& running_exe, int pid, const std::string& version) {
    std::error_code ec;
    fs::remove_all(layout.helper, ec);
    fs::create_directories(layout.helper, ec);
    const fs::path helper = layout.helper / "emuden.exe";
    fs::copy_file(running_exe, helper, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return Status::Fail("The updater could not be prepared: " + ec.message());
    }
    fs::remove(layout.result, ec);
    Log(layout, "Updating to " + version + " once process " + std::to_string(pid) + " has ended");
    return pal::self_update::StartDetached(helper, {HELPER_FLAG, std::to_string(pid), version});
}

std::optional<int> RunHelperIfAsked(int argc, char** argv) {
    if (argc < 4 || std::string(argv[1]) != HELPER_FLAG) {
        return std::nullopt;
    }
    const int pid = std::atoi(argv[2]);
    const std::string version = argv[3];
    // The helper is <install>/.update/helper/emuden.exe; anything else swaps nothing.
    const fs::path helper_dir = Folder(pal::ExecutableDir());
    const fs::path work = helper_dir.parent_path();
    if (!EqualsNoCase(PathToUtf8(helper_dir.filename()), "helper") ||
        !EqualsNoCase(PathToUtf8(work.filename()), ".update")) {
        return 2;
    }
    const PortableLayout layout = LayoutFor(work.parent_path());
    if (!pal::self_update::WaitForExit(pid, EXIT_WAIT_MS)) {
        Log(layout, "EmuDEN did not close; nothing was changed");
        WriteResult(layout, false, version, "EmuDEN did not close, so nothing was changed.");
        return 1;
    }
    Status status;
    if (!IsRegularFile(layout.staged / "emuden.exe")) {
        status = Status::Fail("The staged update is incomplete.");
    } else {
        status = ApplySwap(layout.install, layout.staged, layout.previous);
    }
    std::error_code ec;
    if (status) {
        fs::remove_all(layout.staged, ec);
        fs::remove_all(layout.download, ec);
        Log(layout, "Updated to " + version + "; the version before is in .update/previous");
    } else {
        Log(layout, "Update to " + version + " failed: " + status.error);
    }
    WriteResult(layout, status.ok, version, status.error);
    const Status started = pal::self_update::StartDetached(layout.install / "emuden.exe", {});
    if (!started) {
        Log(layout, started.error);
    }
    return status ? 0 : 1;
}

std::optional<HelperResult> TakeHelperResult(const PortableLayout& layout) {
    const auto text = ReadTextFile(layout.result);
    if (!text) {
        return std::nullopt;
    }
    std::error_code ec;
    fs::remove(layout.result, ec);
    HelperResult result;
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text->size()) {
        const std::size_t end = text->find('\n', start);
        lines.push_back(Trim(std::string_view(*text).substr(start, end == std::string::npos ? end : end - start)));
        start = end == std::string::npos ? text->size() : end + 1;
    }
    result.ok = !lines.empty() && lines[0] == "ok";
    result.version = lines.size() > 1 ? lines[1] : "";
    result.error = lines.size() > 2 ? lines[2] : "";
    return result;
}

void CleanUpAfterUpdate(const PortableLayout& layout) {
    std::error_code ec;
    fs::remove_all(layout.helper, ec);
    fs::remove_all(layout.staged, ec);
    fs::remove_all(layout.download, ec);
}

} // namespace den::update
