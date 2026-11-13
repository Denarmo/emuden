// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "webview_win.h"

#include <objbase.h>

#include <algorithm>
#include <filesystem>
#include <format>

#include <SDL3/SDL.h>

#include "../webview_native.h"
#include "den/base/files.h"
#include "den/base/log.h"

namespace den::pal {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

std::wstring WebWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, out.data(), size);
    return out;
}

std::string WebNarrow(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

bool NativeWebViewAvailable(std::string& reason) {
    CoString version;
    if (SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &version.text)) && version.text) {
        return true;
    }
    reason = "The Microsoft Edge WebView2 Runtime is not installed. It is part of Windows 11; on "
             "older systems install it from Microsoft (\"WebView2 Runtime\", Evergreen).";
    return false;
}

Result<std::unique_ptr<WebView>> CreateNativeWebView(Window& parent, const WebRect& bounds, WebViewConfig config) {
    std::string reason;
    if (!NativeWebViewAvailable(reason)) {
        return Status::Fail(reason);
    }
    auto* window = static_cast<SDL_Window*>(parent.SdlWindow());
    const HWND hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) {
        return Status::Fail("The app's window has no native handle for the browser.");
    }
    auto view = std::make_unique<WinWebView>(hwnd, bounds, std::move(config));
    if (const Status started = view->Start(); !started) {
        return started;
    }
    return std::unique_ptr<WebView>(std::move(view));
}

WinWebView::WinWebView(HWND parent, const WebRect& bounds, WebViewConfig config)
    : parent_(parent), config_(std::move(config)), bounds_(bounds) {}

WinWebView::~WinWebView() {
    alive_.reset();
    pointer_.Hide();
    for (auto& [id, operation] : downloads_) {
        operation->Cancel();
    }
    downloads_.clear();
    if (controller_) {
        controller_->Close();
    }
    view_.Reset();
    controller_.Reset();
    environment_.Reset();
    if (com_started_) {
        CoUninitialize();
    }
}

Status WinWebView::Start() {
    // The engine needs a single-threaded apartment on the thread that pumps the window.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    com_started_ = SUCCEEDED(com);
    if (com == RPC_E_CHANGED_MODE) {
        return Status::Fail("The browser engine cannot start on this thread (COM is multi-threaded here).");
    }
    std::error_code ec;
    std::filesystem::create_directories(config_.user_data_dir, ec);
    const Guard guard = alive_;
    const HRESULT started = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, config_.user_data_dir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, guard](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (guard.expired()) {
                    return S_OK;
                }
                if (FAILED(result) || !environment) {
                    log::Error("webview", "the engine did not start (0x{:08x})", static_cast<unsigned>(result));
                    return S_OK;
                }
                environment_ = environment;
                return environment->CreateCoreWebView2Controller(
                    parent_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                 [this, guard](HRESULT made, ICoreWebView2Controller* controller) -> HRESULT {
                                     if (guard.expired()) {
                                         return S_OK;
                                     }
                                     if (FAILED(made) || !controller) {
                                         log::Error("webview", "no browser view (0x{:08x})",
                                                    static_cast<unsigned>(made));
                                         return S_OK;
                                     }
                                     OnController(controller);
                                     return S_OK;
                                 })
                                 .Get());
            })
            .Get());
    if (FAILED(started)) {
        return Status::Fail(std::format("The browser engine did not start (0x{:08x}).", static_cast<unsigned>(started)));
    }
    return {};
}

void WinWebView::OnController(ICoreWebView2Controller* controller) {
    controller_ = controller;
    controller_->get_CoreWebView2(&view_);
    if (!view_) {
        return;
    }
    ApplySettings();
    HookNavigation();
    HookDownloads();
    ApplyBounds();
    controller_->put_ZoomFactor(zoom_);
    controller_->put_IsVisible(visible_ ? TRUE : FALSE);
    state_.ready = true;
    log::Info("webview", "the browser view is ready");
    if (!pending_url_.empty()) {
        const std::string url = std::move(pending_url_);
        pending_url_.clear();
        Navigate(url);
    } else {
        UpdateState();
    }
}

