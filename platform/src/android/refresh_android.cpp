// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Refresh rates on Android: the main display's modes at its current size, and the window's
// preferred display mode (EmuDenActivity.displayRefreshRates, displayRequestRefresh).

#include <jni.h>

#include "den/platform/refresh.h"
#include "jni_calls.h"

namespace den::pal {

RefreshRates MainRefreshRates(Window&) {
    RefreshRates rates;
    const jni::ActivityCall call;
    const jmethodID method = call.Method("displayRefreshRates", "()[F");
    if (!method) {
        return rates;
    }
    auto array = static_cast<jfloatArray>(call.env->CallStaticObjectMethod(call.activity_class, method));
    if (call.Threw() || !array) {
        return rates;
    }
    const jsize count = call.env->GetArrayLength(array);
    if (count > 0) {
        std::vector<float> values(static_cast<std::size_t>(count));
        call.env->GetFloatArrayRegion(array, 0, count, values.data());
        // The first is the rate it runs at, the others every rate it offers.
        rates.current = values.front();
        rates.available.assign(values.begin() + 1, values.end());
    }
    call.env->DeleteLocalRef(array);
    return rates;
}

void RequestMainRefresh(Window&, float hz) {
    const jni::ActivityCall call;
    if (const jmethodID method = call.Method("displayRequestRefresh", "(F)V")) {
        call.env->CallStaticVoidMethod(call.activity_class, method, static_cast<jfloat>(hz));
        call.Threw();
    }
}

} // namespace den::pal
