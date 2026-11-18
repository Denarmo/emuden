// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The desktop: the main window is always there, and the system has no media volume an app
// should move.

#include "../lifecycle_native.h"
#include "den/platform/audio.h"
#include "den/platform/lifecycle.h"

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace den::pal {

#ifdef __linux__
namespace {

// Steam's "Exit game" ends every process of the game at once, the AppImage's FUSE mount among
// them, while EmuDEN still shuts down: code of the executable not read from the mount yet
// then faults with SIGBUS. Its pages are read in and locked while the mount is there; past the
// lock limit they are at least read in, which keeps them for as long as memory allows.
void KeepImageResident() {
    const char* appdir = std::getenv("APPDIR");
    if (appdir == nullptr || *appdir == '\0') {
        return;
    }
    const std::string root = std::string(appdir) + "/";
    const long page = sysconf(_SC_PAGESIZE);
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        std::istringstream in(line);
        std::string range, perms, offset, device, inode, path;
        in >> range >> perms >> offset >> device >> inode;
        std::getline(in >> std::ws, path);
        const std::size_t dash = range.find('-');
        if (!path.starts_with(root) || perms.empty() || perms[0] != 'r' || dash == std::string::npos) {
            continue;
        }
        const std::uintptr_t start = std::strtoull(range.c_str(), nullptr, 16);
        const std::uintptr_t end = std::strtoull(range.c_str() + dash + 1, nullptr, 16);
        if (end <= start || page <= 0) {
            continue;
        }
        if (mlock(reinterpret_cast<void*>(start), end - start) == 0) {
            continue;
        }
        madvise(reinterpret_cast<void*>(start), end - start, MADV_WILLNEED);
        volatile unsigned char sink = 0;
        for (std::uintptr_t at = start; at < end; at += static_cast<std::uintptr_t>(page)) {
            sink = sink + *reinterpret_cast<const volatile unsigned char*>(at);
            // Locked page by page up to the limit, so most of it survives memory pressure too.
            mlock(reinterpret_cast<void*>(at), static_cast<std::size_t>(page));
        }
    }
}

} // namespace
#endif

void LifecycleBeforeInit() {
#ifdef __linux__
    KeepImageResident();
#endif
}

void LifecycleAfterInit() {}

SafeInsets PlatformSafeInsets() {
    return {};
}

void SetPlatformOrientation(Orientation) {}

void HapticTick() {}

bool MainWindowHidden() {
    return false;
}

bool PrepareFrame() {
    return true;
}

bool EventPumpHeld() {
    return false;
}

namespace audio {

bool HasSystemVolume() {
    return false;
}

int SystemVolumeSteps() {
    return 0;
}

int SystemVolume() {
    return 0;
}

void SetSystemVolume(int) {}

bool HasScreenControls() {
    return false;
}

void OpenScreenControls() {}

void HoldGameFocus(bool) {}

} // namespace audio

} // namespace den::pal
