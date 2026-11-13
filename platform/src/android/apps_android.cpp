// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The device's other apps on Android: AppList.java (shells/android) asks the package manager
// for the launcher's activities, renders their icons into the app cache and opens one.

#include <atomic>

#include "den/base/log.h"
#include "den/platform/apps.h"
#include "jni_calls.h"

namespace den::pal::apps {

namespace {

std::atomic<std::uint64_t> g_revision{1};
std::atomic<bool> g_focused{true};

} // namespace

bool Available() {
    return true;
}

std::vector<App> List() {
    std::vector<App> apps;
    const jni::ActivityCall call;
    const jmethodID method = call.Method("appsList", "()[Ljava/lang/String;");
    if (!method) {
        return apps;
    }
    const auto strings = static_cast<jobjectArray>(call.env->CallStaticObjectMethod(call.activity_class, method));
    if (call.Threw() || !strings) {
        log::Warn("platform", "the apps could not be listed");
        return apps;
    }
    const jsize count = call.env->GetArrayLength(strings);
    for (jsize i = 0; i + 2 < count; i += 3) {
        App app;
        app.id = call.Take(call.env->GetObjectArrayElement(strings, i));
        app.label = call.Take(call.env->GetObjectArrayElement(strings, i + 1));
        app.icon = call.Take(call.env->GetObjectArrayElement(strings, i + 2));
        apps.push_back(std::move(app));
    }
    call.env->DeleteLocalRef(strings);
    return apps;
}

std::uint64_t Revision() {
    return g_revision.load();
}

Status Launch(const std::string& id, Display display) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method("appsLaunch", "(Ljava/lang/String;I)Ljava/lang/String;");
    if (!method) {
        return Status::Fail("this build cannot open other apps");
    }
    jstring name = call.env->NewStringUTF(id.c_str());
    const jint where = display == Display::Main ? 1 : display == Display::Second ? 2 : 0;
    jobject answer = call.env->CallStaticObjectMethod(call.activity_class, method, name, where);
    const bool threw = call.Threw();
    call.env->DeleteLocalRef(name);
    if (threw) {
        return Status::Fail("The app could not be opened");
    }
    const std::string result = call.Take(answer);
    if (result.rfind("error:", 0) == 0) {
        return Status::Fail(result.substr(6));
    }
    Status status;
    if (result.rfind("note:", 0) == 0) {
        status.notes.push_back(result.substr(5));
    }
    return status;
}

bool HasInputFocus() {
    return g_focused.load();
}

} // namespace den::pal::apps

extern "C" {

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_AppList_nativeChanged(JNIEnv*, jclass) {
    ++den::pal::apps::g_revision;
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_AppList_nativeFocus(JNIEnv*, jclass, jboolean focused) {
    den::pal::apps::g_focused = focused != JNI_FALSE;
}

} // extern "C"
