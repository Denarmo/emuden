// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser page. The kit draws the bar at the top and the hints at the bottom; the web
// view is a native view over the rest. A native view sits above everything the app draws, so
// it is hidden while a dialog or the text entry is open. It never moves for toasts (a click
// aimed before one appeared would land elsewhere): the bar repeats the newest toast instead.
//
// Pad: the left stick (or the D-pad) moves a pointer, A clicks, the right stick scrolls, B
// goes back in the page's history or closes the page, X types text into the page, View
// presses Enter, LB and RB go back and forward, Y reloads, Start closes. Mouse and touch go
// straight to the page. The browser (mode "browser": any site) has the address bar on Y and
// its menu on Start instead (web_menu.h).
//
// Without a web engine the page is the fallback of web_fallback.h: the user's own browser
// and the Downloads folder. EMUDEN_WEB_FALLBACK=1 asks for it where an engine exists.

#include <algorithm>
#include <cmath>
#include <format>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/platform/webview.h"
#include "den/ui/glyphs.h"
#include "web_bar.h"
#include "web_browser.h"
#include "web_fallback.h"
#include "web_menu.h"

namespace den::app::web {

namespace {

using input::Nav;

constexpr float STICK_DEADZONE = 0.22f;
constexpr float POINTER_SPEED = 900;  // units per second at full deflection
constexpr float SCROLL_SPEED = 1500;  // pixels per second at full deflection
constexpr float STEP = 28;            // units per D-pad press

PageState g_state;
class WebScreen;
WebScreen* g_screen = nullptr;

/// The stick with the dead zone taken out and a soft start.
float Curve(float value) {
    const float size = std::fabs(value);
    if (size < STICK_DEADZONE) {
        return 0;
    }
    const float t = (size - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
    return std::copysign(t * t, value);
}

class WebScreen final : public ui::Page {
public:
    WebScreen() {
        g_screen = this;
        g_state = PageState{};
        g_state.open = true;
    }
    ~WebScreen() override {
        Deliver();
        view_.reset();
        if (g_screen == this) {
            g_screen = nullptr;
            g_state.open = false;
            g_state.view_visible = false;
        }
    }

    void Build() override {}

    void OnShow() override {
        if (!started_) {
            started_ = true;
            Start();
        }
    }

    void OnHide() override {
        if (view_) {
            view_->Hide();
            view_->SetPointerVisible(false);
        }
        g_state.view_visible = false;
    }

    bool OnEvent(const input::Event& event) override {
        using Type = input::Event::Type;
        const float scale = Svc().canvas->Scale();
        switch (event.type) {
        case Type::Nav:
            HandleNav(event.nav, event.repeat);
            return true;
        case Type::PointerMove:
            // The mouse is in use (over the bar; over the page the engine has it).
            ShowPointer(false);
            return true;
        case Type::PointerUp:
            if (menu_.IsOpen()) {
                menu_.Tap(event.x / scale, event.y / scale);
                return true;
            }
            if (fallback_ && fallback_->Tap(event.x / scale, event.y / scale, GetStage())) {
                return true;
            }
            for (const BarHit& button : buttons_) {
                if (button.enabled && button.rect.Contains(event.x / scale, event.y / scale)) {
                    BarPressed(button.action);
                    break;
                }
            }
            return true;
        default:
            return true;
        }
    }

    void Update(ui::Context& ctx) override {
        if (fallback_) {
            fallback_->Update(GetStage());
            for (Download& download : fallback_->TakeAccepted()) {
                finished_.push_back(std::move(download));
            }
            g_state.offered = fallback_->Offered();
            g_state.offers = fallback_->Offers();
        }
        Deliver();
        if (!view_ || !view_->Visible()) {
            return;
        }
        float lx = 0, ly = 0, rx = 0, ry = 0;
        for (int i = 0; i < Svc().input->PadCount(); ++i) {
            const input::PadState& pad = Svc().input->Pad(i);
            const auto take = [](float& into, float value) {
                if (std::fabs(value) > std::fabs(into)) {
                    into = value;
                }
            };
            take(lx, Curve(pad.lx));
            take(ly, Curve(pad.ly));
            take(rx, Curve(pad.rx));
            take(ry, Curve(pad.ry));
        }
        stick_ = lx != 0 || ly != 0;
        if (stick_) {
            MoveCursor(lx * POINTER_SPEED * ctx.dt, ly * POINTER_SPEED * ctx.dt);
        }
        if (rx != 0 || ry != 0) {
            ShowPointer(true);
            scroll_x_ += rx * SCROLL_SPEED * ctx.dt;
            scroll_y_ += ry * SCROLL_SPEED * ctx.dt;
            const int dx = static_cast<int>(scroll_x_), dy = static_cast<int>(scroll_y_);
            if (dx != 0 || dy != 0) {
                scroll_x_ -= static_cast<float>(dx);
                scroll_y_ -= static_cast<float>(dy);
                view_->Scroll(CursorPx(cursor_x_), CursorPx(cursor_y_), dx, dy);
            }
        }
    }

    std::vector<ui::Hint> Hints() const override {
        if (!view_) {
            return {{Nav::Confirm, "Open in browser"}, {Nav::Back, "Close"}};
        }
        if (menu_.IsOpen()) {
            return {{Nav::Confirm, "Choose"}, {Nav::Back, "Back"}};
        }
        if (browser_) {
            return {{Nav::Confirm, "Click"}, {Nav::Option, "Type"}, {Nav::Extra, "Address"},
                    {Nav::Back, state_.can_go_back ? "Back" : "Close"}, {Nav::Menu, "Menu"}};
        }
        return {{Nav::Confirm, "Click"}, {Nav::Option, "Type"}, {Nav::Extra, "Reload"},
                {Nav::Back, state_.can_go_back ? "Back" : "Close"}, {Nav::Menu, "Close"}};
    }

    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        gfx::Canvas& canvas = *ctx.canvas;
        const float scale = canvas.Scale();
        scale_ = scale;
        // The page area starts below a window title bar the app draws in a window.
        const float top = area.y + BAR_HEIGHT;
        view_rect_ = gfx::Rect{area.x, top, area.w, std::max(0.0f, area.Bottom() - top)};
        DrawBar(ctx, area);
        g_state.menu.clear();
        for (const std::string& label : menu_.Labels()) {
            g_state.menu += (g_state.menu.empty() ? "" : "|") + label;
        }
        if (!view_) {
            fallback_->Draw(ctx, view_rect_, !GetStage().DialogOpen());
            return;
        }
        if (!state_.ready) {
            DrawMessage(ctx, "Starting the browser...", {});
        }
        const pal::WebRect bounds{
            static_cast<int>(std::lround(view_rect_.x * scale)), static_cast<int>(std::lround(view_rect_.y * scale)),
            static_cast<int>(std::lround(view_rect_.w * scale)), static_cast<int>(std::lround(view_rect_.h * scale))};
        view_->SetBounds(bounds);
        // A dialog or the text entry is the app's own drawing: the view would cover it.
        const bool covered = GetStage().DialogOpen() || menu_.IsOpen() || bounds.w <= 0 || bounds.h <= 0;
        if (covered && view_->Visible()) {
            view_->Hide();
        } else if (!covered && !view_->Visible()) {
            view_->Show();
        }
        if (cursor_x_ < 0) {
            cursor_x_ = view_rect_.w * 0.5f;
            cursor_y_ = view_rect_.h * 0.5f;
        }
        cursor_x_ = std::clamp(cursor_x_, 0.0f, std::max(0.0f, view_rect_.w - 1));
        cursor_y_ = std::clamp(cursor_y_, 0.0f, std::max(0.0f, view_rect_.h - 1));
        view_->SetPointerVisible(pointer_ && !covered);
        if (pointer_ && !covered) {
            view_->MovePointer(CursorPx(cursor_x_), CursorPx(cursor_y_));
        }
        g_state.view_visible = view_->Visible();
        g_state.pointer = pointer_;
        g_state.cursor_x = cursor_x_;
        g_state.cursor_y = cursor_y_;
        g_state.view_x = view_rect_.x;
        g_state.view_y = view_rect_.y;
        g_state.view_w = view_rect_.w;
        g_state.view_h = view_rect_.h;
        menu_.Draw(ctx, view_rect_);
    }

    void SetCursorAt(float x, float y) {
        cursor_x_ = x;
        cursor_y_ = y;
        ShowPointer(true);
    }

private:
    struct Arriving {
        std::uint64_t id = 0;
        Download download;
        std::int64_t received = 0, total = -1;
    };

    void Start() {
        const std::string url = Param("url");
        hosts_ = SplitList(Param("hosts"));
        if (hosts_.empty()) {
            if (const std::string host = pal::WebUrlHost(url); !host.empty()) {
                hosts_.push_back(host);
            }
        }
        if (const Opener* opener = FindOpener(Param("opener"))) {
            opener_ = *opener;
        }
        browser_ = Param("mode") == "browser";
        g_state.browser = browser_;
        if (browser_) {
            hosts_ = {ANY};
        }
        wanted_ = SplitList(Param("ext"));
        std::erase_if(wanted_, [](const std::string& e) { return IsRomImage(e); });
        if (wanted_.empty()) {
            wanted_ = opener_.extensions;
        }
        g_state.available = pal::WebView::Available() && pal::GetEnv("EMUDEN_WEB_FALLBACK") != "1";
        if (!g_state.available) {
            StartFallback(url, pal::WebView::UnavailableReason());
            return;
        }
        pal::WebViewConfig config;
        config.user_data_dir = Svc().data_dir / "webview";
        config.allowed_hosts = hosts_;
        auto view = pal::WebView::Create(*Svc().window, pal::WebRect{}, std::move(config));
        if (!view) {
            g_state.available = false;
            StartFallback(url, view.error());
            return;
        }
        view_ = view.take();
        pal::WebCallbacks callbacks;
        callbacks.on_state = [this](const pal::WebState& state) {
            state_ = state;
            g_state.ready = state.ready;
            g_state.url = state.url;
            g_state.title = state.title;
            g_state.loading = state.loading;
            g_state.can_go_back = state.can_go_back;
            g_state.can_go_forward = state.can_go_forward;
        };
        callbacks.on_refused = [this](const pal::WebNavigation& navigation) { Refused(navigation); };
        callbacks.on_download = [this](const pal::WebDownload& download) { return Judge(download); };
        callbacks.on_download_progress = [this](std::uint64_t id, std::int64_t received, std::int64_t total) {
            for (Arriving& item : arriving_) {
                if (item.id == id) {
                    item.received = received;
                    item.total = total;
                }
            }
            NoteProgress();
        };
        callbacks.on_download_finished = [this](std::uint64_t id, const std::filesystem::path& file) {
            for (auto it = arriving_.begin(); it != arriving_.end(); ++it) {
                if (it->id == id) {
                    it->download.file = file;
                    finished_.push_back(std::move(it->download));
                    arriving_.erase(it);
                    break;
                }
            }
            NoteProgress();
        };
        callbacks.on_download_failed = [this](std::uint64_t id, const std::string& reason) {
            for (auto it = arriving_.begin(); it != arriving_.end(); ++it) {
                if (it->id == id) {
                    GetStage().Toast(std::format("{} was not downloaded: {}", it->download.name, reason));
                    std::error_code ec;
                    std::filesystem::remove(it->download.file, ec);
                    arriving_.erase(it);
                    break;
                }
            }
            NoteProgress();
        };
        view_->SetCallbacks(std::move(callbacks));
        view_->Navigate(url);
    }

    void StartFallback(const std::string& url, const std::string& reason) {
        fallback_ = std::make_unique<Fallback>(url, hosts_, reason, wanted_);
        fallback_->SetFolder(opener_.folder);
        g_state.fallback = true;
        g_state.watching = fallback_->Watching();
        g_state.url = url;
    }

    /// Takes the files the opener asked for and nothing else; never a ROM.
    pal::WebDownloadDecision Judge(const pal::WebDownload& download) {
        const Verdict verdict = JudgeDownload(download.file_name, wanted_);
        if (verdict != Verdict::Take) {
            ++g_state.refused_downloads;
            g_state.last_refused_download = download.file_name;
            GetStage().Toast(RefusalText(download.file_name, verdict), 6.0f);
            return pal::WebDownloadDecision::Refuse();
        }
        const std::filesystem::path dir = opener_.folder.empty() ? InboxDir() : opener_.folder;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        Arriving item;
        item.id = download.id;
        item.total = download.size;
        item.download.file = FreeFile(dir, download.file_name);
        item.download.name = download.file_name;
        item.download.host = download.host;
        item.download.mime = download.mime;
        item.download.title = state_.title;
        arriving_.push_back(item);
        NoteProgress();
        return pal::WebDownloadDecision::Accept(item.download.file);
    }

    /// Hands finished files to the opener, from the frame and not from inside the engine's event.
    void Deliver() {
        std::vector<Download> ready;
        ready.swap(finished_);
        for (const Download& download : ready) {
            ++g_state.downloads;
            g_state.last_download = PathToUtf8(download.file);
            log::Info("web", "downloaded '{}' from {}", download.name, download.host);
            if (browser_) {
                // The plugin's toast says what became of it; until then the bar says it is busy.
                setting_up_ = download.name;
                setting_up_at_ = pal::NowSeconds();
            } else if (Svc().stage) {
                GetStage().Toast("Downloaded " + download.name);
            }
            if (opener_.on_download) {
                opener_.on_download(download);
            }
        }
    }

    void NoteProgress() {
        if (arriving_.empty()) {
            g_state.downloading.clear();
            g_state.progress = -1;
            return;
        }
        const Arriving& item = arriving_.back();
        g_state.downloading = item.download.name;
        g_state.progress = item.total > 0 ? static_cast<int>(item.received * 100 / item.total) : -1;
    }

    /// A link to a host outside the page's list: the user is asked, once per press. A yes
    /// allows that host until the page is closed and goes there.
    void Refused(const pal::WebNavigation& navigation) {
        ++g_state.refused_navigations;
        g_state.last_refused_host = navigation.host;
        const double now = pal::NowSeconds();
        if (navigation.host == refused_host_ && now - refused_at_ < 1.5) {
            return;
        }
        refused_host_ = navigation.host;
        refused_at_ = now;
        if (!CanLeaveTo(navigation.url) || GetStage().DialogOpen()) {
            GetStage().Toast(navigation.host.empty() ? std::string("That link is not opened here")
                                                     : std::format("{} is outside this page: not opened", navigation.host));
            return;
        }
        g_state.leave_asked = navigation.host;
        GetStage().Confirm("Leaving this site", std::format("This link goes to {}. Open it?", navigation.host), "Open",
                           [url = navigation.url, host = navigation.host] {
                               if (g_screen && g_screen->view_) {
                                   g_screen->hosts_.push_back(host);
                                   g_screen->view_->SetAllowedHosts(g_screen->hosts_);
                                   g_screen->view_->Navigate(url);
                                   g_state.left_to = host;
                               }
                           });
    }

    void HandleNav(Nav nav, bool repeat) {
        if (menu_.IsOpen()) {
            if (!repeat || nav == Nav::Up || nav == Nav::Down) {
                menu_.Nav(nav);
            }
            return;
        }
        const bool usable = view_ && view_->Visible();
        if (browser_ && usable && !repeat && (nav == Nav::Extra || nav == Nav::Menu)) {
            BarPressed(nav == Nav::Extra ? BarAction::Address : BarAction::Menu);
            return;
        }
        switch (nav) {
        case Nav::Up:
        case Nav::Down:
        case Nav::Left:
        case Nav::Right:
            // A deflected stick also arrives as directions: Update moves the pointer for it.
            if (usable && !stick_) {
                MoveCursor(nav == Nav::Left ? -STEP : nav == Nav::Right ? STEP : 0,
                           nav == Nav::Up ? -STEP : nav == Nav::Down ? STEP : 0);
            }
            break;
        case Nav::Confirm:
            if (fallback_ && !repeat) {
                fallback_->OpenBrowser(GetStage());
            } else if (usable && !repeat) {
                ShowPointer(true);
                view_->Click(CursorPx(cursor_x_), CursorPx(cursor_y_));
            }
            break;
        case Nav::Back:
            if (!repeat) {
                GoBack();
            }
            break;
        case Nav::Option:
            if (usable && !repeat) {
                ui::TextRequest request;
                request.title = "Type into the page";
                request.max_length = 200;
                GetStage().AskText(std::move(request), [](const std::string& text) {
                    // The page may have closed meanwhile.
                    if (g_screen && g_screen->view_) {
                        g_screen->view_->TypeText(text);
                    }
                });
            }
            break;
        case Nav::View:
            if (usable && !repeat) {
                view_->Key(pal::WebKey::Enter);
            }
            break;
        case Nav::Extra:
            if (usable && !repeat) {
                view_->Reload();
            }
            break;
        case Nav::ShoulderL:
            if (usable && !repeat) {
                view_->Back();
            }
            break;
        case Nav::ShoulderR:
            if (usable && !repeat) {
                view_->Forward();
            }
            break;
        case Nav::Menu:
            if (!repeat) {
                GetStage().Pop();
            }
            break;
        default:
            break;
        }
    }

    void BarPressed(BarAction action) {
        switch (action) {
        case BarAction::Back:
            view_->Back();
            break;
        case BarAction::Forward:
            view_->Forward();
            break;
        case BarAction::Reload:
            view_->Reload();
            break;
        case BarAction::Close:
            GetStage().Pop();
            break;
        case BarAction::Address:
            AskAddress(GetStage(), state_.url, CurrentView);
            break;
        case BarAction::Menu:
            FillMenu(menu_, GetStage(), state_, CurrentView);
            break;
        }
    }

    static pal::WebView* CurrentView() {
        return g_screen != nullptr ? g_screen->view_.get() : nullptr;
    }

    void GoBack() {
        if (view_ && state_.can_go_back) {
            view_->Back();
        } else {
            GetStage().Pop();
        }
    }

    void MoveCursor(float dx, float dy) {
        cursor_x_ += dx;
        cursor_y_ += dy;
        ShowPointer(true);
    }

    void ShowPointer(bool on) {
        pointer_ = on;
    }

    int CursorPx(float units) const {
        return static_cast<int>(std::lround(units * scale_));
    }

    // The newest toast, shown in the bar: toasts below the bar are under the view.
    std::string NewestToast() const {
        const auto state = ParseJson(GetStage().StateJson());
        const JsonValue* toasts = state ? state->Find("toasts") : nullptr;
        return toasts && !toasts->items.empty() ? toasts->items.back().string : std::string();
    }

    void DrawMessage(ui::Context& ctx, const std::string& heading, const std::string& text) {
        const ui::Theme& theme = *ctx.theme;
        const float x = view_rect_.x + theme.margin, y = view_rect_.y + theme.margin;
        ctx.canvas->DrawText(heading, x, y, theme.text_heading, theme.fg, gfx::FONT_BOLD);
        ctx.canvas->DrawText(ctx.Text().Elide(text, theme.text_body, view_rect_.w - 2 * theme.margin), x, y + 40,
                             theme.text_body, theme.text_dim);
    }

    void DrawBar(ui::Context& ctx, const gfx::Rect& area) {
        BarInput bar;
        bar.live = view_ != nullptr;
        bar.browser = browser_;
        bar.can_back = state_.can_go_back;
        bar.can_forward = state_.can_go_forward;
        bar.title = !state_.title.empty() ? state_.title : Param("title");
        if (fallback_) {
            state_.url = Param("url"); // the bar names the site the page was opened for
        }
        bar.line = pal::WebUrlHost(state_.url);
        if (!arriving_.empty()) {
            const Arriving& item = arriving_.back();
            bar.line += item.total > 0 ? std::format("  -  downloading {} {}%", item.download.name, item.received * 100 / item.total)
                                       : std::format("  -  downloading {}", item.download.name);
            if (item.total > 0) {
                bar.progress = static_cast<float>(item.received) / static_cast<float>(item.total);
            }
        } else if (const std::string toast = NewestToast(); !toast.empty()) {
            bar.line = toast;
            setting_up_.clear();
        } else if (!setting_up_.empty() && pal::NowSeconds() - setting_up_at_ < 120) {
            bar.line += "  -  adding " + setting_up_ + " to the library...";
        } else if (state_.loading) {
            bar.line += "  -  loading";
        }
        buttons_ = DrawWebBar(ctx, area, bar);
    }

    std::unique_ptr<pal::WebView> view_;
    std::unique_ptr<Fallback> fallback_; // set instead of view_ when there is no engine
    pal::WebState state_;
    Opener opener_;
    std::vector<std::string> hosts_, wanted_;
    std::vector<Arriving> arriving_;
    std::vector<Download> finished_;
    std::vector<BarHit> buttons_;
    BrowserMenu menu_;
    std::string refused_host_;
    gfx::Rect view_rect_;
    double refused_at_ = 0;
    std::string setting_up_; // a download the browser plugin is setting up
    double setting_up_at_ = 0;
    float cursor_x_ = -1, cursor_y_ = -1; // units inside the view
    float scroll_x_ = 0, scroll_y_ = 0;   // pixels not yet sent
    float scale_ = 1;
    bool started_ = false, pointer_ = false, stick_ = false, browser_ = false;
};

} // namespace

const PageState& CurrentState() {
    return g_state;
}

void SetCursor(float x, float y) {
    if (g_screen) {
        g_screen->SetCursorAt(x, y);
    }
}

DEN_REGISTER_SCREEN("web", WebScreen);

} // namespace den::app::web
