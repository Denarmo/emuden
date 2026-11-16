// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Engine apps on Android: EngineApps.java (shells/android) asks the package manager, runs
// the PackageInstaller session and builds the PLAY intent.

#include <cstdlib>

#include "den/base/files.h"
#include "den/platform/engine_apps.h"
#include "jni_calls.h"

namespace den::pal::engine_apps {

namespace {

jint Where(apps::Display display) {
    return display == apps::Display::Main ? 1 : display == apps::Display::Second ? 2 : 0;
}

// A static String method of the activity taking strings: "" is fine, "error:..." is not.
Status Answer(const jni::ActivityCall& call, jobject answer) {
    if (call.Threw()) {
        return Status::Fail("The engine app could not be reached.");
    }
    const std::string text = call.Take(answer);
    if (text.rfind("error:", 0) == 0) {
        return Status::Fail(text.substr(6));
    }
    return {};
}

Status CallWithText(const char* name, const std::string& text) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method(name, "(Ljava/lang/String;)Ljava/lang/String;");
    if (!method) {
        return Status::Fail("This build cannot start engine apps.");
    }
    jstring value = call.env->NewStringUTF(text.c_str());
    jobject answer = call.env->CallStaticObjectMethod(call.activity_class, method, value);
    call.env->DeleteLocalRef(value);
    return Answer(call, answer);
}

} // namespace

bool Available() {
    return true;
}

Installed Query(const std::string& package) {
    Installed out;
    const jni::ActivityCall call;
    const jmethodID method = call.Method("engineQuery", "(Ljava/lang/String;)Ljava/lang/String;");
    if (!method) {
        return out;
    }
    jstring name = call.env->NewStringUTF(package.c_str());
    jobject answer = call.env->CallStaticObjectMethod(call.activity_class, method, name);
    call.env->DeleteLocalRef(name);
    if (call.Threw()) {
        return out;
    }
    // "<cert hex>|<version code>|<plays 0/1>|<version name>", or "" when not installed.
    const std::string text = call.Take(answer);
    const std::size_t a = text.find('|');
    const std::size_t b = a == std::string::npos ? a : text.find('|', a + 1);
    const std::size_t c = b == std::string::npos ? b : text.find('|', b + 1);
    if (c == std::string::npos) {
        return out;
    }
    out.present = true;
    out.cert_sha256 = text.substr(0, a);
    out.version_code = std::strtoll(text.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
    out.plays = text.substr(b + 1, c - b - 1) == "1";
    out.version_name = text.substr(c + 1);
    return out;
}

Status Install(const std::filesystem::path& apk) {
    return CallWithText("engineInstall", PathToUtf8(apk));
}

Status OpenStore(const std::string& url) {
    return CallWithText("engineStore", url);
}

Status Play(const PlayRequest& request) {
    const jni::ActivityCall call;
    const jmethodID method =
        call.Method("enginePlay", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;"
                                  "Ljava/lang/String;ZI)Ljava/lang/String;");
    if (!method) {
        return Status::Fail("This build cannot start engine apps.");
    }
    JNIEnv* env = call.env;
    jstring package = env->NewStringUTF(request.package.c_str());
    jstring cert = env->NewStringUTF(request.cert_sha256.c_str());
    jstring content = env->NewStringUTF(PathToUtf8(request.content).c_str());
    jstring token = env->NewStringUTF(request.token.c_str());
    jstring session = env->NewStringUTF(request.session.c_str());
    jobject answer = env->CallStaticObjectMethod(call.activity_class, method, package, cert, content,
                                                 static_cast<jint>(request.port), token, session,
                                                 request.fullscreen ? JNI_TRUE : JNI_FALSE, Where(request.display));
    for (jstring text : {package, cert, content, token, session}) {
        env->DeleteLocalRef(text);
    }
    return Answer(call, answer);
}

Status Open(const PlayRequest& request) {
    const jni::ActivityCall call;
    const jmethodID method =
        call.Method("engineOpen", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
    if (!method) {
        return Status::Fail("This build cannot start apps for games.");
    }
    JNIEnv* env = call.env;
    jstring package = env->NewStringUTF(request.package.c_str());
    jstring cert = env->NewStringUTF(request.cert_sha256.c_str());
    jstring content = env->NewStringUTF(PathToUtf8(request.content).c_str());
    jobject answer =
        env->CallStaticObjectMethod(call.activity_class, method, package, cert, content, Where(request.display));
    for (jstring text : {package, cert, content}) {
        env->DeleteLocalRef(text);
    }
    return Answer(call, answer);
}

Status Resume(const std::string& package, apps::Display display) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method("engineResume", "(Ljava/lang/String;I)Ljava/lang/String;");
    if (!method) {
        return Status::Fail("This build cannot start engine apps.");
    }
    jstring name = call.env->NewStringUTF(package.c_str());
    jobject answer = call.env->CallStaticObjectMethod(call.activity_class, method, name, Where(display));
    call.env->DeleteLocalRef(name);
    return Answer(call, answer);
}

Result<std::string> Share(const std::string& package, const std::filesystem::path& path) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method("engineShare", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!method) {
        return Status::Fail("This build cannot share files with engine apps.");
    }
    jstring name = call.env->NewStringUTF(package.c_str());
    jstring file = call.env->NewStringUTF(PathToUtf8(path).c_str());
    jobject answer = call.env->CallStaticObjectMethod(call.activity_class, method, name, file);
    call.env->DeleteLocalRef(name);
    call.env->DeleteLocalRef(file);
    if (call.Threw()) {
        return Status::Fail("The engine app could not be reached.");
    }
    std::string text = call.Take(answer);
    if (text.empty() || text.rfind("error:", 0) == 0) {
        return Status::Fail(text.empty() ? "The file could not be shared." : text.substr(6));
    }
    return text;
}

Status Configure(const std::string& package, const std::string& cert_sha256, const std::string& fields) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method(
        "engineConfigure", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!method) {
        return Status::Fail("This build cannot set up engine apps.");
    }
    JNIEnv* env = call.env;
    jstring name = env->NewStringUTF(package.c_str());
    jstring cert = env->NewStringUTF(cert_sha256.c_str());
    jstring json = env->NewStringUTF(fields.c_str());
    jobject answer = env->CallStaticObjectMethod(call.activity_class, method, name, cert, json);
    for (jstring text : {name, cert, json}) {
        env->DeleteLocalRef(text);
    }
    return Answer(call, answer);
}

} // namespace den::pal::engine_apps
