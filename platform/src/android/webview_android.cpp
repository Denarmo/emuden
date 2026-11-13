// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Android backend of pal::WebView: an android.webkit.WebView in the activity's layout,
// above the app's surface (shells/android, WebPage.java and WebDownloads.java).
//
// The view lives on Android's UI thread, the app on its own. Calls go out as commands the
// Java side posts to the UI thread; what the view reports comes back through the natives at
// the end of this file and is carried to the app's thread with SDL_RunOnMainThread, so
// callbacks run from the event pump as on the other platforms. The one answer the UI thread
// cannot wait a frame for, whether a navigation may go ahead, is given there and then from
// the allowed hosts (WebShared).

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

#include <SDL3/SDL.h>
#include <jni.h>

#include "../webview_native.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "../webview_download_names.h"

namespace den::pal {

namespace {

// WebPage.java has the same numbers.
enum class Op { Create, Destroy, Bounds, Visible, Load, GoBack, GoForward, Reload, Stop, Pointer, Click, Scroll, Text, Key, Zoom, Fetch, Cancel };
enum NavigationKind { Link = 0, Redirect = 1, PageStart = 2 };

/// What the UI thread reads to answer a navigation at once.
struct WebShared {
    std::mutex mutex;
    std::vector<std::string> allowed;
    std::set<std::string> approved; // let in by the decision callback
    bool ask = false;               // a decision callback is set
};

/// A navigation the UI thread is holding for the app's decision callback.
struct Decision {
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false, allow = false;
};

struct Event {
    enum Kind { State, Refused, Decide, Download, Blob, Progress, Finished, Failed } kind = State;
    std::uint64_t handle = 0;
    std::string a, b, c, d;
    std::int64_t n = 0, m = 0, total = -1;
    bool loading = false, back = false, forward = false;
    WebNavigation navigation;
    std::shared_ptr<Decision> decision;
};

class AndroidWebView;
std::mutex g_mutex;
std::map<std::uint64_t, std::shared_ptr<WebShared>> g_shared; // read by the UI thread
std::map<std::uint64_t, AndroidWebView*> g_views;             // the app's thread only
std::uint64_t g_last_handle = 0;

std::string Bytes(JNIEnv* env, jbyteArray array) {
    if (!array) {
        return {};
    }
    const jsize size = env->GetArrayLength(array);
    std::string out(static_cast<std::size_t>(size), '\0');
    env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(out.data()));
    return out;
}

// Text crosses as UTF-8 bytes: JNI's own strings are not UTF-8 beyond the basic plane.
jbyteArray ToBytes(JNIEnv* env, const std::string& text) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(text.size()));
    if (array && !text.empty()) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte*>(text.data()));
    }
    return array;
}

void Command(std::uint64_t handle, Op op, int a = 0, int b = 0, int c = 0, int d = 0, const std::string& s = {},
             const std::string& t = {}) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) {
        return;
    }
    jclass activity_class = env->GetObjectClass(activity);
    jmethodID method = env->GetStaticMethodID(activity_class, "webCommand", "(JIIIII[B[B)V");
    if (method && !env->ExceptionCheck()) {
        jbyteArray first = ToBytes(env, s), second = ToBytes(env, t);
        env->CallStaticVoidMethod(activity_class, method, static_cast<jlong>(handle), static_cast<jint>(op), a, b, c, d,
                                  first, second);
        env->DeleteLocalRef(first);
        env->DeleteLocalRef(second);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);
}

int KeyCode(WebKey key) {
    switch (key) { // android.view.KeyEvent
    case WebKey::Enter: return 66;
    case WebKey::Tab: return 61;
    case WebKey::Backspace: return 67;
    case WebKey::Delete: return 112;
    case WebKey::Escape: return 111;
    case WebKey::Up: return 19;
    case WebKey::Down: return 20;
    case WebKey::Left: return 21;
    case WebKey::Right: return 22;
    case WebKey::PageUp: return 92;
    case WebKey::PageDown: return 93;
    case WebKey::Home: return 122;
    case WebKey::End: return 123;
    }
    return 0;
}

