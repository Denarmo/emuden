// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The WebKitGTK web view: a GTK window with the engine's widget, placed inside the app's X11
// window as a child. webview_linux.cpp holds the window, navigation and state;
// webview_linux_downloads.cpp the downloads; webview_linux_input.cpp the pad's input.

#pragma once

#include <map>

#include "../webview_native.h"
#include "gtk_api.h"

namespace den::pal {

class LinuxWebView final : public WebView {
public:
    LinuxWebView(const gtk::Api& api, WebViewConfig config, float display_scale);
    ~LinuxWebView() override;

    /// Builds the windows inside the X11 window `parent_xid`.
    Status Init(unsigned long parent_xid, const WebRect& bounds);

    void SetCallbacks(WebCallbacks callbacks) override;
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
    void SetAllowedHosts(std::vector<std::string> hosts) override;
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
    struct Arriving {
        std::uint64_t id = 0;
        std::filesystem::path target;
        bool failed = false;
    };

    // Signal handlers (the last argument is the view).
    static gtk::gboolean OnDecidePolicy(gtk::WebKitWebView*, gtk::WebKitPolicyDecision*, int type, void* self);
    static void OnNotify(void* object, void* spec, void* self);
    static void OnLoadChanged(gtk::WebKitWebView*, int event, void* self);
    static gtk::gboolean OnPermission(gtk::WebKitWebView*, void* request, void* self);
    static gtk::gboolean OnSwallow(void* object, void* argument, void* self);
    static gtk::gboolean OnRealPress(gtk::GtkWidget*, void* event, void* self);
    static void OnDownloadStarted(gtk::WebKitWebContext*, gtk::WebKitDownload*, void* self);
    static gtk::gboolean OnDecideDestination(gtk::WebKitDownload*, const char* suggested, void* self);
    static void OnReceived(gtk::WebKitDownload*, std::uint64_t length, void* self);
    static void OnFinished(gtk::WebKitDownload*, void* self);
    static void OnFailed(gtk::WebKitDownload*, gtk::GError* error, void* self);
    static gtk::gboolean OnDrawPointer(gtk::GtkWidget*, gtk::cairo_t*, void* self);

    bool DecideNavigation(gtk::WebKitPolicyDecision* decision, bool new_window);
    bool DecideResponse(gtk::WebKitPolicyDecision* decision);
    bool Allow(WebNavigation& navigation) const;
    void Refuse(const WebNavigation& navigation);
    void PublishState();
    void Place();
    void SetActive(bool active);
    void BuildPointer();
    template <typename Event>
    Event* NewEvent(int type, gtk::GdkWindow* window, gtk::GdkDevice* device);
    void Send(void* event);
    void SendButton(int type, int x, int y, gtk::guint state);
    gtk::WebKitWebView* View() const {
        return reinterpret_cast<gtk::WebKitWebView*>(view_);
    }

    const gtk::Api& api_;
    WebViewConfig config_;
    WebCallbacks callbacks_;
    WebState state_;
    WebRect bounds_;
    gtk::GdkDisplay* display_ = nullptr;
    gtk::GdkWindow* parent_ = nullptr;      // the app's window, as GDK sees it
    gtk::GtkWidget* window_ = nullptr;      // the GTK window that holds the engine's widget
    gtk::GtkWidget* view_ = nullptr;        // the engine's widget
    gtk::GtkWidget* pointer_ = nullptr;     // the pad's pointer, a small window above the page
    gtk::WebKitWebContext* context_ = nullptr;
    std::map<gtk::WebKitDownload*, Arriving> downloads_;
    std::uint64_t next_download_ = 1;
    unsigned long parent_xid_ = 0;
    float base_zoom_ = 1, zoom_ = 1;
    int pointer_x_ = 0, pointer_y_ = 0;
    bool visible_ = false, pointer_visible_ = false, pointer_shown_ = false, injecting_ = false, has_x_focus_ = false;
};

/// Runs GTK's pending work now; also called from a timer while a view exists.
void PumpGtk(const gtk::Api& api);

} // namespace den::pal
