// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the app updater needs from the operating system: how this copy of EmuDEN was
// installed, and the steps that replace it (den/update does the rest).

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::pal::self_update {

enum class Method {
    ReleasePage,     // cannot replace itself (a build folder, a read-only folder): a link only
    WindowsPortable, // the folder of the portable zip; a helper swaps the files after exit
    AppImage,        // a Linux AppImage file, replaced by a rename
    AndroidApk,      // the system's package installer, which asks the user
};

struct Target {
    Method method = Method::ReleasePage;
    /// WindowsPortable: the folder emuden.exe is in. AppImage: the AppImage file.
    std::filesystem::path location;
    /// ReleasePage: why the app cannot update itself here.
    std::string reason;
};

/// How this copy can update itself. Looks at files only; safe on any thread.
Target Detect();

/// This process' id.
int ProcessId();
/// Waits until the process `pid` has ended. False when it still runs after `timeout_ms`.
bool WaitForExit(int pid, int timeout_ms);

/// Starts a program that outlives the app (it is not ended with it).
Status StartDetached(const std::filesystem::path& executable, const std::vector<std::string>& args);
/// Starts `executable` once this process has ended (the restart after an AppImage update).
Status StartAfterExit(const std::filesystem::path& executable);
/// Marks a file executable by its owner and everyone who may read it (chmod +x).
Status MakeExecutable(const std::filesystem::path& file);

/// Android: hands a downloaded APK to the system's package installer, which asks the user.
/// When the app may not install apps yet, opens the "install unknown apps" setting for it and
/// fails with what to do next. Elsewhere: fails.
Status InstallPackage(const std::filesystem::path& apk);

} // namespace den::pal::self_update