class AndroidWebView final : public WebView {
public:
    AndroidWebView(const WebRect& bounds, WebViewConfig config) : bounds_(bounds) {
        shared_->allowed = std::move(config.allowed_hosts);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            handle_ = ++g_last_handle;
            g_shared[handle_] = shared_;
            g_views[handle_] = this;
        }
        Command(handle_, Op::Create, bounds.x, bounds.y, bounds.w, bounds.h);
    }

    ~AndroidWebView() override {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_shared.erase(handle_);
            g_views.erase(handle_);
        }
        Command(handle_, Op::Destroy);
    }

    void SetCallbacks(WebCallbacks callbacks) override {
        callbacks_ = std::move(callbacks);
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->ask = static_cast<bool>(callbacks_.on_navigate);
    }
    void SetBounds(const WebRect& bounds) override {
        if (bounds != bounds_) {
            bounds_ = bounds;
            Command(handle_, Op::Bounds, bounds.x, bounds.y, bounds.w, bounds.h);
        }
    }
    void Show() override {
        visible_ = true;
        Command(handle_, Op::Visible, 1);
    }
    void Hide() override {
        visible_ = false;
        Command(handle_, Op::Visible, 0);
    }
    bool Visible() const override {
        return visible_;
    }

    void Navigate(const std::string& url) override {
        WebNavigation navigation;
        navigation.url = url;
        navigation.host = WebUrlHost(url);
        bool allowed = false;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            navigation.host_allowed = WebUrlAllowed(url, shared_->allowed);
            allowed = navigation.host_allowed;
        }
        if (callbacks_.on_navigate) {
            allowed = callbacks_.on_navigate(navigation) && !navigation.host.empty();
            if (allowed && !navigation.host_allowed) {
                std::lock_guard<std::mutex> lock(shared_->mutex);
                shared_->approved.insert(navigation.host);
            }
        }
        if (!allowed) {
            Refuse(navigation);
            return;
        }
        Command(handle_, Op::Load, 0, 0, 0, 0, url);
    }
    void Back() override {
        Command(handle_, Op::GoBack);
    }
    void Forward() override {
        Command(handle_, Op::GoForward);
    }
    void Reload() override {
        Command(handle_, Op::Reload);
    }
    void Stop() override {
        Command(handle_, Op::Stop);
    }
    const WebState& State() const override {
        return state_;
    }
    void SetAllowedHosts(std::vector<std::string> hosts) override {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->allowed = std::move(hosts);
    }
    void CancelDownload(std::uint64_t id) override {
        Command(handle_, Op::Cancel, static_cast<int>(id));
    }

    void SetPointerVisible(bool visible) override {
        if (visible != pointer_visible_) {
            pointer_visible_ = visible;
            Command(handle_, Op::Pointer, visible ? 1 : 0, pointer_x_, pointer_y_);
        }
    }
    void MovePointer(int x, int y) override {
        if (x != pointer_x_ || y != pointer_y_) {
            pointer_x_ = x;
            pointer_y_ = y;
            Command(handle_, Op::Pointer, pointer_visible_ ? 1 : 0, x, y);
        }
    }
    void Click(int x, int y) override {
        Command(handle_, Op::Click, x, y);
    }
    void Scroll(int x, int y, int dx, int dy) override {
        Command(handle_, Op::Scroll, x, y, dx, dy);
    }
    void TypeText(const std::string& text) override {
        Command(handle_, Op::Text, 0, 0, 0, 0, text);
    }
    void Key(WebKey key) override {
        Command(handle_, Op::Key, KeyCode(key));
    }
    void SetZoom(float factor) override {
        zoom_ = std::clamp(factor, 0.25f, 4.0f);
        Command(handle_, Op::Zoom, static_cast<int>(zoom_ * 100.0f));
    }
    float Zoom() const override {
        return zoom_;
    }

    void On(Event& event) {
        switch (event.kind) {
        case Event::State:
            state_.url = std::move(event.a);
            state_.title = std::move(event.b);
            state_.loading = event.loading;
            state_.can_go_back = event.back;
            state_.can_go_forward = event.forward;
            state_.ready = true;
            if (callbacks_.on_state) {
                callbacks_.on_state(state_);
            }
            break;
        case Event::Refused:
            Refuse(event.navigation);
            break;
        case Event::Decide: {
            const bool allow = callbacks_.on_navigate ? callbacks_.on_navigate(event.navigation)
                                                      : event.navigation.host_allowed;
            std::lock_guard<std::mutex> lock(event.decision->mutex);
            event.decision->allow = allow;
            event.decision->done = true;
            event.decision->changed.notify_all();
            break;
        }
        case Event::Download:
            OnDownload(event);
            break;
        case Event::Blob:
            OnBlob(event);
            break;
        case Event::Progress:
            if (callbacks_.on_download_progress) {
                callbacks_.on_download_progress(static_cast<std::uint64_t>(event.n), event.m, event.total);
            }
            break;
        case Event::Finished:
            if (callbacks_.on_download_finished) {
                callbacks_.on_download_finished(static_cast<std::uint64_t>(event.n), PathFromUtf8(event.a));
            }
            break;
        case Event::Failed:
            if (callbacks_.on_download_failed) {
                callbacks_.on_download_failed(static_cast<std::uint64_t>(event.n), event.a);
            }
            break;
        }
    }

