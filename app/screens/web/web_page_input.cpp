// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser page's pad and bar buttons (web_page.h).

#include <format>

#include "den/ui/glyphs.h"
#include "../companion/second_screen.h"
#include "web_page.h"

namespace den::app::web {

void WebScreen::HandleNav(Nav nav, bool repeat) {
    if (menu_.IsOpen()) {
        if (!repeat || nav == Nav::Up || nav == Nav::Down) {
            menu_.Nav(nav);
        }
        return;
    }
    if (browser_ && nav == Nav::View && !repeat && (SecondScreenReady() || CompanionPad())) {
        SetCompanionPad(!CompanionPad());
        return;
    }
    if (CompanionPad()) {
        CompanionNav(nav);
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

void WebScreen::BarPressed(BarAction action) {
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

void WebScreen::DrawMessage(ui::Context& ctx, const std::string& heading, const std::string& text) {
    const ui::Theme& theme = *ctx.theme;
    const float x = view_rect_.x + theme.margin, y = view_rect_.y + theme.margin;
    ctx.canvas->DrawText(heading, x, y, theme.text_heading, theme.fg, gfx::FONT_BOLD);
    ctx.canvas->DrawText(ctx.Text().Elide(text, theme.text_body, view_rect_.w - 2 * theme.margin), x, y + 40,
                         theme.text_body, theme.text_dim);
}

void WebScreen::DrawBar(ui::Context& ctx, const gfx::Rect& area) {
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

} // namespace den::app::web
