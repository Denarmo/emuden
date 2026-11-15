// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A web page shown with the platform's own web engine, in a native view placed over part of
// the app's window. The user browses; the app only decides where navigation may go and where
// a download the user started is saved. Nothing here reads a page: there is no call that
// returns its content, runs a script in it or touches its cookies. See docs/in-app-browser.md.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "den/base/result.h"
#include "den/platform/pal.h"

namespace den::pal {

/// A rectangle in pixels of the parent window's client area.
struct WebRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const WebRect&) const = default;
};

struct WebState {
    std::string url;   // the address of the document shown
    std::string title; // the document's title, as a browser's tab shows it
    bool loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
    bool ready = false; // the engine has started; calls made before are carried out then
};

/// A top-level navigation the engine is about to make.
struct WebNavigation {
    std::string url;
    std::string host;          // lower case, without the port
    bool host_allowed = false; // the host is on the allowed list (see WebHostAllowed)
    bool user_initiated = false;
    bool redirected = false; // a server redirect of a navigation already under way
    bool new_window = false; // the page asked for a new window; it opens in this view
};

/// A download the user started in the page.
struct WebDownload {
    std::uint64_t id = 0;
    std::string file_name; // the name the engine suggests, without a folder
    std::string mime;
    std::string url;         // where the file comes from, after redirects
    std::string host;        // of `url`
    std::int64_t size = -1;  // bytes, -1 when the server did not say
};

struct WebDownloadDecision {
    bool accept = false;
    std::filesystem::path target; // the file to write when accepted
    static WebDownloadDecision Accept(std::filesystem::path file) {
        return {true, std::move(file)};
    }
    static WebDownloadDecision Refuse() {
        return {};
    }
};

enum class WebKey { Enter, Tab, Backspace, Delete, Escape, Up, Down, Left, Right, PageUp, PageDown, Home, End };

/// Everything is called on the main thread, from the platform's event pump.
struct WebCallbacks {
    /// The address, title, loading or history state changed.
    std::function<void(const WebState&)> on_state;
    /// The decision about a navigation; without it a navigation goes ahead when its host is
    /// allowed. A redirect to another host is let through as far as the server's answer, so a
    /// download can come from it; if it turns out to be a page, it is undone and refused.
    std::function<bool(const WebNavigation&)> on_navigate;
    /// A navigation was refused (for a toast).
    std::function<void(const WebNavigation&)> on_refused;
    /// The user started a download. Without the callback every download is refused.
    std::function<WebDownloadDecision(const WebDownload&)> on_download;
    std::function<void(std::uint64_t id, std::int64_t received, std::int64_t total)> on_download_progress;
    std::function<void(std::uint64_t id, const std::filesystem::path& file)> on_download_finished;
    std::function<void(std::uint64_t id, const std::string& reason)> on_download_failed;
};

struct WebViewConfig {
    /// The engine's profile folder (cache, the site's own cookies and storage). The app never
    /// reads it.
    std::filesystem::path user_data_dir;
    /// Hosts the top-level page may be on: exact names ("example.com"), or "*.example.com"
    /// for a name and its subdomains. Ports are ignored. https only, except loopback hosts.
    std::vector<std::string> allowed_hosts;
};

/// The user agent is the engine's own; permission requests (camera, microphone, location,
/// notifications, ...) are denied; the engine's developer tools, password saving and download
/// interface are off.
class WebView {
public:
    virtual ~WebView() = default;

    /// False when the platform has no web engine (or its runtime is missing).
    static bool Available();
    /// Why not, in words for the user; empty when Available().
    static std::string UnavailableReason();
    /// A view over `bounds` of `parent`, hidden until Show(). With the environment variable
    /// EMUDEN_FAKE_WEBVIEW=1 it is the fake backend (tests).
    static Result<std::unique_ptr<WebView>> Create(Window& parent, const WebRect& bounds, WebViewConfig config);

    virtual void SetCallbacks(WebCallbacks callbacks) = 0;
    virtual void SetBounds(const WebRect& bounds) = 0;
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual bool Visible() const = 0;

    /// Goes to `url` if the navigation filter lets it; refused otherwise (on_refused).
    virtual void Navigate(const std::string& url) = 0;
    virtual void Back() = 0;
    virtual void Forward() = 0;
    virtual void Reload() = 0;
    virtual void Stop() = 0;
    virtual const WebState& State() const = 0;
    virtual void SetAllowedHosts(std::vector<std::string> hosts) = 0;
    virtual void CancelDownload(std::uint64_t id) = 0;

    // Input for a pad: the same events a mouse and a keyboard would send. Positions are pixels
    // inside the view.
    /// Shows or hides the pointer the backend draws over the page for MovePointer.
    virtual void SetPointerVisible(bool visible) = 0;
    virtual void MovePointer(int x, int y) = 0;
    virtual void Click(int x, int y) = 0;
    /// The wheel at a point; positive dy scrolls the page down, in pixels.
    virtual void Scroll(int x, int y, int dx, int dy) = 0;
    /// Types into whatever has the page's focus.
    virtual void TypeText(const std::string& text) = 0;
    virtual void Key(WebKey key) = 0;

    virtual void SetZoom(float factor) = 0;
    virtual float Zoom() const = 0;
};

/// The fake backend: no engine, no network. A test plays the user with the Simulate calls.
/// It is also what Create returns under EMUDEN_FAKE_WEBVIEW=1.
class FakeWebView : public WebView {
public:
    /// The user follows a link (or types an address): filtered like any navigation. True when
    /// the view went there.
    virtual bool SimulateLink(const std::string& url, bool new_window = false) = 0;
    /// The user presses a download link. The decision callback runs; when it accepts, the
    /// fake writes `content` to the target and reports progress and the end. Returns the id.
    virtual std::uint64_t SimulateDownload(const std::string& url, const std::string& file_name,
                                           const std::string& mime, const std::string& content) = 0;
    /// What the injection calls did, oldest first: "move 10 20", "click 10 20",
    /// "scroll 10 20 0 120", "text abc", "key Enter".
    virtual const std::vector<std::string>& Input() const = 0;
    virtual const WebRect& Bounds() const = 0;
};
std::unique_ptr<FakeWebView> CreateFakeWebView(WebViewConfig config);

// Addresses (pure functions, shared by every backend).
/// The host of an http or https address in lower case without the port; empty for anything
/// else.
std::string WebUrlHost(const std::string& url);
/// "https", "http", "about", ... in lower case; empty when there is none.
std::string WebUrlScheme(const std::string& url);
/// 127.0.0.1, ::1, localhost and names ending in .localhost.
bool WebHostIsLoopback(const std::string& host);
/// Whether `host` matches an entry of `allowed` (exact, "*.name" for name and subdomains, or
/// "*" for any host: the browser).
bool WebHostAllowed(const std::string& host, const std::vector<std::string>& allowed);
/// Whether the top-level page may be at `url`: https on an allowed host, or http on an
/// allowed loopback host; with "*", http anywhere too.
bool WebUrlAllowed(const std::string& url, const std::vector<std::string>& allowed);
/// The file name of a path or suggestion without folders, drive letters, control characters
/// or the characters a file system refuses; "download" when nothing is left.
std::string WebSafeFileName(const std::string& name);

} // namespace den::pal