private:
    void Refuse(const WebNavigation& navigation) {
        log::Info("webview", "refused a navigation to '{}'", navigation.host);
        if (callbacks_.on_refused) {
            callbacks_.on_refused(navigation);
        }
    }

    // The user pressed a download link: a = address, b = user agent, c = Content-Disposition,
    // d = type. When the app takes the file, the Java side requests that address once more
    // (the view itself has no way to save a response) and writes the answer to the target.
    void OnDownload(Event& event) {
        WebDownload download;
        download.id = ++last_download_;
        download.file_name = WebDownloadName(event.c, event.a);
        download.mime = event.d;
        download.url = event.a;
        download.host = WebUrlHost(event.a);
        download.size = event.n > 0 ? event.n : -1;
        const WebDownloadDecision decision =
            callbacks_.on_download ? callbacks_.on_download(download) : WebDownloadDecision::Refuse();
        if (!decision.accept || decision.target.empty()) {
            log::Info("webview", "refused the download '{}'", download.file_name);
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(decision.target.parent_path(), ec);
        log::Info("webview", "download {} '{}' from {}", download.id, download.file_name, download.host);
        Command(handle_, Op::Fetch, static_cast<int>(download.id), 0, 0, 0, event.a, PathToUtf8(decision.target) + "\n" + event.b);
    }

    // A file the page made itself (a blob: or data: address) and the user asked to save:
    // a = where the Java side put it, b = its type. The page gives such a file no name here,
    // so it is named after what its first bytes say it is.
    void OnBlob(Event& event) {
        const std::filesystem::path file = PathFromUtf8(event.a);
        std::error_code ec;
        char head[8] = {};
        std::ifstream(file, std::ios::binary).read(head, sizeof(head));
        WebDownload download;
        download.id = ++last_download_;
        download.file_name = "download" + WebPatchExtension(std::string_view(head, sizeof(head)));
        download.mime = event.b;
        download.url = state_.url;
        download.host = WebUrlHost(state_.url);
        download.size = event.n;
        const WebDownloadDecision decision =
            callbacks_.on_download ? callbacks_.on_download(download) : WebDownloadDecision::Refuse();
        if (!decision.accept || decision.target.empty()) {
            std::filesystem::remove(file, ec);
            log::Info("webview", "refused the download '{}'", download.file_name);
            return;
        }
        std::filesystem::create_directories(decision.target.parent_path(), ec);
        std::filesystem::rename(file, decision.target, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(file, decision.target, std::filesystem::copy_options::overwrite_existing, ec);
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
        }
        if (ec) {
            if (callbacks_.on_download_failed) {
                callbacks_.on_download_failed(download.id, "the file could not be written");
            }
            return;
        }
        if (callbacks_.on_download_progress) {
            callbacks_.on_download_progress(download.id, event.n, event.n);
        }
        if (callbacks_.on_download_finished) {
            callbacks_.on_download_finished(download.id, decision.target);
        }
    }

    std::shared_ptr<WebShared> shared_ = std::make_shared<WebShared>();
    WebCallbacks callbacks_;
    WebState state_;
    WebRect bounds_;
    std::uint64_t handle_ = 0;
    std::uint64_t last_download_ = 0;
    int pointer_x_ = -1, pointer_y_ = -1;
    float zoom_ = 1.0f;
    bool visible_ = false, pointer_visible_ = false;
};

