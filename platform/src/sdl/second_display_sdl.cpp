// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen on the desktop: a second window that the GL context of the app draws
// into. Its mouse events never reach the event queue of the app; they wait for TakePointer.

#include <mutex>

#include <SDL3/SDL.h>

#include "den/base/log.h"
#include "den/platform/pal.h"
#include "den/platform/second_display.h"

namespace den::pal {

namespace {

class WindowSecondDisplay final : public SecondDisplay {
public:
    bool Present() const override {
        return GetEnv("EMUDEN_SECOND_WINDOW") == "1";
    }

    void SetWanted(bool wanted) override {
        if (wanted == wanted_) {
            return;
        }
        wanted_ = wanted;
        closed_by_user_ = false;
        if (wanted) {
            Open();
        } else {
            Close();
        }
    }

    bool Ready() const override {
        return window_ != nullptr && !closed_by_user_;
    }

    void PixelSize(int& width, int& height) const override {
        width = height = 0;
        if (window_) {
            SDL_GetWindowSizeInPixels(window_, &width, &height);
        }
    }

    bool BeginFrame() override {
        if (closed_by_user_ && window_) {
            Close();
        }
        if (!window_) {
            return false;
        }
        main_window_ = SDL_GL_GetCurrentWindow();
        context_ = SDL_GL_GetCurrentContext();
        if (!main_window_ || !context_) {
            return false;
        }
        SDL_GL_GetSwapInterval(&main_interval_);
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            log::Warn("platform", "the second window cannot be drawn on: {}", SDL_GetError());
            SDL_GL_MakeCurrent(main_window_, context_);
            Close();
            return false;
        }
        // Only the main window waits for the display: two waits would halve the frame rate.
        SDL_GL_SetSwapInterval(0);
        return true;
    }

    void EndFrame() override {
        SDL_GL_SwapWindow(window_);
        SDL_GL_MakeCurrent(main_window_, context_);
        SDL_GL_SetSwapInterval(main_interval_);
    }

    void TakePointer(std::vector<SecondPointer>& out) override {
        std::lock_guard lock(mutex_);
        out.insert(out.end(), pointer_.begin(), pointer_.end());
        pointer_.clear();
    }

private:
    void Open() {
        if (window_) {
            return;
        }
        // The same pixel format as the main window (the GL attributes are still set), so
        // the one context can draw on both.
        window_ = SDL_CreateWindow("EmuDEN - second screen", 640, 480,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window_) {
            log::Warn("platform", "no second window: {}", SDL_GetError());
            return;
        }
        id_ = SDL_GetWindowID(window_);
        SDL_SetEventFilter(&WindowSecondDisplay::Filter, this);
        log::Info("platform", "second window opened");
    }

    void Close() {
        if (!window_) {
            return;
        }
        SDL_SetEventFilter(nullptr, nullptr);
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        id_ = 0;
        down_ = false;
        std::lock_guard lock(mutex_);
        pointer_.clear();
    }

    void Queue(SecondPointer::Kind kind, float x, float y) {
        const float density = SDL_GetWindowPixelDensity(window_);
        const float scale = density > 0.0f ? density : 1.0f;
        std::lock_guard lock(mutex_);
        pointer_.push_back({kind, x * scale, y * scale});
    }

    // Runs when an event is queued. False keeps it out of the queue the app reads.
    static bool SDLCALL Filter(void* userdata, SDL_Event* event) {
        auto* self = static_cast<WindowSecondDisplay*>(userdata);
        switch (event->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.windowID != self->id_) {
                return true;
            }
            if (event->button.button == SDL_BUTTON_LEFT) {
                self->down_ = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                self->Queue(self->down_ ? SecondPointer::Kind::Down : SecondPointer::Kind::Up,
                            event->button.x, event->button.y);
            }
            return false;
        case SDL_EVENT_MOUSE_MOTION:
            if (event->motion.windowID != self->id_) {
                return true;
            }
            if (self->down_) {
                self->Queue(SecondPointer::Kind::Move, event->motion.x, event->motion.y);
            }
            return false;
        case SDL_EVENT_MOUSE_WHEEL:
            return event->wheel.windowID != self->id_;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event->window.windowID != self->id_) {
                return true;
            }
            // Closing the second window closes only it; the next frame takes it down.
            self->closed_by_user_ = true;
            return false;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
            return event->window.windowID != self->id_;
        default:
            return true;
        }
    }

    SDL_Window* window_ = nullptr;
    SDL_WindowID id_ = 0;
    SDL_Window* main_window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    int main_interval_ = 1;
    bool wanted_ = false;
    bool closed_by_user_ = false;
    bool down_ = false;
    std::mutex mutex_;
    std::vector<SecondPointer> pointer_;
};

} // namespace

SecondDisplay& GetSecondDisplay() {
    // Never destroyed: SDL takes the window down when it quits.
    static auto* display = new WindowSecondDisplay();
    return *display;
}

} // namespace den::pal
