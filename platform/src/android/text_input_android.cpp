// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The system keyboard on Android: TextBridge.java (shells/android) opens a window with an
// invisible text field on the display asked for and reports what is typed. Its calls arrive
// on the UI thread and are queued here for the app's thread; every session has a number, so
// what a closed session still reports is dropped.

#include <algorithm>
#include <mutex>

#include "den/platform/text_input.h"
#include "jni_calls.h"

namespace den::pal {

namespace {

class AndroidTextInput final : public TextInput {
public:
    bool Available() const override {
        return true;
    }

    bool Begin(const TextInputRequest& request, TextDisplay display) override {
        End();
        const jni::ActivityCall call;
        const jmethodID method = call.Method("textBegin", "(ILjava/lang/String;Ljava/lang/String;IZ)V");
        if (!method) {
            return false;
        }
        int session = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = session_ = ++last_session_;
            events_.clear();
            covered_ = 0;
        }
        jstring text = call.env->NewStringUTF(request.text.c_str());
        jstring hint = call.env->NewStringUTF(request.placeholder.c_str());
        call.env->CallStaticVoidMethod(call.activity_class, method, static_cast<jint>(session), text, hint,
                                       static_cast<jint>(request.max_length),
                                       static_cast<jboolean>(display == TextDisplay::Second));
        const bool threw = call.Threw();
        call.env->DeleteLocalRef(text);
        call.env->DeleteLocalRef(hint);
        if (threw) {
            std::lock_guard<std::mutex> lock(mutex_);
            session_ = 0;
            return false;
        }
        return true;
    }

    void SetText(const std::string& text) override {
        const int session = Session();
        if (!session) {
            return;
        }
        const jni::ActivityCall call;
        if (const jmethodID method = call.Method("textSet", "(ILjava/lang/String;)V")) {
            jstring value = call.env->NewStringUTF(text.c_str());
            call.env->CallStaticVoidMethod(call.activity_class, method, static_cast<jint>(session), value);
            call.Threw();
            call.env->DeleteLocalRef(value);
        }
    }

    void End() override {
        int session = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = session_;
            session_ = 0;
            events_.clear();
            covered_ = 0;
        }
        if (!session) {
            return;
        }
        const jni::ActivityCall call;
        if (const jmethodID method = call.Method("textEnd", "(I)V")) {
            call.env->CallStaticVoidMethod(call.activity_class, method, static_cast<jint>(session));
            call.Threw();
        }
    }

    bool Active() const override {
        return Session() != 0;
    }

    void Poll(std::vector<TextInputEvent>& out) override {
        std::lock_guard<std::mutex> lock(mutex_);
        out.insert(out.end(), events_.begin(), events_.end());
        events_.clear();
    }

    float CoveredShare() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return covered_;
    }

    // From the UI thread.
    void Report(int session, TextInputEvent::Kind kind, std::string text = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (session == session_ && events_.size() < 256) {
            events_.push_back({kind, std::move(text)});
        }
    }

    void Covered(int session, float share) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (session == session_) {
            covered_ = std::clamp(share, 0.0f, 1.0f);
        }
    }

private:
    int Session() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return session_;
    }

    mutable std::mutex mutex_;
    int session_ = 0, last_session_ = 0;
    float covered_ = 0;
    std::vector<TextInputEvent> events_;
};

AndroidTextInput& Input() {
    static auto* input = new AndroidTextInput();
    return *input;
}

std::string Utf8(JNIEnv* env, jstring text) {
    const char* chars = text ? env->GetStringUTFChars(text, nullptr) : nullptr;
    std::string result = chars ? chars : "";
    if (chars) {
        env->ReleaseStringUTFChars(text, chars);
    }
    return result;
}

} // namespace

TextInput& GetTextInput() {
    return Input();
}

} // namespace den::pal

extern "C" {

using Kind = den::pal::TextInputEvent::Kind;

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_TextBridge_nativeShown(JNIEnv*, jclass, jint session) {
    den::pal::Input().Report(session, Kind::Shown);
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_TextBridge_nativeHidden(JNIEnv*, jclass, jint session) {
    den::pal::Input().Report(session, Kind::Hidden);
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_TextBridge_nativeText(JNIEnv* env, jclass, jint session,
                                                                           jstring text) {
    den::pal::Input().Report(session, Kind::Text, den::pal::Utf8(env, text));
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_TextBridge_nativeDone(JNIEnv*, jclass, jint session) {
    den::pal::Input().Report(session, Kind::Done);
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_TextBridge_nativeCancel(JNIEnv*, jclass, jint session) {
    den::pal::Input().Report(session, Kind::Cancel);
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_TextBridge_nativeCovered(JNIEnv*, jclass, jint session,
                                                                             jfloat share) {
    den::pal::Input().Covered(session, share);
}

} // extern "C"
