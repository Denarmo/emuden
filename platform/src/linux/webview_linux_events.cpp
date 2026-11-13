// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Downloads the user starts in the page, and the pad's input. Input goes in as the GDK
// events a mouse and a keyboard produce, handed to GTK the way the X server's would be;
// typed text goes through the engine's editing command "InsertText", the one an input
// method uses. Nothing here asks the page for anything.

#include <algorithm>
#include <cmath>

#include "den/base/files.h"
#include "webview_linux.h"

namespace den::pal {

using namespace gtk;

namespace {

constexpr int POINTER_SIZE = 18;
constexpr double WHEEL_STEP = 40; // pixels the engine scrolls for one unit of a smooth wheel

std::string Text(const char* text) {
    return text ? std::string(text) : std::string();
}

guint KeyValue(WebKey key) {
    switch (key) {
    case WebKey::Enter: return 0xff0d;
    case WebKey::Tab: return 0xff09;
    case WebKey::Backspace: return 0xff08;
    case WebKey::Delete: return 0xffff;
    case WebKey::Escape: return 0xff1b;
    case WebKey::Up: return 0xff52;
    case WebKey::Down: return 0xff54;
    case WebKey::Left: return 0xff51;
    case WebKey::Right: return 0xff53;
    case WebKey::PageUp: return 0xff55;
    case WebKey::PageDown: return 0xff56;
    case WebKey::Home: return 0xff50;
    case WebKey::End: return 0xff57;
    }
    return 0;
}

} // namespace

// Downloads.

void LinuxWebView::OnDownloadStarted(WebKitWebContext*, WebKitDownload* download, void* self) {
    auto* view = static_cast<LinuxWebView*>(self);
    Connect(view->api_, download, "decide-destination", &OnDecideDestination, self);
    Connect(view->api_, download, "received-data", &OnReceived, self);
    Connect(view->api_, download, "finished", &OnFinished, self);
    Connect(view->api_, download, "failed", &OnFailed, self);
}

gboolean LinuxWebView::OnDecideDestination(WebKitDownload* download, const char* suggested, void* self) {
    auto* view = static_cast<LinuxWebView*>(self);
    const Api& api = view->api_;
    WebDownload info;
    info.id = view->next_download_++;
    info.file_name = WebSafeFileName(Text(suggested));
    if (WebKitURIResponse* response = api.webkit_download_get_response(download)) {
        info.url = Text(api.webkit_uri_response_get_uri(response));
        info.mime = Text(api.webkit_uri_response_get_mime_type(response));
        const std::uint64_t length = api.webkit_uri_response_get_content_length(response);
        info.size = length > 0 ? static_cast<std::int64_t>(length) : -1;
    }
    if (info.url.empty()) {
        info.url = Text(api.webkit_uri_request_get_uri(api.webkit_download_get_request(download)));
    }
    info.host = WebUrlHost(info.url);
    const WebDownloadDecision decision =
        view->callbacks_.on_download ? view->callbacks_.on_download(info) : WebDownloadDecision::Refuse();
    if (!decision.accept || decision.target.empty()) {
        api.webkit_download_cancel(download);
        return 1;
    }
    api.webkit_download_set_allow_overwrite(download, 1);
    api.webkit_download_set_destination(download, PathToUtf8(decision.target).c_str());
    view->downloads_[static_cast<WebKitDownload*>(api.g_object_ref(download))] = Arriving{info.id, decision.target};
    return 1;
}

void LinuxWebView::OnReceived(WebKitDownload* download, std::uint64_t, void* self) {
    auto* view = static_cast<LinuxWebView*>(self);
    const auto it = view->downloads_.find(download);
    if (it == view->downloads_.end() || !view->callbacks_.on_download_progress) {
        return;
    }
    std::int64_t total = -1;
    if (WebKitURIResponse* response = view->api_.webkit_download_get_response(download)) {
        const std::uint64_t length = view->api_.webkit_uri_response_get_content_length(response);
        total = length > 0 ? static_cast<std::int64_t>(length) : -1;
    }
    view->callbacks_.on_download_progress(
        it->second.id, static_cast<std::int64_t>(view->api_.webkit_download_get_received_data_length(download)), total);
}

void LinuxWebView::OnFailed(WebKitDownload* download, GError* error, void* self) {
    auto* view = static_cast<LinuxWebView*>(self);
    const auto it = view->downloads_.find(download);
    if (it == view->downloads_.end()) {
        return; // refused here: the engine reports the cancel as a failure
    }
    it->second.failed = true;
    if (view->callbacks_.on_download_failed) {
        view->callbacks_.on_download_failed(it->second.id, error ? Text(error->message) : "the download failed");
    }
}

void LinuxWebView::OnFinished(WebKitDownload* download, void* self) {
    auto* view = static_cast<LinuxWebView*>(self);
    const auto it = view->downloads_.find(download);
    if (it == view->downloads_.end()) {
        return;
    }
    const Arriving item = it->second;
    view->downloads_.erase(it);
    view->api_.g_signal_handlers_disconnect_matched(download, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, self);
    view->api_.g_object_unref(download);
    if (!item.failed && view->callbacks_.on_download_finished) {
        view->callbacks_.on_download_finished(item.id, item.target);
    }
}

void LinuxWebView::CancelDownload(std::uint64_t id) {
    for (auto& [download, item] : downloads_) {
        if (item.id == id) {
            api_.webkit_download_cancel(download); // "failed" and "finished" follow
            return;
        }
    }
}

// Input.

template <typename Event>
Event* LinuxWebView::NewEvent(int type, GdkWindow* window, GdkDevice* device) {
    auto* event = static_cast<Event*>(api_.gdk_event_new(type));
    event->window = static_cast<GdkWindow*>(api_.g_object_ref(window));
    event->send_event = 1;
    event->time = static_cast<std::uint32_t>(api_.g_get_monotonic_time() / 1000);
    if (device) {
        api_.gdk_event_set_device(event, device);
    }
    return event;
}

void LinuxWebView::Send(void* event) {
    injecting_ = true;
    api_.gtk_main_do_event(event);
    injecting_ = false;
    api_.gdk_event_free(event);
}

void LinuxWebView::SendButton(int type, int x, int y, guint state) {
    GdkWindow* window = view_ ? api_.gtk_widget_get_window(view_) : nullptr;
    if (!window) {
        return;
    }
    GdkDevice* device = api_.gdk_seat_get_pointer(api_.gdk_display_get_default_seat(display_));
    int root_x = 0, root_y = 0;
    api_.gdk_window_get_root_coords(window, x, y, &root_x, &root_y);
    if (type == GDK_MOTION_NOTIFY) {
        auto* event = NewEvent<GdkEventMotion>(type, window, device);
        event->x = x;
        event->y = y;
        event->x_root = root_x;
        event->y_root = root_y;
        event->state = state;
        Send(event);
        return;
    }
    auto* event = NewEvent<GdkEventButton>(type, window, device);
    event->x = x;
    event->y = y;
    event->x_root = root_x;
    event->y_root = root_y;
    event->state = state;
    event->button = 1;
    Send(event);
}

void LinuxWebView::MovePointer(int x, int y) {
    const bool moved = x != pointer_x_ || y != pointer_y_;
    pointer_x_ = x;
    pointer_y_ = y;
    if (pointer_ && (moved || !pointer_shown_)) {
        api_.gtk_window_move(pointer_, bounds_.x + x - POINTER_SIZE / 2, bounds_.y + y - POINTER_SIZE / 2);
        if (GdkWindow* window = pointer_shown_ ? api_.gtk_widget_get_window(pointer_) : nullptr) {
            api_.gdk_window_raise(window); // showing the page again puts it on top
        }
    }
    if (moved && visible_) {
        SendButton(GDK_MOTION_NOTIFY, x, y, 0);
    }
}

void LinuxWebView::Click(int x, int y) {
    if (!visible_) {
        return;
    }
    SendButton(GDK_MOTION_NOTIFY, x, y, 0);
    SendButton(GDK_BUTTON_PRESS, x, y, 0);
    SendButton(GDK_BUTTON_RELEASE, x, y, GDK_BUTTON1_MASK);
}

void LinuxWebView::Scroll(int x, int y, int dx, int dy) {
    GdkWindow* window = view_ && visible_ ? api_.gtk_widget_get_window(view_) : nullptr;
    if (!window) {
        return;
    }
    int root_x = 0, root_y = 0;
    api_.gdk_window_get_root_coords(window, x, y, &root_x, &root_y);
    auto* event = NewEvent<GdkEventScroll>(GDK_SCROLL, window,
                                           api_.gdk_seat_get_pointer(api_.gdk_display_get_default_seat(display_)));
    event->x = x;
    event->y = y;
    event->x_root = root_x;
    event->y_root = root_y;
    event->direction = GDK_SCROLL_SMOOTH;
    event->delta_x = dx / WHEEL_STEP;
    event->delta_y = dy / WHEEL_STEP;
    Send(event);
}

void LinuxWebView::TypeText(const std::string& text) {
    if (view_ && !text.empty()) {
        api_.webkit_web_view_execute_editing_command_with_argument(View(), "InsertText", text.c_str());
    }
}

void LinuxWebView::Key(WebKey key) {
    // Key events go to the window, which passes them to the widget with its focus.
    GdkWindow* window = window_ && visible_ ? api_.gtk_widget_get_window(window_) : nullptr;
    const guint value = KeyValue(key);
    if (!window || value == 0) {
        return;
    }
    std::uint16_t code = 0;
    GdkKeymapKey* keys = nullptr;
    int count = 0;
    if (api_.gdk_keymap_get_entries_for_keyval(api_.gdk_keymap_get_for_display(display_), value, &keys, &count)) {
        if (count > 0) {
            code = static_cast<std::uint16_t>(keys[0].keycode);
        }
        api_.g_free(keys);
    }
    GdkDevice* device = api_.gdk_seat_get_keyboard(api_.gdk_display_get_default_seat(display_));
    for (const int type : {GDK_KEY_PRESS, GDK_KEY_RELEASE}) {
        auto* event = NewEvent<GdkEventKey>(type, window, device);
        event->keyval = value;
        event->hardware_keycode = code;
        Send(event);
    }
}

/// A real mouse button went down in the page: the keyboard follows it there, so a keyboard
/// types into the page. It returns to the app's window when the view is hidden.
gboolean LinuxWebView::OnRealPress(GtkWidget*, void*, void* self) {
    auto* view = static_cast<LinuxWebView*>(self);
    GdkWindow* window = view->window_ ? view->api_.gtk_widget_get_window(view->window_) : nullptr;
    if (!view->injecting_ && window) {
        const Api& api = view->api_;
        api.gdk_x11_display_error_trap_push(view->display_);
        api.XSetInputFocus(api.gdk_x11_display_get_xdisplay(view->display_), api.gdk_x11_window_get_xid(window),
                           2 /* RevertToParent */, 0);
        api.gdk_x11_display_error_trap_pop_ignored(view->display_);
        view->has_x_focus_ = true;
    }
    return 0; // the page gets the click
}

// The pad's pointer: a small round window above the page that clicks pass through.

void LinuxWebView::BuildPointer() {
    pointer_ = api_.gtk_window_new(GTK_WINDOW_POPUP);
    api_.gtk_window_set_default_size(pointer_, POINTER_SIZE, POINTER_SIZE);
    api_.gtk_widget_set_app_paintable(pointer_, 1);
    Connect(api_, pointer_, "draw", &OnDrawPointer, this);
    api_.gtk_widget_realize(pointer_);
    GdkWindow* window = api_.gtk_widget_get_window(pointer_);
    api_.gdk_x11_display_error_trap_push(display_);
    api_.gdk_window_reparent(window, parent_, bounds_.x, bounds_.y);
    api_.gdk_display_sync(display_);
    api_.gdk_x11_display_error_trap_pop_ignored(display_);
    // Round, and no part of it takes input.
    cairo_region_t* round = api_.cairo_region_create();
    const double radius = POINTER_SIZE / 2.0;
    for (int row = 0; row < POINTER_SIZE; ++row) {
        const double dy = row + 0.5 - radius;
        const int half = static_cast<int>(std::lround(std::sqrt(std::max(0.0, radius * radius - dy * dy))));
        const cairo_rectangle_int_t line{static_cast<int>(radius) - half, row, 2 * half, 1};
        api_.cairo_region_union_rectangle(round, &line);
    }
    api_.gdk_window_shape_combine_region(window, round, 0, 0);
    api_.cairo_region_destroy(round);
    cairo_region_t* nothing = api_.cairo_region_create();
    api_.gdk_window_input_shape_combine_region(window, nothing, 0, 0);
    api_.cairo_region_destroy(nothing);
}

gboolean LinuxWebView::OnDrawPointer(GtkWidget*, cairo_t* cairo, void* self) {
    const Api& api = static_cast<LinuxWebView*>(self)->api_;
    const double radius = POINTER_SIZE / 2.0;
    api.cairo_set_source_rgb(cairo, 0.05, 0.05, 0.05); // a dark rim, so it shows on white pages
    api.cairo_paint(cairo);
    api.cairo_set_source_rgb(cairo, 1, 1, 1);
    api.cairo_arc(cairo, radius, radius, radius - 3, 0, 6.283185307);
    api.cairo_fill(cairo);
    return 1;
}

void LinuxWebView::SetPointerVisible(bool visible) {
    pointer_visible_ = visible;
    const bool show = visible && visible_;
    if (!pointer_ || show == pointer_shown_) {
        return;
    }
    pointer_shown_ = show;
    if (show) {
        api_.gtk_window_move(pointer_, bounds_.x + pointer_x_ - POINTER_SIZE / 2,
                             bounds_.y + pointer_y_ - POINTER_SIZE / 2);
        api_.gtk_widget_show_all(pointer_);
        if (GdkWindow* window = api_.gtk_widget_get_window(pointer_)) {
            api_.gdk_window_raise(window);
        }
    } else {
        api_.gtk_widget_hide(pointer_);
    }
}

} // namespace den::pal
