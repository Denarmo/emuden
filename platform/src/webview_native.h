// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What each platform's web view backend provides (src/win, src/posix).

#pragma once

#include <memory>
#include <string>

#include "den/platform/webview.h"

namespace den::pal {

/// False with the reason filled when the platform has no engine.
bool NativeWebViewAvailable(std::string& reason);
Result<std::unique_ptr<WebView>> CreateNativeWebView(Window& parent, const WebRect& bounds, WebViewConfig config);

} // namespace den::pal
