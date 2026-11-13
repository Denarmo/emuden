// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// WebKitGTK inside the app's window. GTK runs on the app's main thread: its pending work is
// done from SDL's event pump, so the engine's signals arrive where every other event does.
//
// Navigation is decided twice. A link the user clicks to a host outside the list is refused
// before any request. Everything else is decided on the server's answer for the top-level
// document: a file becomes a download, a page on a host outside the list is dropped before
// it shows. Frames and the files a page loads are the site's own business.

#include <algorithm>
#include <atomic>

#include <SDL3/SDL.h>

#include "den/base/files.h"
#include "den/base/log.h"
#include "webview_linux.h"

namespace den::pal {

namespace {

using namespace gtk;

std::atomic<bool> g_pump_queued{false};
SDL_TimerID g_pump_timer = 0;
int g_views = 0;
bool g_gtk_started = false;

void SDLCALL PumpOnMain(void*) {
    g_pump_queued = false;
    std::string reason;
    if (const Api* api = Load(reason); api && g_views > 0) {
        PumpGtk(*api);
    }
}

Uint32 SDLCALL PumpTick(void*, SDL_TimerID, Uint32 interval) {
    if (!g_pump_queued.exchange(true)) {
        SDL_RunOnMainThread(PumpOnMain, nullptr, false);
    }
    return interval;
}

/// Starts GTK once, on X11 only and without touching the app's locale or scale.
Status StartGtk(const Api& api) {
    if (g_gtk_started) {
        return {};
    }
    api.gdk_set_allowed_backends("x11");
    api.gtk_disable_setlocale();
    // The app works in pixels; the page's size comes from the zoom level instead.
    api.g_setenv("GDK_SCALE", "1", 1);
    if (!api.gtk_init_check(nullptr, nullptr)) {
        return Status::Fail("The web engine could not start (GTK found no X11 display).");
    }
    g_gtk_started = true;
    return {};
}

std::string Text(const char* text) {
    return text ? std::string(text) : std::string();
}

} // namespace

void PumpGtk(const Api& api) {
    for (int i = 0; i < 200 && api.g_main_context_pending(nullptr); ++i) {
        api.g_main_context_iteration(nullptr, 0);
    }
}

LinuxWebView::LinuxWebView(const Api& api, WebViewConfig config, float display_scale)
    : api_(api), config_(std::move(config)), base_zoom_(display_scale > 0 ? display_scale : 1) {
    if (++g_views == 1) {
        g_pump_timer = SDL_AddTimer(8, PumpTick, nullptr);
    }
}

LinuxWebView::~LinuxWebView() {
    for (auto& [download, item] : downloads_) {
        api_.g_signal_handlers_disconnect_matched(download, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);
        api_.webkit_download_cancel(download);
        api_.g_object_unref(download);
    }
    downloads_.clear();
    if (has_x_focus_ && display_) {
        Hide();
    }
    if (context_) {
        api_.g_signal_handlers_disconnect_matched(context_, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);
    }
    if (view_) {
        api_.g_signal_handlers_disconnect_matched(view_, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);
    }
    if (pointer_) {
        api_.g_signal_handlers_disconnect_matched(pointer_, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);
        api_.gtk_widget_destroy(pointer_);
    }
    if (window_) {
        api_.gtk_widget_destroy(window_); // takes the engine's widget with it
    }
    if (context_) {
        api_.g_object_unref(context_);
    }
    if (parent_) {
        api_.g_object_unref(parent_);
    }
    if (g_gtk_started) {
        PumpGtk(api_); // the windows go now, not at some later frame
    }
    if (--g_views == 0 && g_pump_timer) {
        SDL_RemoveTimer(g_pump_timer);
        g_pump_timer = 0;
    }
}

Status LinuxWebView::Init(unsigned long parent_xid, const WebRect& bounds) {
    if (Status started = StartGtk(api_); !started.ok) {
        return started;
    }
    parent_xid_ = parent_xid;
    bounds_ = bounds;
    display_ = api_.gdk_display_get_default();
    parent_ = api_.gdk_x11_window_foreign_new_for_display(display_, parent_xid);
    if (!parent_) {
        return Status::Fail("The web engine could not find the app's window.");
    }

    const std::string data = PathToUtf8(config_.user_data_dir);
    WebKitWebsiteDataManager* manager =
        data.empty() ? api_.webkit_website_data_manager_new(nullptr)
                     : api_.webkit_website_data_manager_new("base-data-directory", data.c_str(),
                                                            "base-cache-directory", (data + "/cache").c_str(),
                                                            static_cast<const char*>(nullptr));
    context_ = api_.webkit_web_context_new_with_website_data_manager(manager);
    api_.g_object_unref(manager);
    view_ = api_.webkit_web_view_new_with_context(context_);
    if (!view_) {
        return Status::Fail("The web engine could not make a view.");
    }
    api_.webkit_settings_set_enable_developer_extras(api_.webkit_web_view_get_settings(View()), 0);
    api_.webkit_web_view_set_zoom_level(View(), base_zoom_);

    Connect(api_, view_, "decide-policy", &OnDecidePolicy, this);
    Connect(api_, view_, "notify::uri", &OnNotify, this);
    Connect(api_, view_, "notify::title", &OnNotify, this);
    Connect(api_, view_, "notify::is-loading", &OnNotify, this);
    Connect(api_, view_, "load-changed", &OnLoadChanged, this);
    Connect(api_, view_, "permission-request", &OnPermission, this);
    // No menu, dialogs, file pickers, notifications, printing or full screen of the engine's
    // own: each would be a window outside the app's.
    for (const char* signal : {"context-menu", "script-dialog", "run-file-chooser", "show-notification", "print",
                               "enter-fullscreen", "run-color-chooser"}) {
        Connect(api_, view_, signal, &OnSwallow, this);
    }
    Connect(api_, view_, "button-press-event", &OnRealPress, this);
    Connect(api_, context_, "download-started", &OnDownloadStarted, this);

    // A window without a frame that the window manager leaves alone, moved into the app's
    // window: from then on its place is counted from the app window's corner.
    window_ = api_.gtk_window_new(GTK_WINDOW_POPUP);
    api_.gtk_window_set_default_size(window_, std::max(bounds.w, 1), std::max(bounds.h, 1));
    api_.gtk_container_add(window_, view_);
    api_.gtk_widget_realize(window_);
    api_.gdk_x11_display_error_trap_push(display_);
    api_.gdk_window_reparent(api_.gtk_widget_get_window(window_), parent_, bounds.x, bounds.y);
    api_.gdk_display_sync(display_);
    api_.gdk_x11_display_error_trap_pop_ignored(display_);
    BuildPointer();

    state_.ready = true;
    PublishState();
    log::Info("web", "web engine: {}", api_.library);
    return {};
}

void LinuxWebView::SetCallbacks(WebCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
    PublishState();
}

void LinuxWebView::Place() {
    if (!window_ || bounds_.w <= 0 || bounds_.h <= 0) {
        return;
    }
    api_.gtk_window_move(window_, bounds_.x, bounds_.y);
    api_.gtk_window_resize(window_, bounds_.w, bounds_.h);
    if (GdkWindow* window = api_.gtk_widget_get_window(window_)) {
        api_.gdk_window_move_resize(window, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
    }
}

void LinuxWebView::SetBounds(const WebRect& bounds) {
    if (bounds == bounds_) {
        return;
    }
    bounds_ = bounds;
    Place();
    MovePointer(pointer_x_, pointer_y_);
}

void LinuxWebView::Show() {
    if (visible_ || !window_) {
        return;
    }
    visible_ = true;
    api_.gtk_widget_show_all(window_);
    Place();
    api_.gtk_widget_grab_focus(view_);
    SetActive(true);
    SetPointerVisible(pointer_visible_);
}

void LinuxWebView::Hide() {
    if (window_ && visible_) {
        visible_ = false;
        SetActive(false);
        api_.gtk_widget_hide(window_);
        if (pointer_) {
            api_.gtk_widget_hide(pointer_);
            pointer_shown_ = false;
        }
    }
    // The keyboard goes back to the app's window if a click in the page took it.
    if (has_x_focus_) {
        has_x_focus_ = false;
        api_.gdk_x11_display_error_trap_push(display_);
        api_.XSetInputFocus(api_.gdk_x11_display_get_xdisplay(display_), parent_xid_, 2 /* RevertToParent */, 0);
        api_.gdk_display_sync(display_);
        api_.gdk_x11_display_error_trap_pop_ignored(display_);
    }
}

/// Tells GTK the page's window is the active one. The X server's focus stays with the app's
/// window (the pad needs it there), so GTK would otherwise show the page as unfocused.
void LinuxWebView::SetActive(bool active) {
    GdkWindow* window = window_ ? api_.gtk_widget_get_window(window_) : nullptr;
    if (!window) {
        return;
    }
    auto* event = static_cast<GdkEventFocus*>(api_.gdk_event_new(GDK_FOCUS_CHANGE));
    event->window = static_cast<GdkWindow*>(api_.g_object_ref(window));
    event->send_event = 1;
    event->in = active ? 1 : 0;
    api_.gdk_event_set_device(event, api_.gdk_seat_get_keyboard(api_.gdk_display_get_default_seat(display_)));
    Send(event);
}

bool LinuxWebView::Allow(WebNavigation& navigation) const {
    navigation.host = WebUrlHost(navigation.url);
    navigation.host_allowed = WebUrlAllowed(navigation.url, config_.allowed_hosts);
    return callbacks_.on_navigate ? callbacks_.on_navigate(navigation) : navigation.host_allowed;
}

void LinuxWebView::Refuse(const WebNavigation& navigation) {
    if (callbacks_.on_refused) {
        callbacks_.on_refused(navigation);
    }
}

void LinuxWebView::Navigate(const std::string& url) {
    WebNavigation navigation;
    navigation.url = url;
    if (!Allow(navigation)) {
        Refuse(navigation);
        return;
    }
    if (view_) {
        api_.webkit_web_view_load_uri(View(), url.c_str());
    }
}

void LinuxWebView::Back() {
    if (view_ && state_.can_go_back) {
        api_.webkit_web_view_go_back(View());
    }
}

void LinuxWebView::Forward() {
    if (view_ && state_.can_go_forward) {
        api_.webkit_web_view_go_forward(View());
    }
}

void LinuxWebView::Reload() {
    if (view_) {
        api_.webkit_web_view_reload(View());
    }
}

void LinuxWebView::Stop() {
    if (view_) {
        api_.webkit_web_view_stop_loading(View());
    }
}

void LinuxWebView::SetAllowedHosts(std::vector<std::string> hosts) {
    config_.allowed_hosts = std::move(hosts);
}

void LinuxWebView::SetZoom(float factor) {
    zoom_ = std::clamp(factor, 0.25f, 5.0f);
    if (view_) {
        api_.webkit_web_view_set_zoom_level(View(), static_cast<double>(zoom_ * base_zoom_));
    }
}

void LinuxWebView::PublishState() {
    if (view_) {
        state_.url = Text(api_.webkit_web_view_get_uri(View()));
        state_.title = Text(api_.webkit_web_view_get_title(View()));
        state_.loading = api_.webkit_web_view_is_loading(View()) != 0;
        state_.can_go_back = api_.webkit_web_view_can_go_back(View()) != 0;
        state_.can_go_forward = api_.webkit_web_view_can_go_forward(View()) != 0;
    }
    if (callbacks_.on_state) {
        callbacks_.on_state(state_);
    }
}

void LinuxWebView::OnNotify(void*, void*, void* self) {
    static_cast<LinuxWebView*>(self)->PublishState();
}

void LinuxWebView::OnLoadChanged(WebKitWebView*, int, void* self) {
    static_cast<LinuxWebView*>(self)->PublishState();
}

gboolean LinuxWebView::OnPermission(WebKitWebView*, void* request, void* self) {
    static_cast<LinuxWebView*>(self)->api_.webkit_permission_request_deny(request);
    return 1;
}

gboolean LinuxWebView::OnSwallow(void*, void*, void*) {
    return 1; // handled: the engine shows nothing of its own
}

gboolean LinuxWebView::OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision, int type, void* self) {
    auto* view = static_cast<LinuxWebView*>(self);
    if (type == POLICY_RESPONSE) {
        return view->DecideResponse(decision) ? 1 : 0;
    }
    return view->DecideNavigation(decision, type == POLICY_NEW_WINDOW_ACTION) ? 1 : 0;
}

/// True when the decision was made here; false leaves it to the engine (which goes ahead).
bool LinuxWebView::DecideNavigation(WebKitPolicyDecision* decision, bool new_window) {
    WebKitNavigationAction* action = api_.webkit_navigation_policy_decision_get_navigation_action(decision);
    WebNavigation navigation;
    navigation.url = Text(api_.webkit_uri_request_get_uri(api_.webkit_navigation_action_get_request(action)));
    navigation.user_initiated = api_.webkit_navigation_action_is_user_gesture(action) != 0;
    navigation.redirected = api_.webkit_navigation_action_is_redirect(action) != 0;
    navigation.new_window = new_window;
    const std::string scheme = WebUrlScheme(navigation.url);
    const bool web = scheme == "http" || scheme == "https";
    if (new_window) {
        // There is one view: a link that asks for a window opens here, under the same rule.
        api_.webkit_policy_decision_ignore(decision);
        if (web && Allow(navigation)) {
            api_.webkit_web_view_load_uri(View(), navigation.url.c_str());
        } else {
            Refuse(navigation);
        }
        return true;
    }
    if (!web) {
        // about:, data: and blob: are documents the page makes for its own frames; anything
        // else (mailto:, an app's own scheme) would leave the app.
        if (scheme == "about" || scheme == "data" || scheme == "blob") {
            return false;
        }
        api_.webkit_policy_decision_ignore(decision);
        Refuse(navigation);
        return true;
    }
    const bool clicked = api_.webkit_navigation_action_get_navigation_type(action) == NAVIGATION_LINK_CLICKED;
    if (clicked && !navigation.redirected && !Allow(navigation)) {
        api_.webkit_policy_decision_ignore(decision);
        Refuse(navigation);
        return true;
    }
    return false; // decided on the server's answer
}

bool LinuxWebView::DecideResponse(WebKitPolicyDecision* decision) {
    if (!api_.webkit_response_policy_decision_is_main_frame_main_resource(decision)) {
        return false;
    }
    WebKitURIResponse* response = api_.webkit_response_policy_decision_get_response(decision);
    // A file: what a browser would hand to its download manager.
    bool attachment = !api_.webkit_response_policy_decision_is_mime_type_supported(decision);
    if (SoupMessageHeaders* headers = api_.webkit_uri_response_get_http_headers(response); headers && !attachment) {
        char* disposition = nullptr;
        GHashTable* params = nullptr;
        if (api_.soup_message_headers_get_content_disposition(headers, &disposition, &params)) {
            attachment = ToLower(Text(disposition)) == "attachment";
            api_.g_free(disposition);
            if (params) {
                api_.g_hash_table_destroy(params);
            }
        }
    }
    if (attachment) {
        api_.webkit_policy_decision_download(decision);
        return true;
    }
    WebNavigation navigation;
    navigation.url = Text(api_.webkit_uri_response_get_uri(response));
    navigation.redirected = navigation.url != state_.url;
    if (!Allow(navigation)) {
        api_.webkit_policy_decision_ignore(decision);
        Refuse(navigation);
        return true;
    }
    api_.webkit_policy_decision_use(decision);
    return true;
}

bool NativeWebViewAvailable(std::string& reason) {
    if (!gtk::Load(reason)) {
        return false;
    }
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::string(driver) != "x11") {
        reason = std::string("The in-app browser needs an X11 session; this one is ") + driver + ".";
        return false;
    }
    return true;
}

Result<std::unique_ptr<WebView>> CreateNativeWebView(Window& parent, const WebRect& bounds, WebViewConfig config) {
    std::string reason;
    if (!NativeWebViewAvailable(reason)) {
        return Status::Fail(reason);
    }
    const gtk::Api* api = gtk::Load(reason);
    auto* window = static_cast<SDL_Window*>(parent.SdlWindow());
    const auto xid = static_cast<unsigned long>(
        SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    if (xid == 0) {
        return Status::Fail("The in-app browser needs an X11 window.");
    }
    auto view = std::make_unique<LinuxWebView>(*api, std::move(config), parent.DisplayScale());
    if (Status status = view->Init(xid, bounds); !status.ok) {
        return status;
    }
    return std::unique_ptr<WebView>(std::move(view));
}

} // namespace den::pal
