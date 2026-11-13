// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The app going on while its main activity is stopped (another app covers its display and
// the game runs on the other one), and the system's media volume.
//
// SDL pauses its window when the activity stops: it takes the GL context off the thread and
// the surface goes. With SDL_HINT_ANDROID_BLOCK_ON_PAUSE off the loop is not blocked and the
// sound is not paused, but SDL's event pump then waits 100 ms on every call, and nothing has
// a context. So while the window is hidden the context is made current here on a surface of
// its own (a pixel buffer, or none at all), and the pump is left alone until the activity
// starts again (EmuDenActivity.nativeStopped); pad and key events are queued by SDL's Java
// side without it.

#include <atomic>

#include <EGL/egl.h>
#include <SDL3/SDL.h>
#include <jni.h>

#include "../lifecycle_native.h"
#include "den/base/log.h"
#include "den/platform/audio.h"
#include "den/platform/lifecycle.h"
#include "jni_calls.h"

namespace den::pal {

namespace {

std::atomic<bool> g_hidden{false};  // SDL paused the window
std::atomic<bool> g_stopped{false}; // the activity is stopped and not on its way back

// Main thread only.
EGLDisplay g_display = EGL_NO_DISPLAY;
EGLContext g_context = EGL_NO_CONTEXT;
EGLSurface g_buffer = EGL_NO_SURFACE;
bool g_buffer_tried = false;
bool g_told = false;

bool Watch(void*, SDL_Event* event) {
    if (event->type == SDL_EVENT_DID_ENTER_BACKGROUND) {
        g_hidden = true;
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        g_hidden = false;
    }
    return true;
}

void MakeBuffer() {
    g_buffer_tried = true;
    EGLint config_id = 0;
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglQueryContext(g_display, g_context, EGL_CONFIG_ID, &config_id)) {
        return;
    }
    const EGLint wanted[] = {EGL_CONFIG_ID, config_id, EGL_NONE};
    if (!eglChooseConfig(g_display, wanted, &config, 1, &count) || count != 1) {
        return;
    }
    const EGLint size[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    g_buffer = eglCreatePbufferSurface(g_display, config, size);
    if (g_buffer == EGL_NO_SURFACE) {
        eglGetError(); // the window's config has no pixel buffers: without a surface then
    }
}

int VolumeCall(const char* name, const char* signature, int argument, bool has_argument) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method(name, signature);
    if (!method) {
        return 0;
    }
    jint result = 0;
    if (has_argument) {
        call.env->CallStaticVoidMethod(call.activity_class, method, static_cast<jint>(argument));
    } else {
        result = call.env->CallStaticIntMethod(call.activity_class, method);
    }
    return call.Threw() ? 0 : result;
}

} // namespace

void LifecycleBeforeInit() {
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
}

void LifecycleAfterInit() {
    SDL_AddEventWatch(Watch, nullptr);
}

bool MainWindowHidden() {
    return g_hidden.load();
}

bool EventPumpHeld() {
    return g_hidden.load() && g_stopped.load();
}

bool PrepareFrame() {
    const EGLContext current = eglGetCurrentContext();
    if (!g_hidden.load()) {
        if (current != EGL_NO_CONTEXT) {
            g_context = current;
            g_display = eglGetCurrentDisplay();
        }
        g_told = false;
        return current != EGL_NO_CONTEXT;
    }
    if (current != EGL_NO_CONTEXT) {
        return true;
    }
    if (g_context == EGL_NO_CONTEXT || g_display == EGL_NO_DISPLAY) {
        return false;
    }
    if (!g_buffer_tried) {
        MakeBuffer();
    }
    if (eglMakeCurrent(g_display, g_buffer, g_buffer, g_context)) {
        if (!g_told) {
            g_told = true;
            log::Info("platform", "the main window is hidden; drawing goes on {}",
                      g_buffer != EGL_NO_SURFACE ? "with a buffer of its own" : "without a surface");
        }
        return true;
    }
    if (!g_told) {
        g_told = true;
        log::Warn("platform", "no GL context while the main window is hidden (EGL {:#x})", eglGetError());
    }
    return false;
}

namespace audio {

bool HasSystemVolume() {
    return true;
}

int SystemVolumeSteps() {
    static const int steps = VolumeCall("volumeSteps", "()I", 0, false);
    return steps;
}

int SystemVolume() {
    return VolumeCall("volumeGet", "()I", 0, false);
}

void SetSystemVolume(int step) {
    VolumeCall("volumeSet", "(I)V", step, true);
}

bool HasScreenControls() {
    static const bool has = [] {
        const jni::ActivityCall call;
        const jmethodID method = call.Method("screenControlsAvailable", "()Z");
        if (!method) {
            return false;
        }
        const jboolean result = call.env->CallStaticBooleanMethod(call.activity_class, method);
        return !call.Threw() && result != JNI_FALSE;
    }();
    return has;
}

void OpenScreenControls() {
    const jni::ActivityCall call;
    if (const jmethodID method = call.Method("screenControlsOpen", "()V")) {
        call.env->CallStaticVoidMethod(call.activity_class, method);
        call.Threw();
    }
}

} // namespace audio

} // namespace den::pal

extern "C" {

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_EmuDenActivity_nativeStopped(JNIEnv*, jclass,
                                                                                 jboolean stopped) {
    den::pal::g_stopped = stopped != JNI_FALSE;
}

} // extern "C"
