// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web_menu.h"

#include <algorithm>
#include <format>

#include "den/ui/glyphs.h"
#include "web_browser.h"

namespace den::app::web {

namespace {

constexpr float ROW = 52;
constexpr float WIDTH = 520;

} // namespace

void BrowserMenu::Open(std::vector<Item> items) {
    items_ = std::move(items);
    focus_ = 0;
}

void BrowserMenu::Nav(input::Nav nav) {
    using input::Nav;
    if (items_.empty()) {
        return;
    }
    switch (nav) {
    case Nav::Up:
        focus_ = focus_ == 0 ? items_.size() - 1 : focus_ - 1;
        break;
    case Nav::Down:
        focus_ = (focus_ + 1) % items_.size();
        break;
    case Nav::Confirm:
        Run(focus_);
        break;
    case Nav::Back:
    case Nav::Menu:
        Close();
        break;
    default:
        break;
    }
}

void BrowserMenu::Tap(float x, float y) {
    for (std::size_t i = 0; i < rects_.size() && i < items_.size(); ++i) {
        if (rects_[i].Contains(x, y)) {
            Run(i);
            return;
        }
    }
    if (!panel_.Contains(x, y)) {
        Close();
    }
}

void BrowserMenu::Run(std::size_t index) {
    // The action may open another menu or close the page: take it out first.
    std::function<void()> action = std::move(items_[index].action);
    Close();
    if (action) {
        action();
    }
}

std::vector<std::string> BrowserMenu::Labels() const {
    std::vector<std::string> out;
    for (const Item& item : items_) {
        out.push_back(item.label);
    }
    return out;
}

void BrowserMenu::Draw(ui::Context& ctx, const gfx::Rect& area) {
    rects_.clear();
    if (items_.empty()) {
        return;
    }
    const ui::Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(area, theme.scrim);
    const float width = std::min(WIDTH, area.w - 32);
    const std::size_t fits = std::max<std::size_t>(1, static_cast<std::size_t>((area.h - 48) / ROW));
    const std::size_t shown = std::min(items_.size(), fits);
    // The rows around the focus when they do not all fit.
    std::size_t first = focus_ >= shown ? focus_ - shown + 1 : 0;
    first = std::min(first, items_.size() - shown);
    panel_ = gfx::Rect{area.Right() - width - 16, area.y + 16, width, static_cast<float>(shown) * ROW + 16};
    canvas.FillRect(panel_, theme.dialog, theme.radius);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i < first || i >= first + shown) {
            rects_.push_back(gfx::Rect{});
            continue;
        }
        const gfx::Rect row{panel_.x + 8, panel_.y + 8 + static_cast<float>(i - first) * ROW, panel_.w - 16, ROW - 4};
        rects_.push_back(row);
        if (i == focus_) {
            canvas.FillRect(row, theme.panel_strong, theme.radius_small);
            canvas.StrokeRect(row, theme.fg, 3, theme.radius_small);
        }
        canvas.DrawText(ctx.Text().Elide(items_[i].label, theme.text_body, row.w - 32), row.x + 16,
                        row.y + (row.h - theme.text_body) * 0.5f - 2, theme.text_body, theme.fg);
    }
}

void AskAddress(ui::Stage& stage, const std::string& current, ViewGetter view) {
    ui::TextRequest request;
    request.title = "Go to an address or search";
    request.text = current;
    request.placeholder = "example.com or words to search for";
    request.max_length = 500;
    stage.AskText(std::move(request), [view = std::move(view)](const std::string& text) {
        const std::string address = AddressFor(text);
        if (pal::WebView* page = view(); page != nullptr && !address.empty()) {
            page->Navigate(address);
        }
    });
}

void FillMenu(BrowserMenu& menu, ui::Stage& stage, const pal::WebState& state, ViewGetter view) {
    std::vector<BrowserMenu::Item> items;
    items.push_back({"Go to an address or search", [&stage, url = state.url, view] { AskAddress(stage, url, view); }});
    if (!state.url.empty() && !state.url.starts_with("about:")) {
        const bool marked = IsBookmarked(state.url);
        items.push_back({marked ? "Remove this bookmark" : "Bookmark this page",
                         [&stage, url = state.url, title = state.title] {
                             stage.Toast(ToggleBookmark(url, title) ? "Bookmarked" : "Bookmark removed");
                         }});
    }
    for (const Bookmark& bookmark : LoadBookmarks()) {
        items.push_back({"Go to " + (bookmark.title.empty() ? bookmark.url : bookmark.title), [view, url = bookmark.url] {
                             if (pal::WebView* page = view()) {
                                 page->Navigate(url);
                             }
                         }});
    }
    items.push_back({"Reload", [view] {
                         if (pal::WebView* page = view()) {
                             page->Reload();
                         }
                     }});
    items.push_back({"Close the browser", [&stage] { stage.Pop(); }});
    menu.Open(std::move(items));
}

} // namespace den::app::web
