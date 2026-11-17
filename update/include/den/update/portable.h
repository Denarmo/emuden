// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Updating the portable Windows folder. The release's zip is unpacked into a staging folder
// beside the install; a copy of the running emuden.exe, started with HELPER_FLAG, waits for
// the app to end, moves each new file and folder in (the old ones to ".update/previous"),
// puts everything back when a move fails, and starts EmuDEN again. The "user" folder (the
// portable data) is never touched.

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "den/base/jobs.h"
#include "den/base/result.h"

namespace den::update {

inline constexpr const char* HELPER_FLAG = "--emuden-apply-update";

/// The folders of an update under the install folder.
struct PortableLayout {
    std::filesystem::path install;  // emuden.exe's folder
    std::filesystem::path work;     // .update
    std::filesystem::path download; // .update/download
    std::filesystem::path staged;   // .update/staged: the new version, unpacked
    std::filesystem::path previous; // .update/previous: the version before the last update
    std::filesystem::path helper;   // .update/helper: the copy of emuden.exe that swaps
    std::filesystem::path result;   // .update/result.txt: what the helper did
    std::filesystem::path log;      // .update/update-log.txt
};
PortableLayout LayoutFor(const std::filesystem::path& install);

/// Unpacks the release's zip into `staged` (emptied first). The zip's files sit at its top
/// (or in one folder at its top); it must hold emuden.exe, and nothing under "user/" is taken.
Status StageZip(const std::filesystem::path& zip, const std::filesystem::path& staged, Job* job = nullptr);

/// Renames `from` to `to`; replaceable in tests.
using Mover = std::function<bool(const std::filesystem::path& from, const std::filesystem::path& to)>;
/// A rename that tries again for a few seconds while a file is held open (a virus scanner).
bool MoveWithRetries(const std::filesystem::path& from, const std::filesystem::path& to);

/// Moves every entry of `staged` (but "user" and ".update") into `install`, the entry it
/// replaces into `previous` (emptied first). When a move fails, every move made is undone in
/// reverse, so the install is as it was. Entries of the install the new version lacks stay.
Status ApplySwap(const std::filesystem::path& install, const std::filesystem::path& staged,
                 const std::filesystem::path& previous, const Mover& move = MoveWithRetries);

/// Copies the running emuden.exe to the helper folder and starts it to apply `version` once
/// this process (`pid`) has ended. The app quits right after.
Status StartHelper(const PortableLayout& layout, const std::filesystem::path& running_exe, int pid,
                   const std::string& version);

/// The helper: when argv holds HELPER_FLAG, applies the staged update and returns the exit
/// code; nullopt otherwise (the app runs as usual).
std::optional<int> RunHelperIfAsked(int argc, char** argv);

struct HelperResult {
    bool ok = false;
    std::string version;
    std::string error;
};
/// What the last helper did, once: the file is removed.
std::optional<HelperResult> TakeHelperResult(const PortableLayout& layout);
/// Removes what an update left that is not needed any more: the helper, staging, the download.
void CleanUpAfterUpdate(const PortableLayout& layout);

} // namespace den::update
