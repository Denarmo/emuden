// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser's menu (Start on the browser page): the address bar, bookmarks, reload and
// close, as a list the pad moves through. The page hides its web view while the menu is open,
// since the view would cover it.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "den/app/app.h"
#include "den/platform/webview.h"

namespace den::app::web {

class BrowserMenu {
public:
    struct Item {
        std::string label;
        std::function<void()> action;
    };

    void Open(std::vector<Item> items);
    void Close() {
        items_.clear();
    }
    bool IsOpen() const {
        return !items_.empty();
    }
    /// Up, Down, A (runs the item and closes), B or Start (closes).
    void Nav(input::Nav nav);
    /// A tap at a point (logical units): runs the item under it; outside the list closes it.
    void Tap(float x, float y);
    void Draw(ui::Context& ctx, const gfx::Rect& area);
    /// The labels, for the automation endpoint.
    std::vector<std::string> Labels() const;

private:
    void Run(std::size_t index);

    std::vector<Item> items_;
    std::vector<gfx::Rect> rects_;
    gfx::Rect panel_;
    std::size_t focus_ = 0;
};

/// The page whose view the menu acts on, null once it is closed.
using ViewGetter = std::function<pal::WebView*()>;

/// Opens the text entry with the page's address; what the user types is opened (AddressFor).
void AskAddress(ui::Stage& stage, const std::string& current, ViewGetter view);
/// Fills the menu for the page at `state`: Address, Bookmark (or Remove bookmark), the
/// bookmarks, Reload, Close the browser.
void FillMenu(BrowserMenu& menu, ui::Stage& stage, const pal::WebState& state, ViewGetter view);

} // namespace den::app::web