// The user agent stays the engine's own. Everything that would let the app and the page talk
// to each other is off, as is the engine's interface around the page.
void WinWebView::ApplySettings() {
    ComPtr<ICoreWebView2Settings> settings;
    view_->get_Settings(&settings);
    if (!settings) {
        return;
    }
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_AreDefaultContextMenusEnabled(FALSE);
    settings->put_AreHostObjectsAllowed(FALSE);
    settings->put_IsWebMessageEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    ComPtr<ICoreWebView2Settings3> settings3;
    if (SUCCEEDED(settings.As(&settings3))) {
        settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    }
    ComPtr<ICoreWebView2Settings4> settings4;
    if (SUCCEEDED(settings.As(&settings4))) {
        settings4->put_IsPasswordAutosaveEnabled(FALSE);
        settings4->put_IsGeneralAutofillEnabled(FALSE);
    }
}

WebNavigation WinWebView::Describe(const std::wstring& url) const {
    WebNavigation navigation;
    navigation.url = WebNarrow(url.c_str());
    navigation.host = WebUrlHost(navigation.url);
    navigation.host_allowed = WebUrlAllowed(navigation.url, config_.allowed_hosts);
    return navigation;
}

bool WinWebView::Permitted(const std::string& url) const {
    return WebUrlAllowed(url, config_.allowed_hosts) || approved_hosts_.count(WebUrlHost(url)) != 0;
}

bool WinWebView::Decide(const WebNavigation& navigation) {
    if (!callbacks_.on_navigate) {
        return navigation.host_allowed;
    }
    const bool allowed = callbacks_.on_navigate(navigation);
    if (allowed && !navigation.host_allowed && !navigation.host.empty()) {
        approved_hosts_.insert(navigation.host);
    }
    return allowed && !navigation.host.empty();
}

void WinWebView::Refuse(const WebNavigation& navigation) {
    log::Info("webview", "refused a navigation to '{}'", navigation.host);
    if (callbacks_.on_refused) {
        callbacks_.on_refused(navigation);
    }
}

// A redirect ended on a page of a host that is not allowed: it is not shown.
void WinWebView::UndoForeignPage() {
    view_->Stop();
    BOOL back = FALSE;
    view_->get_CanGoBack(&back);
    if (back) {
        view_->GoBack();
    } else {
        view_->Navigate(L"about:blank");
    }
}

void WinWebView::HookNavigation() {
    const Guard guard = alive_;
    EventRegistrationToken token{};
    view_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this, guard](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                if (guard.expired()) {
                    return S_OK;
                }
                CoString uri;
                args->get_Uri(&uri.text);
                const std::wstring url = uri.text ? uri.text : L"";
                if (url == L"about:blank") {
                    return S_OK;
                }
                WebNavigation navigation = Describe(url);
                BOOL flag = FALSE;
                args->get_IsUserInitiated(&flag);
                navigation.user_initiated = flag != FALSE;
                args->get_IsRedirected(&flag);
                navigation.redirected = flag != FALSE;
                if (Decide(navigation)) {
                    state_.loading = true;
                    UpdateState();
                    return S_OK;
                }
                // A redirect to another host may be where a download comes from: the request
                // goes on, and a page that arrives instead is undone when it starts to load.
                if (navigation.redirected && !navigation.host.empty()) {
                    return S_OK;
                }
                args->put_Cancel(TRUE);
                Refuse(navigation);
                return S_OK;
            })
            .Get(),
        &token);
    view_->add_ContentLoading(
        Callback<ICoreWebView2ContentLoadingEventHandler>(
            [this, guard](ICoreWebView2*, ICoreWebView2ContentLoadingEventArgs*) -> HRESULT {
                if (guard.expired()) {
                    return S_OK;
                }
                CoString source;
                view_->get_Source(&source.text);
                const std::wstring url = source.text ? source.text : L"";
                if (url != L"about:blank" && !url.empty() && !Permitted(WebNarrow(url.c_str()))) {
                    WebNavigation navigation = Describe(url);
                    navigation.redirected = true;
                    UndoForeignPage();
                    Refuse(navigation);
                }
                return S_OK;
            })
            .Get(),
        &token);
    view_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, guard](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (!guard.expired()) {
                    state_.loading = false;
                    UpdateState();
                }
                return S_OK;
            })
            .Get(),
        &token);
    view_->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
                                 [this, guard](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                                     if (!guard.expired()) {
                                         UpdateState();
                                     }
                                     return S_OK;
                                 })
                                 .Get(),
                             &token);
    view_->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>(
                                  [this, guard](ICoreWebView2*, IUnknown*) -> HRESULT {
                                      if (!guard.expired()) {
                                          UpdateState();
                                      }
                                      return S_OK;
                                  })
                                  .Get(),
                              &token);
    view_->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                                        [this, guard](ICoreWebView2*, IUnknown*) -> HRESULT {
                                            if (!guard.expired()) {
                                                UpdateState();
                                            }
                                            return S_OK;
                                        })
                                        .Get(),
                                    &token);
    // A link that asks for a new window opens here, under the same rules.
    view_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this, guard](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                if (guard.expired()) {
                    return S_OK;
                }
                args->put_Handled(TRUE);
                CoString uri;
                args->get_Uri(&uri.text);
                const std::wstring url = uri.text ? uri.text : L"";
                WebNavigation navigation = Describe(url);
                navigation.new_window = true;
                BOOL user = FALSE;
                args->get_IsUserInitiated(&user);
                navigation.user_initiated = user != FALSE;
                if (Decide(navigation)) {
                    view_->Navigate(url.c_str());
                } else {
                    Refuse(navigation);
                }
                return S_OK;
            })
            .Get(),
        &token);
    view_->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                return S_OK;
            })
            .Get(),
        &token);
}

