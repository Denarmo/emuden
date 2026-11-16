// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The parts of the web view every backend shares: address checks and the entry points.

#include <algorithm>
#include <string_view>

#include "den/base/files.h"
#include "den/base/names.h"
#include "webview_native.h"

namespace den::pal {

std::string WebUrlScheme(const std::string& url) {
    const auto colon = url.find(':');
    if (colon == std::string::npos || colon == 0) {
        return {};
    }
    for (std::size_t i = 0; i < colon; ++i) {
        const char c = url[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && !(i > 0 && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))) {
            return {};
        }
    }
    return ToLower(url.substr(0, colon));
}

std::string WebUrlHost(const std::string& url) {
    const std::string scheme = WebUrlScheme(url);
    if (scheme != "http" && scheme != "https") {
        return {};
    }
    std::size_t start = scheme.size() + 1;
    if (url.compare(start, 2, "//") != 0) {
        return {};
    }
    start += 2;
    const std::size_t end = std::min(url.find_first_of("/?#\\", start), url.size());
    std::string authority = url.substr(start, end - start);
    // "user@host" is how a link dresses up as another site: the host is what follows.
    if (const auto at = authority.rfind('@'); at != std::string::npos) {
        authority.erase(0, at + 1);
    }
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        return close == std::string::npos ? std::string() : ToLower(authority.substr(1, close - 1));
    }
    if (const auto colon = authority.find(':'); colon != std::string::npos) {
        authority.erase(colon);
    }
    while (!authority.empty() && authority.back() == '.') {
        authority.pop_back();
    }
    return ToLower(authority);
}

bool WebHostIsLoopback(const std::string& host) {
    return host == "127.0.0.1" || host == "::1" || host == "localhost" || host.ends_with(".localhost");
}

bool WebHostAllowed(const std::string& host, const std::vector<std::string>& allowed) {
    if (host.empty()) {
        return false;
    }
    for (const std::string& raw : allowed) {
        const std::string entry = ToLower(Trim(raw));
        if (entry == "*") {
            return true; // the browser: any site
        }
        if (entry.starts_with("*.")) {
            const std::string name = entry.substr(2);
            if (!name.empty() && (host == name || host.ends_with("." + name))) {
                return true;
            }
        } else if (!entry.empty() && host == entry) {
            return true;
        }
    }
    return false;
}

bool WebUrlAllowed(const std::string& url, const std::vector<std::string>& allowed) {
    const std::string host = WebUrlHost(url);
    if (!WebHostAllowed(host, allowed)) {
        return false;
    }
    const bool any = std::find(allowed.begin(), allowed.end(), "*") != allowed.end();
    return WebUrlScheme(url) == "https" || WebHostIsLoopback(host) || (any && WebUrlScheme(url) == "http");
}

std::string WebSafeFileName(const std::string& name) {
    // Only what follows the last folder or drive.
    const std::size_t last = name.find_last_of("/\\:");
    const std::string_view file =
        last == std::string::npos ? std::string_view(name) : std::string_view(name).substr(last + 1);
    return SafeFileName(file, {.replacement = '\0', .trim_front = ". ", .max_bytes = 0, .fallback = "download"});
}

bool WebView::Available() {
    if (GetEnv("EMUDEN_FAKE_WEBVIEW") == "1") {
        return true;
    }
    std::string reason;
    return NativeWebViewAvailable(reason);
}

std::string WebView::UnavailableReason() {
    if (GetEnv("EMUDEN_FAKE_WEBVIEW") == "1") {
        return {};
    }
    std::string reason;
    return NativeWebViewAvailable(reason) ? std::string() : reason;
}

Result<std::unique_ptr<WebView>> WebView::Create(Window& parent, const WebRect& bounds, WebViewConfig config) {
    if (GetEnv("EMUDEN_FAKE_WEBVIEW") == "1") {
        std::unique_ptr<FakeWebView> fake = CreateFakeWebView(std::move(config));
        fake->SetBounds(bounds);
        return std::unique_ptr<WebView>(std::move(fake));
    }
    return CreateNativeWebView(parent, bounds, std::move(config));
}

} // namespace den::pal
