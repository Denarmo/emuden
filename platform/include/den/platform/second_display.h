// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen of the device: the other panel of a two-screen handheld (an Android
// Presentation), or a second window on the desktop. It is drawn with the GL context of the
// app, so textures, shaders and the canvas work on it as on the main window.

#pragma once

#include <vector>

namespace den::pal {

/// A touch or mouse press on the second screen, in its pixels.
struct SecondPointer {
    enum class Kind { Down, Move, Up };
    Kind kind = Kind::Down;
    float x = 0, y = 0;
};

/// Main thread only, with the GL context of the app current.
class SecondDisplay {
public:
    virtual ~SecondDisplay() = default;
    /// The device has a second screen now (a display can come and go). On the desktop:
    /// the app was started with EMUDEN_SECOND_WINDOW=1.
    virtual bool Present() const = 0;
    /// Opens the surface on the second screen (true) or gives the screen back (false). On
    /// the desktop true opens the second window even when Present() is false.
    virtual void SetWanted(bool wanted) = 0;
    /// There is a surface to draw on right now. It goes away and comes back with the
    /// display, with pause and resume, and with the window being closed.
    virtual bool Ready() const = 0;
    virtual void PixelSize(int& width, int& height) const = 0;
    /// Makes the second screen the target of the GL context. False when there is nothing
    /// to draw on; then nothing changed. Every true needs its EndFrame.
    virtual bool BeginFrame() = 0;
    /// Shows what was drawn and makes the main window the target again.
    virtual void EndFrame() = 0;
    /// Appends the presses since the last call, oldest first.
    virtual void TakePointer(std::vector<SecondPointer>& out) = 0;
};

SecondDisplay& GetSecondDisplay();

} // namespace den::pal