void WinWebView::UpdateState() {
    if (!view_) {
        return;
    }
    CoString source, title;
    view_->get_Source(&source.text);
    view_->get_DocumentTitle(&title.text);
    BOOL back = FALSE, forward = FALSE;
    view_->get_CanGoBack(&back);
    view_->get_CanGoForward(&forward);
    state_.url = source.Utf8();
    state_.title = title.Utf8();
    state_.can_go_back = back != FALSE;
    state_.can_go_forward = forward != FALSE;
    if (callbacks_.on_state) {
        callbacks_.on_state(state_);
    }
}

void WinWebView::ApplyBounds() {
    if (controller_) {
        const RECT rect{bounds_.x, bounds_.y, bounds_.x + bounds_.w, bounds_.y + bounds_.h};
        controller_->put_Bounds(rect);
    }
    PlacePointer();
}

void WinWebView::SetBounds(const WebRect& bounds) {
    if (bounds != bounds_) {
        bounds_ = bounds;
        ApplyBounds();
    }
}

void WinWebView::Show() {
    visible_ = true;
    if (controller_) {
        controller_->put_IsVisible(TRUE);
    }
    PlacePointer();
}

void WinWebView::Hide() {
    visible_ = false;
    if (controller_) {
        controller_->put_IsVisible(FALSE);
    }
    // The keyboard goes back to the app, whose own interface is what shows now.
    if (GetFocus() != parent_ && GetForegroundWindow() == parent_) {
        SetFocus(parent_);
    }
    PlacePointer();
}

void WinWebView::Navigate(const std::string& url) {
    if (!view_) {
        pending_url_ = url;
        return;
    }
    // The filter runs in NavigationStarting; an address the engine cannot parse ends here.
    if (FAILED(view_->Navigate(WebWide(url).c_str()))) {
        WebNavigation navigation;
        navigation.url = url;
        navigation.host = WebUrlHost(url);
        Refuse(navigation);
    }
}

void WinWebView::Back() {
    if (view_) {
        view_->GoBack();
    }
}

void WinWebView::Forward() {
    if (view_) {
        view_->GoForward();
    }
}

void WinWebView::Reload() {
    if (view_) {
        view_->Reload();
    }
}

void WinWebView::Stop() {
    if (view_) {
        view_->Stop();
    }
}

void WinWebView::SetZoom(float factor) {
    zoom_ = std::clamp(factor, 0.25f, 4.0f);
    if (controller_) {
        controller_->put_ZoomFactor(zoom_);
    }
}

} // namespace den::pal
