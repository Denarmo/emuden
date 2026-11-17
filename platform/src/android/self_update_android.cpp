// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Android: the APK goes to the system's package installer (shells/android, UpdateInstall), which
// asks the user. The app never replaces itself.

#include <signal.h>
#include <unistd.h>

#include <cerrno>

#include "den/base/files.h"
#include "den/platform/self_update.h"
#include "jni_calls.h"

namespace den::pal::self_update {

Target Detect() {
    Target target;
    target.method = Method::AndroidApk;
    return target;
}

int ProcessId() {
    return static_cast<int>(getpid());
}

bool WaitForExit(int pid, int timeout_ms) {
    for (int waited = 0; kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM; waited += 100) {
        if (waited >= timeout_ms) {
            return false;
        }
        usleep(100 * 1000);
    }
    return true;
}

Status StartDetached(const std::filesystem::path&, const std::vector<std::string>&) {
    return Status::Fail("Android starts no programs of its own.");
}

Status StartAfterExit(const std::filesystem::path&) {
    return Status::Fail("Android starts no programs of its own.");
}

Status MakeExecutable(const std::filesystem::path&) {
    return {};
}

Status InstallPackage(const std::filesystem::path& apk) {
    const jni::ActivityCall call;
    const jmethodID method = call.Method("updateInstall", "(Ljava/lang/String;)Ljava/lang/String;");
    if (!method) {
        return Status::Fail("This build cannot hand updates to the installer.");
    }
    jstring path = call.env->NewStringUTF(PathToUtf8(apk).c_str());
    jobject answer = call.env->CallStaticObjectMethod(call.activity_class, method, path);
    call.env->DeleteLocalRef(path);
    if (call.Threw()) {
        return Status::Fail("The installer could not be reached.");
    }
    const std::string text = call.Take(answer);
    if (text.rfind("error:", 0) == 0) {
        return Status::Fail(text.substr(6));
    }
    return {};
}

} // namespace den::pal::self_update
