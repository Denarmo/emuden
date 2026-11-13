// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Device facts on Android: Build.MODEL and friends, every display with its physical size,
// memory and the built-in pad, as "key=value" lines from DeviceFacts.describe (shells/android).

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <jni.h>

#include "../device_native.h"

namespace den::pal {

namespace {

// The class is loaded through the activity's class loader: FindClass on a native thread
// only knows the system classes.
std::string Describe(JNIEnv* env, jobject activity) {
    std::string text;
    jclass activity_class = env->GetObjectClass(activity);
    jclass class_class = env->FindClass("java/lang/Class");
    jclass loader_class = env->FindClass("java/lang/ClassLoader");
    jmethodID get_loader = env->GetMethodID(class_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jmethodID load = env->GetMethodID(loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jobject loader = env->CallObjectMethod(activity_class, get_loader);
    jstring name = env->NewStringUTF("io.github.denarmo.emuden.DeviceFacts");
    const auto facts_class = static_cast<jclass>(loader ? env->CallObjectMethod(loader, load, name) : nullptr);
    if (!env->ExceptionCheck() && facts_class) {
        jmethodID describe =
            env->GetStaticMethodID(facts_class, "describe", "(Landroid/app/Activity;)Ljava/lang/String;");
        const auto result = static_cast<jstring>(
            describe && !env->ExceptionCheck() ? env->CallStaticObjectMethod(facts_class, describe, activity)
                                               : nullptr);
        if (!env->ExceptionCheck() && result) {
            if (const char* chars = env->GetStringUTFChars(result, nullptr)) {
                text = chars;
                env->ReleaseStringUTFChars(result, chars);
            }
        }
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    return text;
}

std::vector<std::string> Split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, separator);) {
        parts.push_back(part);
    }
    return parts;
}

} // namespace

void FillNativeFacts(DeviceFacts& facts) {
    facts.touch = true;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) {
        return;
    }
    env->PushLocalFrame(32);
    const std::string text = Describe(env, activity);
    env->PopLocalFrame(nullptr);

    std::vector<DisplayFacts> displays;
    for (const std::string& line : Split(text, '\n')) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "manufacturer") {
            facts.manufacturer = value;
        } else if (key == "model") {
            facts.model = value;
        } else if (key == "soc") {
            facts.soc = value;
        } else if (key == "release") {
            facts.os_version = value;
        } else if (key == "sdk") {
            facts.api_level = std::atoi(value.c_str());
        } else if (key == "ram_mb") {
            facts.ram_mb = std::atoi(value.c_str());
        } else if (key == "touch") {
            facts.touch = value == "1";
        } else if (key == "pad") {
            facts.builtin_pad = value == "1";
        } else if (key == "display") {
            // name|width|height|refresh|xdpi|ydpi|density
            const std::vector<std::string> fields = Split(value, '|');
            if (fields.size() < 7) {
                continue;
            }
            DisplayFacts display;
            display.name = fields[0];
            display.width = std::atoi(fields[1].c_str());
            display.height = std::atoi(fields[2].c_str());
            display.refresh_hz = static_cast<float>(std::atof(fields[3].c_str()));
            const double xdpi = std::atof(fields[4].c_str()), ydpi = std::atof(fields[5].c_str());
            display.density = static_cast<float>(std::atof(fields[6].c_str()));
            if (xdpi > 0 && ydpi > 0) {
                display.diagonal_inches =
                    static_cast<float>(std::hypot(display.width / xdpi, display.height / ydpi));
            }
            displays.push_back(std::move(display));
        }
    }
    // SDL knows one display on Android; the system's list has the second screen too.
    if (!displays.empty()) {
        facts.displays = std::move(displays);
    }
}

} // namespace den::pal
