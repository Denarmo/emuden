// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// HTTP on Android: the platform's HttpURLConnection, reached through the activity
// (shells/android, EmuDenActivity.httpOpen and HttpCall).

#include <fstream>
#include <system_error>
#include <vector>

#include <SDL3/SDL.h>
#include <jni.h>

#include "den/base/files.h"
#include "den/platform/pal.h"

namespace den::pal {

namespace {

constexpr jsize CHUNK = 64 * 1024;

std::string TakeString(JNIEnv* env, jobject text) {
    if (!text) {
        return {};
    }
    const auto string = static_cast<jstring>(text);
    const char* chars = env->GetStringUTFChars(string, nullptr);
    std::string result = chars ? chars : "";
    if (chars) {
        env->ReleaseStringUTFChars(string, chars);
    }
    return result;
}

// True when a Java exception was pending; it is cleared and noted in the response.
bool Threw(JNIEnv* env, HttpResponse& response) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    env->ExceptionClear();
    if (response.error.empty()) {
        response.error = "the Android HTTP client failed";
    }
    return true;
}

void Transfer(JNIEnv* env, const HttpRequest& request, HttpResponse& response) {
    const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) {
        response.error = "no activity";
        return;
    }
    jclass activity_class = env->GetObjectClass(activity);
    jmethodID open = env->GetStaticMethodID(
        activity_class, "httpOpen",
        "(Ljava/lang/String;[Ljava/lang/String;I)Lio/github/denarmo/emuden/HttpCall;");
    if (Threw(env, response) || !open) {
        response.error = "this build has no HTTP client";
        return;
    }
    jobjectArray headers = env->NewObjectArray(static_cast<jsize>(request.headers.size() * 2),
                                               env->FindClass("java/lang/String"), nullptr);
    jsize index = 0;
    for (const auto& [name, value] : request.headers) {
        jstring n = env->NewStringUTF(name.c_str());
        jstring v = env->NewStringUTF(value.c_str());
        env->SetObjectArrayElement(headers, index++, n);
        env->SetObjectArrayElement(headers, index++, v);
        env->DeleteLocalRef(n);
        env->DeleteLocalRef(v);
    }
    jobject call = env->CallStaticObjectMethod(activity_class, open,
                                               env->NewStringUTF(request.url.c_str()), headers,
                                               static_cast<jint>(request.timeout_seconds));
    if (Threw(env, response) || !call) {
        return;
    }
    jclass call_class = env->GetObjectClass(call);
    jmethodID status = env->GetMethodID(call_class, "status", "()I");
    jmethodID length = env->GetMethodID(call_class, "length", "()J");
    jmethodID error = env->GetMethodID(call_class, "error", "()Ljava/lang/String;");
    jmethodID read = env->GetMethodID(call_class, "read", "([B)I");
    jmethodID close = env->GetMethodID(call_class, "close", "()V");

    response.status = env->CallIntMethod(call, status);
    response.error = TakeString(env, env->CallObjectMethod(call, error));
    const jlong announced = env->CallLongMethod(call, length);
    const std::uint64_t total = announced > 0 ? static_cast<std::uint64_t>(announced) : 0;

    const bool to_file = !request.to_file.empty();
    std::filesystem::path part = request.to_file;
    part += ".part";
    std::ofstream file;
    if (to_file) {
        file.open(part, std::ios::binary | std::ios::trunc);
        if (!file && response.error.empty()) {
            response.error = "could not write " + PathToUtf8(part);
        }
    }
    jbyteArray buffer = env->NewByteArray(CHUNK);
    std::vector<char> bytes(CHUNK);
    while (response.error.empty()) {
        const jint n = env->CallIntMethod(call, read, buffer);
        if (Threw(env, response) || n == 0) {
            break;
        }
        if (n < 0) {
            response.error = TakeString(env, env->CallObjectMethod(call, error));
            break;
        }
        env->GetByteArrayRegion(buffer, 0, n, reinterpret_cast<jbyte*>(bytes.data()));
        response.received += static_cast<std::uint64_t>(n);
        if (to_file) {
            file.write(bytes.data(), n);
            if (!file) {
                response.error = "could not write " + PathToUtf8(part);
            }
        } else if (response.body.size() + static_cast<std::size_t>(n) > request.max_body) {
            response.error = "the answer is larger than allowed";
        } else {
            response.body.append(bytes.data(), static_cast<std::size_t>(n));
        }
        if (response.error.empty() && request.progress &&
            !request.progress(response.received, total)) {
            response.error = "cancelled";
        }
    }
    env->CallVoidMethod(call, close);
    env->ExceptionClear();

    if (to_file) {
        file.close();
        std::error_code ec;
        if (response.ok()) {
            std::filesystem::rename(part, request.to_file, ec);
        } else {
            std::filesystem::remove(part, ec);
        }
    }
}

} // namespace

HttpResponse HttpGet(const HttpRequest& request) {
    HttpResponse response;
    // SDL attaches the calling thread to the Java VM and detaches it when the thread ends.
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env || env->PushLocalFrame(32) != 0) {
        response.error = "no Java environment";
        return response;
    }
    Transfer(env, request, response);
    env->PopLocalFrame(nullptr);
    return response;
}

} // namespace den::pal
