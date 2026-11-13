// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The fake web view: the navigation filter, the history and the download decisions of a real
// backend with no engine and no network behind them.

#include <format>
#include <fstream>

#include "den/platform/webview.h"

namespace den::pal {

namespace {

const char* KeyName(WebKey key) {
    static const char* const NAMES[] = {"Enter", "Tab",   "Backspace", "Delete",   "Escape", "Up", "Down",
                                        "Left",  "Right", "PageUp",    "PageDown", "Home",   "End"};
    return NAMES[static_cast<int>(key)];
}

class Fake final : public FakeWebView {
public:
    explicit Fake(WebViewConfig config) : config_(std::move(config)) {
        state_.ready = true;
    }

    void SetCallbacks(WebCallbacks callbacks) override {
        callbacks_ = std::move(callbacks);
    }
    void SetBounds(const WebRect& bounds) override {
        bounds_ = bounds;
    }
    void Show() override {
        visible_ = true;
    }
    void Hide() override {
        visible_ = false;
    }
    bool Visible() const override {
        return visible_;
    }

    void Navigate(const std::string& url) override {
        Go(url, false, false);
    }
    void Back() override {
        if (index_ > 0) {
            --index_;
            Arrived();
        }
    }
    void Forward() override {
        if (index_ + 1 < static_cast<int>(history_.size())) {
            ++index_;
            Arrived();
        }
    }
    void Reload() override {
        if (index_ >= 0) {
            Arrived();
        }
    }
    void Stop() override {}
    const WebState& State() const override {
        return state_;
    }
    void SetAllowedHosts(std::vector<std::string> hosts) override {
        config_.allowed_hosts = std::move(hosts);
    }
    void CancelDownload(std::uint64_t) override {}

    void SetPointerVisible(bool) override {}
    void MovePointer(int x, int y) override {
        input_.push_back(std::format("move {} {}", x, y));
    }
    void Click(int x, int y) override {
        input_.push_back(std::format("click {} {}", x, y));
    }
    void Scroll(int x, int y, int dx, int dy) override {
        input_.push_back(std::format("scroll {} {} {} {}", x, y, dx, dy));
    }
    void TypeText(const std::string& text) override {
        input_.push_back("text " + text);
    }
    void Key(WebKey key) override {
        input_.push_back(std::string("key ") + KeyName(key));
    }
    void SetZoom(float factor) override {
        zoom_ = factor;
    }
    float Zoom() const override {
        return zoom_;
    }

    bool SimulateLink(const std::string& url, bool new_window) override {
        return Go(url, true, new_window);
    }

    std::uint64_t SimulateDownload(const std::string& url, const std::string& file_name, const std::string& mime,
                                   const std::string& content) override {
        WebDownload download;
        download.id = ++last_download_;
        download.file_name = WebSafeFileName(file_name);
        download.mime = mime;
        download.url = url;
        download.host = WebUrlHost(url);
        download.size = static_cast<std::int64_t>(content.size());
        const WebDownloadDecision decision =
            callbacks_.on_download ? callbacks_.on_download(download) : WebDownloadDecision::Refuse();
        if (!decision.accept) {
            return download.id;
        }
        std::ofstream out(decision.target, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out) {
            if (callbacks_.on_download_failed) {
                callbacks_.on_download_failed(download.id, "the file could not be written");
            }
            return download.id;
        }
        if (callbacks_.on_download_progress) {
            callbacks_.on_download_progress(download.id, download.size, download.size);
        }
        if (callbacks_.on_download_finished) {
            callbacks_.on_download_finished(download.id, decision.target);
        }
        return download.id;
    }

    const std::vector<std::string>& Input() const override {
        return input_;
    }
    const WebRect& Bounds() const override {
        return bounds_;
    }

private:
    bool Go(const std::string& url, bool user, bool new_window) {
        WebNavigation navigation;
        navigation.url = url;
        navigation.host = WebUrlHost(url);
        navigation.host_allowed = WebUrlAllowed(url, config_.allowed_hosts);
        navigation.user_initiated = user;
        navigation.new_window = new_window;
        const bool allowed = callbacks_.on_navigate ? callbacks_.on_navigate(navigation) : navigation.host_allowed;
        if (!allowed) {
            if (callbacks_.on_refused) {
                callbacks_.on_refused(navigation);
            }
            return false;
        }
        history_.resize(static_cast<std::size_t>(index_ + 1));
        history_.push_back(url);
        ++index_;
        Arrived();
        return true;
    }

    void Arrived() {
        state_.url = history_[static_cast<std::size_t>(index_)];
        state_.title = WebUrlHost(state_.url);
        state_.loading = false;
        state_.can_go_back = index_ > 0;
        state_.can_go_forward = index_ + 1 < static_cast<int>(history_.size());
        if (callbacks_.on_state) {
            callbacks_.on_state(state_);
        }
    }

    WebViewConfig config_;
    WebCallbacks callbacks_;
    WebState state_;
    WebRect bounds_;
    std::vector<std::string> history_;
    std::vector<std::string> input_;
    int index_ = -1;
    std::uint64_t last_download_ = 0;
    float zoom_ = 1.0f;
    bool visible_ = false;
};

} // namespace

std::unique_ptr<FakeWebView> CreateFakeWebView(WebViewConfig config) {
    return std::make_unique<Fake>(std::move(config));
}

} // namespace den::pal
