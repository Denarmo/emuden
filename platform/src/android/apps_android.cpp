// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The device's other apps on Android: AppList.java (shells/android) asks the package manager
// for the launcher's activities, renders their icons into the app cache and opens one;
// AppBeside.java moves this app's own windows over and under it and shows the handle.

#include <atomic>
#include <mutex>

#include "den/base/log.h"
#include "den/platform/apps.h"
#include "jni_calls.h"

namespace den::pal::apps {

namespace {

std::atomic<std::uint64_t> g_revision{1};
std::atomic<bool> g_focused{true};
std::atomic<bool> g_catch_main{false}, g_catch_second{false};
std::mutex g_events_lock;
std::vector<Event> g_events;

jint Where(Display display) {
    return display == Display::Main ? 1 : display == Display::Second ? 2 : 0;
}

void Push(Event::Kind kind, jint display) {
    std::lock_guard lock(g_events_lock);
    if (g_events.size() < 64) {
        g_events.push_back({kind, display == 2 ? Display::Second : display == 1 ? Display::Main : Display::Default});
    }
}

// A static method of the activity that takes the display and returns nothing or a flag.
bool DisplayCall(const char* name, const char* signature, Display display, bool flag) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method(name, signature);
    if (!method) {
        return false;
    }
    jboolean answer = JNI_FALSE;
    if (flag) {
        answer = call.env->CallStaticBooleanMethod(call.activity_class, method, Where(display));
    } else {
        call.env->CallStaticVoidMethod(call.activity_class, method, Where(display));
    }
    return !call.Threw() && answer != JNI_FALSE;
}

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
    jobject answer = call.env->CallStaticObjectMethod(call.activity_class, method, name, Where(display));
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

void Raise(Display display) {
    DisplayCall("besideRaise", "(I)V", display, false);
}

void Lower(Display display, const std::string& app) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method("besideLower", "(ILjava/lang/String;)V");
    if (!method) {
        return;
    }
    jstring name = call.env->NewStringUTF(app.c_str());
    call.env->CallStaticVoidMethod(call.activity_class, method, Where(display), name);
    call.Threw();
    call.env->DeleteLocalRef(name);
}

bool ShowHandle(Display display) {
    return DisplayCall("besideHandle", "(I)Z", display, true);
}

void HideHandle() {
    DisplayCall("besideHandleHide", "(I)V", Display::Default, false);
}

void CatchBack(Display display, bool on) {
    (display == Display::Second ? g_catch_second : g_catch_main) = on;
}

std::vector<Event> TakeEvents() {
    std::lock_guard lock(g_events_lock);
    std::vector<Event> events;
    events.swap(g_events);
    return events;
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

// A handle tap (0) or Home (2), from the UI thread.
JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_AppBeside_nativeEvent(JNIEnv*, jclass, jint kind,
                                                                           jint display) {
    using Kind = den::pal::apps::Event::Kind;
    den::pal::apps::Push(kind == 2 ? Kind::Home : Kind::Handle, display);
}

// Back on a display (1 main, 2 second). True when the app wants it as an event of its own.
JNIEXPORT jboolean JNICALL Java_io_github_denarmo_emuden_AppBeside_nativeBack(JNIEnv*, jclass, jint display) {
    const bool caught = (display == 2 ? den::pal::apps::g_catch_second : den::pal::apps::g_catch_main).load();
    if (caught) {
        den::pal::apps::Push(den::pal::apps::Event::Kind::Back, display);
    }
    return caught ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
