// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Secrets on Android: a file encrypted with a key of the Android Keystore, kept by the shell
// (shells/android, SecretStore through EmuDenActivity.secretGet/Set/Remove).

#include <mutex>

#include "den/platform/pal.h"
#include "jni_calls.h"

namespace den::pal {

namespace {

constexpr std::size_t MAX_VALUE = 2048;

class KeystoreSecrets final : public Secrets {
public:
    std::optional<std::string> Get(const std::string& key) override {
        std::lock_guard lock(mutex_);
        const jni::ActivityCall call;
        const jmethodID method = call.Method("secretGet", "(Ljava/lang/String;)[B");
        if (!method) {
            return std::nullopt;
        }
        jstring name = call.env->NewStringUTF(key.c_str());
        auto bytes = static_cast<jbyteArray>(call.env->CallStaticObjectMethod(call.activity_class, method, name));
        call.env->DeleteLocalRef(name);
        if (call.Threw() || !bytes) {
            return std::nullopt;
        }
        std::string value(static_cast<std::size_t>(call.env->GetArrayLength(bytes)), '\0');
        call.env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(value.size()),
                                     reinterpret_cast<jbyte*>(value.data()));
        call.env->DeleteLocalRef(bytes);
        return value;
    }
    Status Set(const std::string& key, const std::string& value) override {
        if (value.size() > MAX_VALUE) {
            return Status::Fail("the secret is larger than 2 KiB");
        }
        std::lock_guard lock(mutex_);
        const jni::ActivityCall call;
        const jmethodID method = call.Method("secretSet", "(Ljava/lang/String;[B)Z");
        if (!method) {
            return Status::Fail("this build has no secret store");
        }
        jstring name = call.env->NewStringUTF(key.c_str());
        jbyteArray bytes = call.env->NewByteArray(static_cast<jsize>(value.size()));
        call.env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(value.size()),
                                     reinterpret_cast<const jbyte*>(value.data()));
        const jboolean stored = call.env->CallStaticBooleanMethod(call.activity_class, method, name, bytes);
        const bool threw = call.Threw();
        call.env->DeleteLocalRef(name);
        call.env->DeleteLocalRef(bytes);
        if (threw || !stored) {
            return Status::Fail("the secret could not be stored");
        }
        return {};
    }
    void Remove(const std::string& key) override {
        std::lock_guard lock(mutex_);
        const jni::ActivityCall call;
        if (const jmethodID method = call.Method("secretRemove", "(Ljava/lang/String;)V")) {
            jstring name = call.env->NewStringUTF(key.c_str());
            call.env->CallStaticVoidMethod(call.activity_class, method, name);
            call.Threw();
            call.env->DeleteLocalRef(name);
        }
    }

private:
    std::mutex mutex_;
};

} // namespace

Secrets& GetSecrets() {
    static KeystoreSecrets secrets;
    return secrets;
}

} // namespace den::pal