// On the app's thread, from the event pump.
void SDLCALL Deliver(void* userdata) {
    const std::unique_ptr<Event> event(static_cast<Event*>(userdata));
    AndroidWebView* view = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (const auto it = g_views.find(event->handle); it != g_views.end()) {
            view = it->second;
        }
    }
    if (view) {
        view->On(*event);
    } else if (event->decision) {
        std::lock_guard<std::mutex> lock(event->decision->mutex);
        event->decision->done = true;
        event->decision->changed.notify_all();
    }
}

void Post(std::unique_ptr<Event> event) {
    SDL_RunOnMainThread(Deliver, event.release(), false);
}

// On the UI thread: may the top-level page go to `url`? Mirrors the Windows backend: a link
// to another host is refused; a redirect to one goes on as far as the server's answer (a
// download may come from it); a page that starts on one is undone by the caller.
bool MayNavigate(std::uint64_t handle, const std::string& url, int kind, bool gesture) {
    if (url == "about:blank") {
        return true;
    }
    std::shared_ptr<WebShared> shared;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (const auto it = g_shared.find(handle); it != g_shared.end()) {
            shared = it->second;
        }
    }
    if (!shared) {
        return false;
    }
    WebNavigation navigation;
    navigation.url = url;
    navigation.host = WebUrlHost(url);
    navigation.user_initiated = gesture;
    navigation.redirected = kind != Link;
    bool ask = false, approved = false;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        navigation.host_allowed = WebUrlAllowed(url, shared->allowed);
        approved = shared->approved.count(navigation.host) != 0;
        ask = shared->ask;
    }
    bool allowed = navigation.host_allowed;
    if (kind == PageStart) {
        allowed = allowed || approved;
    } else if (ask) {
        // The decision is the app's, on its own thread; without an answer in time (the app
        // is paused, say) the allowed hosts decide.
        auto event = std::make_unique<Event>();
        event->kind = Event::Decide;
        event->handle = handle;
        event->navigation = navigation;
        const auto decision = event->decision = std::make_shared<Decision>();
        Post(std::move(event));
        std::unique_lock<std::mutex> lock(decision->mutex);
        if (decision->changed.wait_for(lock, std::chrono::milliseconds(300), [&] { return decision->done; })) {
            allowed = decision->allow && !navigation.host.empty();
            if (allowed && !navigation.host_allowed) {
                std::lock_guard<std::mutex> hosts(shared->mutex);
                shared->approved.insert(navigation.host);
            }
        }
    }
    if (allowed || (kind == Redirect && !navigation.host.empty())) {
        return true;
    }
    auto event = std::make_unique<Event>();
    event->kind = Event::Refused;
    event->handle = handle;
    event->navigation = std::move(navigation);
    Post(std::move(event));
    return false;
}

} // namespace

