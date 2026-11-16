// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser page on a system without a web engine the app can show (SteamOS without
// WebKitGTK, Windows without the WebView2 runtime, ...). The same route and the same opener:
// the page offers to open the site in the user's own browser, then watches the Downloads
// folder and asks "Install <file>?" for each wanted file that appears there afterwards.
// Never a ROM image, never a file that was already there.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "den/app/app.h"
#include "den/platform/downloads.h"
#include "den/app/web_browser.h"

namespace den::app::web {

class Fallback {
public:
    /// `hosts` are the opener's; the address is only opened when it is on one of them.
    Fallback(std::string url, std::vector<std::string> hosts, std::string reason, std::vector<std::string> wanted);

    /// Looks at the Downloads folder about once a second and asks about what it finds.
    void Update(ui::Stage& stage);
    /// `selected`: the button is the selection (not while a question is open above it).
    void Draw(ui::Context& ctx, const gfx::Rect& area, bool selected);
    /// Opens the site in the user's browser.
    void OpenBrowser(ui::Stage& stage);
    /// A tap or click at a point (logical units); true when it was on the button.
    bool Tap(float x, float y, ui::Stage& stage);
    /// Where accepted files are copied: the opener's folder; empty = InboxDir().
    void SetFolder(std::filesystem::path folder) {
        folder_ = std::move(folder);
    }
    /// The files the user said yes to, copied into the inbox; each is handed out once.
    std::vector<Download> TakeAccepted();

    /// The folders watched, for the automation endpoint.
    std::string Watching() const;
    const std::string& Offered() const {
        return offered_;
    }
    int Offers() const {
        return offers_;
    }

private:
    struct Shared {
        std::vector<Download> accepted;
        std::string status;
    };

    std::string url_, host_, reason_;
    std::filesystem::path folder_;
    std::vector<std::string> hosts_, wanted_;
    std::unique_ptr<pal::DownloadWatch> watch_;
    std::vector<pal::DownloadWatch::File> found_; // waiting to be asked about
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::string offered_;
    gfx::Rect button_;
    double next_poll_ = 0;
    int offers_ = 0;
    bool opened_ = false;
};

} // namespace den::app::web
