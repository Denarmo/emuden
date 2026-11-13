// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser page. The kit draws the bar at the top and the hints at the bottom; the web
// view is a native view over the rest. A native view sits above everything the app draws, so
// it is hidden while a dialog or the text entry is open and pushed down while toasts show.
//
// Pad: the left stick (or the D-pad) moves a pointer, A clicks, the right stick scrolls, B
// goes back in the page's history or closes the page, X types text into the page, View
// presses Enter, LB and RB go back and forward, Y reloads, Start closes. Mouse and touch go
// straight to the page.

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/platform/webview.h"
#include "den/ui/glyphs.h"
#include "web_browser.h"

namespace den::app::web {

namespace {

using input::Nav;

constexpr float BAR_HEIGHT = 72;
constexpr float BUTTON = 48;
constexpr float STICK_DEADZONE = 0.22f;
constexpr float POINTER_SPEED = 900;  // units per second at full deflection
constexpr float SCROLL_SPEED = 1500;  // pixels per second at full deflection
constexpr float STEP = 28;            // units per D-pad press

PageState g_state;
class WebScreen;
WebScreen* g_screen = nullptr;

std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string item; std::getline(in, item, ',');) {
        if (item = ToLower(Trim(item)); !item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

/// A file of the inbox that does not exist yet: "hack.bps", then "hack (2).bps", ...
std::filesystem::path FreeFile(const std::filesystem::path& dir, const std::string& name) {
    const std::filesystem::path wanted = PathFromUtf8(name);
    std::filesystem::path file = dir / wanted;
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(file, ec) && n < 1000; ++n) {
        file = dir / PathFromUtf8(std::format("{} ({}){}", PathToUtf8(wanted.stem()), n, PathToUtf8(wanted.extension())));
    }
    return file;
}

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
            for (const BarButton& button : buttons_) {
                if (button.enabled && button.rect.Contains(event.x / scale, event.y / scale)) {
                    button.action();
                    break;
                }
            }
            return true;
        default:
            return true;
        }
    }

    void Update(ui::Context& ctx) override {
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
            return {{Nav::Back, "Close"}};
        }
        return {{Nav::Confirm, "Click"}, {Nav::Option, "Type"}, {Nav::Extra, "Reload"},
                {Nav::Back, state_.can_go_back ? "Back" : "Close"}, {Nav::Menu, "Close"}};
    }

    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        gfx::Canvas& canvas = *ctx.canvas;
        const float scale = canvas.Scale();
        scale_ = scale;
        // Toasts are drawn from 24 units down, 52 apart: the view starts below them.
        const float top = std::max(BAR_HEIGHT, 16.0f + 52.0f * static_cast<float>(ToastCount()));
        view_rect_ = gfx::Rect{area.x, top, area.w, std::max(0.0f, area.h - top)};
        DrawBar(ctx, area);
        if (!view_) {
            DrawMessage(ctx, "The in-app browser is not available", reason_);
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
        const bool covered = GetStage().DialogOpen() || bounds.w <= 0 || bounds.h <= 0;
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
    }

    void SetCursorAt(float x, float y) {
        cursor_x_ = x;
        cursor_y_ = y;
        ShowPointer(true);
    }

