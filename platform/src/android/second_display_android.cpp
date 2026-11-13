// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen on Android: SecondScreen.java (shells/android) shows a Presentation on
// the other display and hands its Surface over; here it becomes an EGL window surface of
// the app's own display and config, so the one GL context draws on both screens.
//
// The Java side calls in from the UI thread, the app draws on its own thread. The surface is
// current only between BeginFrame and EndFrame, and the lock is held for that whole time, so
// the UI thread can destroy the surface whenever it holds the lock.

#include <mutex>

#include <EGL/egl.h>
#include <SDL3/SDL.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "den/base/log.h"
#include "den/platform/second_display.h"

namespace den::pal {

namespace {

class AndroidSecondDisplay final : public SecondDisplay {
public:
    bool Present() const override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return present_;
    }

    void SetWanted(bool wanted) override {
        if (wanted == wanted_) {
            return;
        }
        wanted_ = wanted;
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (!env || !activity) {
            return;
        }
        jclass activity_class = env->GetObjectClass(activity);
        jmethodID method = env->GetStaticMethodID(activity_class, "secondScreenWanted", "(Z)V");
        if (method && !env->ExceptionCheck()) {
            env->CallStaticVoidMethod(activity_class, method, static_cast<jboolean>(wanted));
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);
    }

    bool Ready() const override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return window_ != nullptr && !failed_;
    }

    void PixelSize(int& width, int& height) const override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        width = width_;
        height = height_;
    }

    bool BeginFrame() override {
        mutex_.lock();
        if (!window_ || failed_ || !MakeSurface()) {
            mutex_.unlock();
            return false;
        }
        context_ = eglGetCurrentContext();
        main_draw_ = eglGetCurrentSurface(EGL_DRAW);
        main_read_ = eglGetCurrentSurface(EGL_READ);
        if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
            log::Warn("platform", "the second screen cannot be drawn on (EGL {:#x})", eglGetError());
            failed_ = true;
            eglMakeCurrent(display_, main_draw_, main_read_, context_);
            mutex_.unlock();
            return false;
        }
        // Only the main screen waits for its display: two waits would halve the frame rate.
        eglSwapInterval(display_, 0);
        EGLint w = 0, h = 0;
        if (eglQuerySurface(display_, surface_, EGL_WIDTH, &w) &&
            eglQuerySurface(display_, surface_, EGL_HEIGHT, &h) && w > 0 && h > 0) {
            width_ = w;
            height_ = h;
        }
        return true;
    }

    void EndFrame() override {
        eglSwapBuffers(display_, surface_);
        eglMakeCurrent(display_, main_draw_, main_read_, context_);
        mutex_.unlock();
    }

    void TakePointer(std::vector<SecondPointer>& out) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        out.insert(out.end(), pointer_.begin(), pointer_.end());
        pointer_.clear();
    }

    // From the UI thread.
    void SetPresent(bool present) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (present != present_) {
            log::Info("platform", "second screen {}", present ? "found" : "gone");
        }
        present_ = present;
    }

    void SetWindow(ANativeWindow* window, int width, int height) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (window != window_) {
            DropSurface();
            if (window_) {
                ANativeWindow_release(window_);
            }
            window_ = window;
            failed_ = false;
            pointer_.clear();
            log::Info("platform", "second screen surface {}", window ? "ready" : "gone");
        } else if (window) {
            ANativeWindow_release(window); // the extra reference of this call
        }
        width_ = width;
        height_ = height;
    }

    void Touch(SecondPointer::Kind kind, float x, float y) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (window_ && pointer_.size() < 256) {
            pointer_.push_back({kind, x, y});
        }
    }

private:
    bool MakeSurface() {
        if (surface_ != EGL_NO_SURFACE) {
            return true;
        }
        display_ = eglGetCurrentDisplay();
        const EGLContext context = eglGetCurrentContext();
        EGLint config_id = 0;
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (display_ == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT ||
            !eglQueryContext(display_, context, EGL_CONFIG_ID, &config_id)) {
            return false; // no context right now (the app is on its way to the background)
        }
        const EGLint wanted[] = {EGL_CONFIG_ID, config_id, EGL_NONE};
        if (eglChooseConfig(display_, wanted, &config, 1, &count) && count == 1) {
            surface_ = eglCreateWindowSurface(display_, config, window_, nullptr);
        }
        if (surface_ == EGL_NO_SURFACE) {
            log::Warn("platform", "no surface on the second screen (EGL {:#x})", eglGetError());
            failed_ = true;
            return false;
        }
        return true;
    }

    void DropSurface() {
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
    }

    mutable std::recursive_mutex mutex_; // recursive: a drawing client may ask for the size
    bool wanted_ = false;
    bool present_ = false;
    bool failed_ = false;
    ANativeWindow* window_ = nullptr;
    int width_ = 0, height_ = 0;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLSurface main_draw_ = EGL_NO_SURFACE, main_read_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    std::vector<SecondPointer> pointer_;
};

AndroidSecondDisplay& Display() {
    static auto* display = new AndroidSecondDisplay();
    return *display;
}

} // namespace

SecondDisplay& GetSecondDisplay() {
    return Display();
}

} // namespace den::pal

extern "C" {

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_SecondScreen_nativeDisplay(JNIEnv*, jclass,
                                                                                jboolean present) {
    den::pal::Display().SetPresent(present != JNI_FALSE);
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_SecondScreen_nativeSurface(
    JNIEnv* env, jclass, jobject surface, jint width, jint height) {
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    den::pal::Display().SetWindow(window, width, height);
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_SecondScreen_nativeTouch(JNIEnv*, jclass, jint kind,
                                                                              jfloat x, jfloat y) {
    using Kind = den::pal::SecondPointer::Kind;
    den::pal::Display().Touch(kind == 0 ? Kind::Down : kind == 1 ? Kind::Move : Kind::Up, x, y);
}

} // extern "C"
