// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// WebKitGTK and the GTK 3 it brings, found at run time. The app never links against them:
// SteamOS and many other systems do not ship WebKitGTK, and the AppImage must start there.
// The declarations below are the part of their C interface the web view uses, written out
// here so a build needs none of their headers.

#pragma once

#include <cstdint>
#include <string>

namespace den::pal::gtk {

// The C types under GLib's names, so the declarations read like the originals.
using gboolean = int;
using gpointer = void*;
using gulong = unsigned long;
using guint = unsigned int;

// Opaque objects.
struct GtkWidget;
struct GdkWindow;
struct GdkDisplay;
struct GdkDevice;
struct GdkSeat;
struct GdkKeymap;
struct GMainContext;
struct GHashTable;
struct cairo_t;
struct cairo_region_t;
struct WebKitWebView;
struct WebKitWebContext;
struct WebKitWebsiteDataManager;
struct WebKitSettings;
struct WebKitPolicyDecision;
struct WebKitNavigationAction;
struct WebKitURIRequest;
struct WebKitURIResponse;
struct WebKitDownload;
struct SoupMessageHeaders;
struct XDisplay;

struct GError {
    std::uint32_t domain;
    int code;
    char* message;
};
struct GdkKeymapKey {
    guint keycode;
    int group;
    int level;
};
struct cairo_rectangle_int_t {
    int x, y, width, height;
};

// GdkEvent variants as GTK 3 lays them out (gdkevents.h; fixed for the life of GTK 3).
enum : int {
    GDK_MOTION_NOTIFY = 3,
    GDK_BUTTON_PRESS = 4,
    GDK_BUTTON_RELEASE = 7,
    GDK_KEY_PRESS = 8,
    GDK_KEY_RELEASE = 9,
    GDK_FOCUS_CHANGE = 12,
    GDK_SCROLL = 31,
};
constexpr int GDK_SCROLL_SMOOTH = 4;
constexpr guint GDK_BUTTON1_MASK = 1u << 8;

struct GdkEventMotion {
    int type;
    GdkWindow* window;
    std::int8_t send_event;
    std::uint32_t time;
    double x, y;
    double* axes;
    guint state;
    std::int16_t is_hint;
    GdkDevice* device;
    double x_root, y_root;
};
struct GdkEventButton {
    int type;
    GdkWindow* window;
    std::int8_t send_event;
    std::uint32_t time;
    double x, y;
    double* axes;
    guint state;
    guint button;
    GdkDevice* device;
    double x_root, y_root;
};
struct GdkEventScroll {
    int type;
    GdkWindow* window;
    std::int8_t send_event;
    std::uint32_t time;
    double x, y;
    guint state;
    int direction;
    GdkDevice* device;
    double x_root, y_root;
    double delta_x, delta_y;
    guint is_stop : 1;
};
struct GdkEventKey {
    int type;
    GdkWindow* window;
    std::int8_t send_event;
    std::uint32_t time;
    guint state;
    guint keyval;
    int length;
    char* string;
    std::uint16_t hardware_keycode;
    std::uint8_t group;
    guint is_modifier : 1;
};
struct GdkEventFocus {
    int type;
    GdkWindow* window;
    std::int8_t send_event;
    std::int16_t in;
};

constexpr int GTK_WINDOW_POPUP = 1;
constexpr int G_SIGNAL_MATCH_DATA = 1 << 4;
// WebKitPolicyDecisionType and WebKitNavigationType.
constexpr int POLICY_NAVIGATION_ACTION = 0, POLICY_NEW_WINDOW_ACTION = 1, POLICY_RESPONSE = 2;
constexpr int NAVIGATION_LINK_CLICKED = 0;

// X(return type, name, arguments)
#define DEN_GTK_FUNCTIONS(X)                                                                                     \
    X(gboolean, g_main_context_iteration, (GMainContext*, gboolean))                                             \
    X(gboolean, g_main_context_pending, (GMainContext*))                                                         \
    X(gulong, g_signal_connect_data, (gpointer, const char*, void (*)(), gpointer, void*, int))                  \
    X(guint, g_signal_handlers_disconnect_matched, (gpointer, int, guint, std::uint32_t, void*, void*, void*))   \
    X(gpointer, g_object_ref, (gpointer))                                                                        \
    X(void, g_object_unref, (gpointer))                                                                          \
    X(void, g_free, (gpointer))                                                                                  \
    X(void, g_hash_table_destroy, (GHashTable*))                                                                 \
    X(std::int64_t, g_get_monotonic_time, ())                                                                    \
    X(void, gtk_disable_setlocale, ())                                                                           \
    X(gboolean, gtk_init_check, (int*, char***))                                                                 \
    X(GtkWidget*, gtk_window_new, (int))                                                                         \
    X(void, gtk_container_add, (GtkWidget*, GtkWidget*))                                                         \
    X(void, gtk_widget_realize, (GtkWidget*))                                                                    \
    X(void, gtk_widget_show_all, (GtkWidget*))                                                                   \
    X(void, gtk_widget_hide, (GtkWidget*))                                                                       \
    X(void, gtk_widget_destroy, (GtkWidget*))                                                                    \
    X(GdkWindow*, gtk_widget_get_window, (GtkWidget*))                                                           \
    X(void, gtk_widget_grab_focus, (GtkWidget*))                                                                 \
    X(void, gtk_widget_set_app_paintable, (GtkWidget*, gboolean))                                                \
    X(void, gtk_window_set_default_size, (GtkWidget*, int, int))                                                 \
    X(void, gtk_window_resize, (GtkWidget*, int, int))                                                           \
    X(void, gtk_window_move, (GtkWidget*, int, int))                                                             \
    X(void, gtk_main_do_event, (void*))                                                                          \
    X(void, gdk_set_allowed_backends, (const char*))                                                             \
    X(GdkDisplay*, gdk_display_get_default, ())                                                                  \
    X(GdkWindow*, gdk_x11_window_foreign_new_for_display, (GdkDisplay*, unsigned long))                          \
    X(unsigned long, gdk_x11_window_get_xid, (GdkWindow*))                                                       \
    X(XDisplay*, gdk_x11_display_get_xdisplay, (GdkDisplay*))                                                    \
    X(void, gdk_x11_display_error_trap_push, (GdkDisplay*))                                                      \
    X(void, gdk_x11_display_error_trap_pop_ignored, (GdkDisplay*))                                               \
    X(void, gdk_display_sync, (GdkDisplay*))                                                                     \
    X(void, gdk_window_reparent, (GdkWindow*, GdkWindow*, int, int))                                             \
    X(void, gdk_window_raise, (GdkWindow*))                                                                      \
    X(void, gdk_window_move_resize, (GdkWindow*, int, int, int, int))                                            \
    X(void, gdk_window_move, (GdkWindow*, int, int))                                                             \
    X(void, gdk_window_get_root_coords, (GdkWindow*, int, int, int*, int*))                                      \
    X(void, gdk_window_input_shape_combine_region, (GdkWindow*, const cairo_region_t*, int, int))                \
    X(void, gdk_window_shape_combine_region, (GdkWindow*, const cairo_region_t*, int, int))                      \
    X(void*, gdk_event_new, (int))                                                                               \
    X(void, gdk_event_free, (void*))                                                                             \
    X(void, gdk_event_set_device, (void*, GdkDevice*))                                                           \
    X(GdkSeat*, gdk_display_get_default_seat, (GdkDisplay*))                                                     \
    X(GdkDevice*, gdk_seat_get_pointer, (GdkSeat*))                                                              \
    X(GdkDevice*, gdk_seat_get_keyboard, (GdkSeat*))                                                             \
    X(GdkKeymap*, gdk_keymap_get_for_display, (GdkDisplay*))                                                     \
    X(gboolean, gdk_keymap_get_entries_for_keyval, (GdkKeymap*, guint, GdkKeymapKey**, int*))                    \
    X(int, XSetInputFocus, (XDisplay*, unsigned long, int, unsigned long))                                       \
    X(cairo_region_t*, cairo_region_create, ())                                                                  \
    X(int, cairo_region_union_rectangle, (cairo_region_t*, const cairo_rectangle_int_t*))                        \
    X(void, cairo_region_destroy, (cairo_region_t*))                                                             \
    X(void, cairo_set_source_rgb, (cairo_t*, double, double, double))                                            \
    X(void, cairo_paint, (cairo_t*))                                                                             \
    X(void, cairo_arc, (cairo_t*, double, double, double, double, double))                                       \
    X(void, cairo_fill, (cairo_t*))                                                                              \
    X(WebKitWebsiteDataManager*, webkit_website_data_manager_new, (const char*, ...))                            \
    X(WebKitWebContext*, webkit_web_context_new_with_website_data_manager, (WebKitWebsiteDataManager*))          \
    X(GtkWidget*, webkit_web_view_new_with_context, (WebKitWebContext*))                                         \
    X(WebKitSettings*, webkit_web_view_get_settings, (WebKitWebView*))                                           \
    X(void, webkit_settings_set_enable_developer_extras, (WebKitSettings*, gboolean))                            \
    X(void, webkit_web_view_load_uri, (WebKitWebView*, const char*))                                             \
    X(void, webkit_web_view_go_back, (WebKitWebView*))                                                           \
    X(void, webkit_web_view_go_forward, (WebKitWebView*))                                                        \
    X(void, webkit_web_view_reload, (WebKitWebView*))                                                            \
    X(void, webkit_web_view_stop_loading, (WebKitWebView*))                                                      \
    X(const char*, webkit_web_view_get_uri, (WebKitWebView*))                                                    \
    X(const char*, webkit_web_view_get_title, (WebKitWebView*))                                                  \
    X(gboolean, webkit_web_view_is_loading, (WebKitWebView*))                                                    \
    X(gboolean, webkit_web_view_can_go_back, (WebKitWebView*))                                                   \
    X(gboolean, webkit_web_view_can_go_forward, (WebKitWebView*))                                                \
    X(void, webkit_web_view_set_zoom_level, (WebKitWebView*, double))                                            \
    X(void, webkit_web_view_execute_editing_command_with_argument, (WebKitWebView*, const char*, const char*))   \
    X(void, webkit_policy_decision_use, (WebKitPolicyDecision*))                                                 \
    X(void, webkit_policy_decision_ignore, (WebKitPolicyDecision*))                                              \
    X(void, webkit_policy_decision_download, (WebKitPolicyDecision*))                                            \
    X(WebKitNavigationAction*, webkit_navigation_policy_decision_get_navigation_action, (WebKitPolicyDecision*)) \
    X(WebKitURIRequest*, webkit_navigation_action_get_request, (WebKitNavigationAction*))                        \
    X(int, webkit_navigation_action_get_navigation_type, (WebKitNavigationAction*))                              \
    X(gboolean, webkit_navigation_action_is_user_gesture, (WebKitNavigationAction*))                             \
    X(gboolean, webkit_navigation_action_is_redirect, (WebKitNavigationAction*))                                 \
    X(const char*, webkit_uri_request_get_uri, (WebKitURIRequest*))                                              \
    X(WebKitURIResponse*, webkit_response_policy_decision_get_response, (WebKitPolicyDecision*))                 \
    X(gboolean, webkit_response_policy_decision_is_mime_type_supported, (WebKitPolicyDecision*))                 \
    X(gboolean, webkit_response_policy_decision_is_main_frame_main_resource, (WebKitPolicyDecision*))            \
    X(const char*, webkit_uri_response_get_uri, (WebKitURIResponse*))                                            \
    X(const char*, webkit_uri_response_get_mime_type, (WebKitURIResponse*))                                      \
    X(std::uint64_t, webkit_uri_response_get_content_length, (WebKitURIResponse*))                               \
    X(SoupMessageHeaders*, webkit_uri_response_get_http_headers, (WebKitURIResponse*))                           \
    X(gboolean, soup_message_headers_get_content_disposition, (SoupMessageHeaders*, char**, GHashTable**))       \
    X(void, webkit_permission_request_deny, (gpointer))                                                          \
    X(WebKitURIResponse*, webkit_download_get_response, (WebKitDownload*))                                       \
    X(WebKitURIRequest*, webkit_download_get_request, (WebKitDownload*))                                         \
    X(void, webkit_download_set_destination, (WebKitDownload*, const char*))                                     \
    X(void, webkit_download_set_allow_overwrite, (WebKitDownload*, gboolean))                                    \
    X(std::uint64_t, webkit_download_get_received_data_length, (WebKitDownload*))                                \
    X(void, webkit_download_cancel, (WebKitDownload*))

struct Api {
#define DEN_GTK_FIELD(ret, name, args) ret (*name) args = nullptr;
    DEN_GTK_FUNCTIONS(DEN_GTK_FIELD)
#undef DEN_GTK_FIELD
    std::string library; // the file that was loaded
};

/// The interface, loaded on first use; null with `reason` filled when the system has no
/// usable WebKitGTK. Loading starts nothing: GTK is initialised by the first view.
const Api* Load(std::string& reason);

template <typename Handler>
gulong Connect(const Api& api, gpointer object, const char* signal, Handler handler, gpointer data) {
    return api.g_signal_connect_data(object, signal, reinterpret_cast<void (*)()>(handler), data, nullptr, 0);
}

} // namespace den::pal::gtk
