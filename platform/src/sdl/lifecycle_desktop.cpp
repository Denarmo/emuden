// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The desktop: the main window is always there, and the system has no media volume an app
// should move.

#include "../lifecycle_native.h"
#include "den/platform/audio.h"
#include "den/platform/lifecycle.h"

namespace den::pal {

void LifecycleBeforeInit() {}

void LifecycleAfterInit() {}

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

} // namespace audio

} // namespace den::pal
