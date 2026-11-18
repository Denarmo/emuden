// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The app going on while its main window is not shown. On Android another app can cover the
// display the main window is on while a game runs on the other display: the activity is
// stopped and its surface is gone, but the process, the loop, the sound and the second
// display carry on. On the desktop the main window is always there.

#pragma once

namespace den::pal {

/// The main window has no surface to draw on right now. Nothing drawn for it is seen and it
/// must not be swapped; the second display (pal::GetSecondDisplay) may still be drawn on.
bool MainWindowHidden();
/// Call once at the start of every frame, before any drawing. While the main window is
/// hidden it keeps the app's GL context usable without that window. False when there is no
/// context to draw with at all: skip every GL call of this frame.
bool PrepareFrame();
/// Events must be taken without the system's own pump this frame (it would wait for the
/// hidden window). For den::input.
bool EventPumpHeld();
/// Subtle tactile pulse when the user presses a touch control (no-op on desktop).
void HapticTick();

} // namespace den::pal
