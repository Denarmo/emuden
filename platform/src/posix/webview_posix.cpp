// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Linux, macOS and Android have no web view backend yet (WebKitGTK, WKWebView and
// android.webkit.WebView are the engines to wrap). Until then the page says so.

#include "../webview_native.h"

namespace den::pal {

bool NativeWebViewAvailable(std::string& reason) {
    reason = "The in-app browser is not available on this platform yet.";
    return false;
}

Result<std::unique_ptr<WebView>> CreateNativeWebView(Window&, const WebRect&, WebViewConfig) {
    std::string reason;
    NativeWebViewAvailable(reason);
    return Status::Fail(reason);
}

} // namespace den::pal
