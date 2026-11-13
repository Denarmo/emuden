// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The WebView2 backend: the Evergreen runtime's browser in a child window of the app's window.
// webview_win.cpp holds the engine and its events, webview_win_input.cpp the pad's pointer.

#pragma once

#include <windows.h>

#include <objbase.h>
#include <wrl.h>

#include <WebView2.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#include "den/platform/webview.h"

namespace den::pal {

std::wstring WebWide(const std::string& text);
std::string WebNarrow(const wchar_t* text);

/// A string the engine allocated, freed when it goes out of scope.
struct CoString {
    LPWSTR text = nullptr;
    ~CoString() {
        CoTaskMemFree(text);
    }
    std::string Utf8() const {
        return WebNarrow(text);
    }
};

/// The pointer a pad moves: a small window of the app above the page, which clicks pass
/// through. The page itself is never drawn on.
class PointerWindow {
public:
    ~PointerWindow();
    /// `x`, `y` are screen pixels of the pointer's centre.
    void Show(HWND owner, int x, int y);
    void Hide();

private:
    void Paint(int size);
    HWND window_ = nullptr;
    int size_ = 0;
};

class WinWebView final : public WebView {
public:
    WinWebView(HWND parent, const WebRect& bounds, WebViewConfig config);
    ~WinWebView() override;
    /// Starts the engine; the view is ready when its completion handlers have run.
    Status Start();

    void SetCallbacks(WebCallbacks callbacks) override {
        callbacks_ = std::move(callbacks);
    }
    void SetBounds(const WebRect& bounds) override;
    void Show() override;
    void Hide() override;
    bool Visible() const override {
        return visible_;
    }
    void Navigate(const std::string& url) override;
    void Back() override;
    void Forward() override;
    void Reload() override;
    void Stop() override;
    const WebState& State() const override {
        return state_;
    }
    void SetAllowedHosts(std::vector<std::string> hosts) override {
        config_.allowed_hosts = std::move(hosts);
    }
    void CancelDownload(std::uint64_t id) override;

    void SetPointerVisible(bool visible) override;
    void MovePointer(int x, int y) override;
    void Click(int x, int y) override;
    void Scroll(int x, int y, int dx, int dy) override;
    void TypeText(const std::string& text) override;
    void Key(WebKey key) override;
    void SetZoom(float factor) override;
    float Zoom() const override {
        return zoom_;
    }

private:
    using Guard = std::weak_ptr<bool>;

    void OnController(ICoreWebView2Controller* controller);
    void ApplySettings();
    void HookNavigation();
    void HookDownloads();
    void ApplyBounds();
    void UpdateState();
    WebNavigation Describe(const std::wstring& url) const;
    /// Whether a page at `url` may be shown (allowed host, or one the decision callback let in).
    bool Permitted(const std::string& url) const;
    bool Decide(const WebNavigation& navigation);
    void Refuse(const WebNavigation& navigation);
    void UndoForeignPage();
    HRESULT OnDownload(ICoreWebView2DownloadStartingEventArgs* args);
    void FinishDownload(std::uint64_t id, ICoreWebView2DownloadOperation* operation);

    // Input (webview_win_input.cpp).
    void Mouse(const char* type, int x, int y, const std::string& extra);
    void PlacePointer();
    double PixelsPerDip() const;

    HWND parent_ = nullptr;
    WebViewConfig config_;
    WebCallbacks callbacks_;
    WebState state_;
    WebRect bounds_;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> view_;
    std::map<std::uint64_t, Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation>> downloads_;
    std::set<std::string> approved_hosts_; // let in by the decision callback
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::string pending_url_;
    std::uint64_t last_download_ = 0;
    PointerWindow pointer_;
    int pointer_x_ = -1, pointer_y_ = -1;
    float zoom_ = 1.0f;
    bool visible_ = false;
    bool pointer_visible_ = false;
    bool com_started_ = false;
};

} // namespace den::pal