bool NativeWebViewAvailable(std::string& reason) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    bool available = false;
    if (env && activity) {
        jclass activity_class = env->GetObjectClass(activity);
        jmethodID method = env->GetStaticMethodID(activity_class, "webAvailable", "()Z");
        if (method && !env->ExceptionCheck()) {
            available = env->CallStaticBooleanMethod(activity_class, method) != JNI_FALSE;
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);
    }
    if (!available) {
        reason = "Android System WebView is missing or switched off on this device. Enable it in the "
                 "system's app settings.";
    }
    return available;
}

// The profile (cache, the site's cookies and storage) is the one Android keeps for the app's
// web views; `config.user_data_dir` has no meaning here.
Result<std::unique_ptr<WebView>> CreateNativeWebView(Window&, const WebRect& bounds, WebViewConfig config) {
    std::string reason;
    if (!NativeWebViewAvailable(reason)) {
        return Status::Fail(reason);
    }
    return std::unique_ptr<WebView>(std::make_unique<AndroidWebView>(bounds, std::move(config)));
}

} // namespace den::pal

extern "C" {

using den::pal::Bytes;
using den::pal::Event;

JNIEXPORT jboolean JNICALL Java_io_github_denarmo_emuden_WebPage_nativeNavigate(JNIEnv* env, jclass, jlong handle,
                                                                               jbyteArray url, jint kind,
                                                                               jboolean gesture) {
    return den::pal::MayNavigate(static_cast<std::uint64_t>(handle), Bytes(env, url), kind, gesture != JNI_FALSE)
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_WebPage_nativeState(JNIEnv* env, jclass, jlong handle,
                                                                        jbyteArray url, jbyteArray title,
                                                                        jboolean loading, jboolean back,
                                                                        jboolean forward) {
    auto event = std::make_unique<Event>();
    event->kind = Event::State;
    event->handle = static_cast<std::uint64_t>(handle);
    event->a = Bytes(env, url);
    event->b = Bytes(env, title);
    event->loading = loading != JNI_FALSE;
    event->back = back != JNI_FALSE;
    event->forward = forward != JNI_FALSE;
    den::pal::Post(std::move(event));
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_WebPage_nativeDownload(JNIEnv* env, jclass, jlong handle,
                                                                           jbyteArray url, jbyteArray agent,
                                                                           jbyteArray disposition, jbyteArray mime,
                                                                           jlong length) {
    auto event = std::make_unique<Event>();
    event->kind = Event::Download;
    event->handle = static_cast<std::uint64_t>(handle);
    event->a = Bytes(env, url);
    event->b = Bytes(env, agent);
    event->c = Bytes(env, disposition);
    event->d = Bytes(env, mime);
    event->n = length;
    den::pal::Post(std::move(event));
}

JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_WebDownloads_nativeBlob(JNIEnv* env, jclass, jlong handle,
                                                                            jbyteArray file, jbyteArray mime,
                                                                            jlong length) {
    auto event = std::make_unique<Event>();
    event->kind = Event::Blob;
    event->handle = static_cast<std::uint64_t>(handle);
    event->a = Bytes(env, file);
    event->b = Bytes(env, mime);
    event->n = length;
    den::pal::Post(std::move(event));
}

// kind: 0 = progress (received, total), 1 = finished (text = the file), 2 = failed (text = why).
JNIEXPORT void JNICALL Java_io_github_denarmo_emuden_WebDownloads_nativeFetch(JNIEnv* env, jclass, jlong handle,
                                                                             jint id, jint kind, jlong received,
                                                                             jlong total, jbyteArray text) {
    auto event = std::make_unique<Event>();
    event->kind = kind == 0 ? Event::Progress : kind == 1 ? Event::Finished : Event::Failed;
    event->handle = static_cast<std::uint64_t>(handle);
    event->a = Bytes(env, text);
    event->n = id;
    event->m = received;
    event->total = total > 0 ? total : -1;
    den::pal::Post(std::move(event));
}

} // extern "C"