private:
    struct BarButton {
        gfx::Rect rect;
        bool enabled = true;
        std::function<void()> action;
    };
    struct Arriving {
        std::uint64_t id = 0;
        Download download;
        std::int64_t received = 0, total = -1;
    };

    void Start() {
        const std::string url = Param("url");
        hosts_ = Split(Param("hosts"));
        if (hosts_.empty()) {
            if (const std::string host = pal::WebUrlHost(url); !host.empty()) {
                hosts_.push_back(host);
            }
        }
        if (const Opener* opener = FindOpener(Param("opener"))) {
            opener_ = *opener;
        }
        wanted_ = Split(Param("ext"));
        std::erase_if(wanted_, [](const std::string& e) { return IsRomImage(e); });
        if (wanted_.empty()) {
            wanted_ = opener_.extensions;
        }
        g_state.available = pal::WebView::Available();
        if (!g_state.available) {
            reason_ = pal::WebView::UnavailableReason();
            return;
        }
        pal::WebViewConfig config;
        config.user_data_dir = Svc().data_dir / "webview";
        config.allowed_hosts = hosts_;
        auto view = pal::WebView::Create(*Svc().window, pal::WebRect{}, std::move(config));
        if (!view) {
            reason_ = view.error();
            g_state.available = false;
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

    /// Takes the files the opener asked for and nothing else; never a ROM.
    pal::WebDownloadDecision Judge(const pal::WebDownload& download) {
        const Verdict verdict = JudgeDownload(download.file_name, wanted_);
        if (verdict != Verdict::Take) {
            ++g_state.refused_downloads;
            g_state.last_refused_download = download.file_name;
            GetStage().Toast(verdict == Verdict::RomImage
                                 ? std::format("{} is a ROM image: EmuDEN does not download ROMs", download.file_name)
                                 : std::format("{} is not a file this page takes", download.file_name));
            return pal::WebDownloadDecision::Refuse();
        }
        std::error_code ec;
        std::filesystem::create_directories(InboxDir(), ec);
        Arriving item;
        item.id = download.id;
        item.total = download.size;
        item.download.file = FreeFile(InboxDir(), download.file_name);
        item.download.name = download.file_name;
        item.download.host = download.host;
        item.download.mime = download.mime;
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
            if (Svc().stage) {
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

    void Refused(const pal::WebNavigation& navigation) {
        ++g_state.refused_navigations;
        g_state.last_refused_host = navigation.host;
        const double now = pal::NowSeconds();
        if (navigation.host == refused_host_ && now - refused_at_ < 1.5) {
            return; // one toast per press
        }
        refused_host_ = navigation.host;
        refused_at_ = now;
        GetStage().Toast(navigation.host.empty() ? std::string("That link is not opened here")
                                                 : std::format("{} is outside this page: not opened", navigation.host));
    }

    void HandleNav(Nav nav, bool repeat) {
        const bool usable = view_ && view_->Visible();
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
            if (usable && !repeat) {
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

    int ToastCount() const {
        const auto state = ParseJson(GetStage().StateJson());
        const JsonValue* toasts = state ? state->Find("toasts") : nullptr;
        return toasts ? static_cast<int>(toasts->items.size()) : 0;
    }

    void DrawMessage(ui::Context& ctx, const std::string& heading, const std::string& text) {
        const ui::Theme& theme = *ctx.theme;
        const float x = view_rect_.x + theme.margin, y = view_rect_.y + theme.margin;
        ctx.canvas->DrawText(heading, x, y, theme.text_heading, theme.fg, gfx::FONT_BOLD);
        ctx.canvas->DrawText(ctx.Text().Elide(text, theme.text_body, view_rect_.w - 2 * theme.margin), x, y + 40,
                             theme.text_body, theme.text_dim);
    }

    void DrawBar(ui::Context& ctx, const gfx::Rect& area) {
        const ui::Theme& theme = *ctx.theme;
        gfx::Canvas& canvas = *ctx.canvas;
        canvas.FillRect(gfx::Rect{area.x, 0, area.w, BAR_HEIGHT}, theme.panel);
        buttons_.clear();
        const float y = (BAR_HEIGHT - BUTTON) * 0.5f;
        const auto button = [&](float x, bool enabled, std::function<void()> action) {
            const gfx::Rect rect{x, y, BUTTON, BUTTON};
            canvas.FillRect(rect, theme.panel, theme.radius);
            buttons_.push_back({rect, enabled, std::move(action)});
            return rect;
        };
        const auto ink = [&](bool enabled) { return enabled ? theme.fg : theme.text_dim.WithAlpha(0.35f); };
        const bool live = view_ != nullptr;

        // Back and forward: chevrons.
        gfx::Rect r = button(16, live && state_.can_go_back, [this] { view_->Back(); });
        ui::DrawLine(canvas, r.CenterX() + 5, r.y + 14, r.CenterX() - 5, r.CenterY(), 3, ink(live && state_.can_go_back));
        ui::DrawLine(canvas, r.CenterX() - 5, r.CenterY(), r.CenterX() + 5, r.Bottom() - 14, 3, ink(live && state_.can_go_back));
        r = button(72, live && state_.can_go_forward, [this] { view_->Forward(); });
        ui::DrawLine(canvas, r.CenterX() - 5, r.y + 14, r.CenterX() + 5, r.CenterY(), 3, ink(live && state_.can_go_forward));
        ui::DrawLine(canvas, r.CenterX() + 5, r.CenterY(), r.CenterX() - 5, r.Bottom() - 14, 3, ink(live && state_.can_go_forward));
        // Reload: a ring with an arrow head.
        r = button(128, live, [this] { view_->Reload(); });
        canvas.StrokeRect(gfx::Rect{r.CenterX() - 10, r.CenterY() - 10, 20, 20}, ink(live), 3, 10);
        ui::DrawLine(canvas, r.CenterX() + 10, r.CenterY() - 2, r.CenterX() + 15, r.CenterY() - 9, 3, ink(live));
        ui::DrawLine(canvas, r.CenterX() + 10, r.CenterY() - 2, r.CenterX() + 2, r.CenterY() - 5, 3, ink(live));
        // Close: a cross.
        r = button(area.Right() - 16 - BUTTON, true, [this] { GetStage().Pop(); });
        ui::DrawLine(canvas, r.CenterX() - 8, r.CenterY() - 8, r.CenterX() + 8, r.CenterY() + 8, 3, theme.fg);
        ui::DrawLine(canvas, r.CenterX() + 8, r.CenterY() - 8, r.CenterX() - 8, r.CenterY() + 8, 3, theme.fg);

        const float text_x = 192, text_w = std::max(0.0f, area.Right() - 16 - BUTTON - 16 - text_x);
        const std::string title = !state_.title.empty() ? state_.title : Param("title");
        canvas.DrawText(ctx.Text().Elide(title, theme.text_body, text_w, gfx::FONT_BOLD), text_x, 12, theme.text_body,
                        theme.fg, gfx::FONT_BOLD);
        std::string line = pal::WebUrlHost(state_.url);
        if (!arriving_.empty()) {
            const Arriving& item = arriving_.back();
            line += item.total > 0 ? std::format("  -  downloading {} {}%", item.download.name, item.received * 100 / item.total)
                                   : std::format("  -  downloading {}", item.download.name);
        } else if (state_.loading) {
            line += "  -  loading";
        }
        canvas.DrawText(ctx.Text().Elide(line, theme.text_small, text_w), text_x, 42, theme.text_small, theme.text_dim);
        if (!arriving_.empty() && arriving_.back().total > 0) {
            const float done = static_cast<float>(arriving_.back().received) / static_cast<float>(arriving_.back().total);
            canvas.FillRect(gfx::Rect{area.x, BAR_HEIGHT - 3, area.w * std::clamp(done, 0.0f, 1.0f), 3}, theme.fg);
        }
    }

    std::unique_ptr<pal::WebView> view_;
    pal::WebState state_;
    Opener opener_;
    std::vector<std::string> hosts_, wanted_;
    std::vector<Arriving> arriving_;
    std::vector<Download> finished_;
    std::vector<BarButton> buttons_;
    std::string reason_, refused_host_;
    gfx::Rect view_rect_;
    double refused_at_ = 0;
    float cursor_x_ = -1, cursor_y_ = -1; // units inside the view
    float scroll_x_ = 0, scroll_y_ = 0;   // pixels not yet sent
    float scale_ = 1;
    bool started_ = false, pointer_ = false, stick_ = false;
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
