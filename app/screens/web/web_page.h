// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser page's class, shared by web_screen.cpp (the view, downloads, drawing) and
// web_page_input.cpp (the pad and the bar's buttons). Internal to the browser screens.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "den/app/app.h"
#include "den/platform/webview.h"
#include "web_bar.h"
#include "den/app/web_browser.h"
#include "web_fallback.h"
#include "web_menu.h"

namespace den::app::web {

using input::Nav;

inline constexpr float STICK_DEADZONE = 0.22f;
inline constexpr float POINTER_SPEED = 900; // units per second at full deflection
inline constexpr float SCROLL_SPEED = 1500; // pixels per second at full deflection
inline constexpr float STEP = 28;           // units per D-pad press

class WebScreen;
extern PageState g_state;
extern WebScreen* g_screen;

class WebScreen final : public ui::Page {
public:
    WebScreen();
    ~WebScreen() override;
    void Build() override {}
    void OnShow() override;
    void OnHide() override;
    bool OnEvent(const input::Event& event) override;
    void Update(ui::Context& ctx) override;
    std::vector<ui::Hint> Hints() const override;
    void Draw(ui::Context& ctx, const gfx::Rect& area) override;
    void GoTo(const std::string& url);
    void SetCursorAt(float x, float y);

private:
    struct Arriving {
        std::uint64_t id = 0;
        Download download;
        std::int64_t received = 0, total = -1;
    };

    void Start();
    void StartFallback(const std::string& url, const std::string& reason);
    /// Takes the files the opener asked for and nothing else; never a ROM.
    pal::WebDownloadDecision Judge(const pal::WebDownload& download);
    /// Hands finished files to the opener, from the frame and not from inside the engine's event.
    void Deliver();
    void NoteProgress();
    /// A link to a host outside the page's list: the user is asked, once per press. A yes
    /// allows that host until the page is closed and goes there.
    void Refused(const pal::WebNavigation& navigation);
    void HandleNav(Nav nav, bool repeat);
    void BarPressed(BarAction action);
    static pal::WebView* CurrentView();
    void GoBack();
    void MoveCursor(float dx, float dy);
    void ShowPointer(bool on);
    int CursorPx(float units) const;
    void DrawMessage(ui::Context& ctx, const std::string& heading, const std::string& text);
    void DrawBar(ui::Context& ctx, const gfx::Rect& area);

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

} // namespace den::app::web
