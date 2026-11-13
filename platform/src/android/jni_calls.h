// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Calls into the activity's static methods (shells/android, EmuDenActivity) from any thread.

#pragma once

#include <string>

#include <SDL3/SDL.h>
#include <jni.h>

namespace den::pal::jni {

/// The JNI environment of the calling thread and the activity's class, for one call. The
/// class is null when there is no activity (or the call is made while it is going away).
struct ActivityCall {
    JNIEnv* env = nullptr;
    jclass activity_class = nullptr;

    ActivityCall() {
        env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (env && activity) {
            activity_class = env->GetObjectClass(activity);
            env->DeleteLocalRef(activity);
        }
    }
    ~ActivityCall() {
        if (env && activity_class) {
            env->DeleteLocalRef(activity_class);
        }
    }
    ActivityCall(const ActivityCall&) = delete;
    ActivityCall& operator=(const ActivityCall&) = delete;

    /// A static method of the activity, or null (a pending exception is cleared).
    jmethodID Method(const char* name, const char* signature) const {
        if (!activity_class) {
            return nullptr;
        }
        const jmethodID method = env->GetStaticMethodID(activity_class, name, signature);
        return Threw() ? nullptr : method;
    }
    /// True when a Java exception was pending; it is cleared.
    bool Threw() const {
        if (env && env->ExceptionCheck()) {
            env->ExceptionClear();
            return true;
        }
        return false;
    }
    std::string Take(jobject text) const {
        if (!text) {
            return {};
        }
        const auto string = static_cast<jstring>(text);
        const char* chars = env->GetStringUTFChars(string, nullptr);
        std::string result = chars ? chars : "";
        if (chars) {
            env->ReleaseStringUTFChars(string, chars);
        }
        env->DeleteLocalRef(text);
        return result;
    }
};

} // namespace den::pal::jni
