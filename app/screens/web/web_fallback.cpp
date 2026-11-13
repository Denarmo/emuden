// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <format>
#include <sstream>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/platform/webview.h"
#include "den/ui/selection.h"
#include "web_fallback.h"

namespace den::app::web {

namespace {

constexpr double POLL_SECONDS = 1.0;

std::string List(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

/// `text` broken into lines no wider than `width`.
std::vector<std::string> Wrap(gfx::Text& text, const std::string& words, float size, float width) {
    std::vector<std::string> lines;
    std::istringstream in(words);
    std::string line;
    for (std::string word; in >> word;) {
        const std::string longer = line.empty() ? word : line + " " + word;
        if (!line.empty() && text.Measure(longer, size) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = longer;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace

Fallback::Fallback(std::string url, std::vector<std::string> hosts, std::string reason, std::vector<std::string> wanted)
    : url_(std::move(url)), host_(pal::WebUrlHost(url_)), reason_(std::move(reason)), hosts_(std::move(hosts)),
      wanted_(std::move(wanted)) {
    if (wanted_.empty()) {
        wanted_ = DefaultExtensions();
    }
    std::vector<std::filesystem::path> folders = pal::DownloadFolders();
    if (!folders.empty()) {
        // JudgeDownload never takes a ROM image, whatever the opener listed.
        watch_ = std::make_unique<pal::DownloadWatch>(std::move(folders), [wanted = wanted_](const std::string& name) {
            return JudgeDownload(name, wanted) == Verdict::Take;
        });
    }
    shared_->status = watch_ ? "Waiting for a download..." : "";
}

std::string Fallback::Watching() const {
    std::string out;
    if (watch_) {
        for (const std::filesystem::path& folder : watch_->Folders()) {
            out += (out.empty() ? "" : ", ") + PathToUtf8(folder);
        }
    }
    return out;
}

void Fallback::OpenBrowser(ui::Stage& stage) {
    if (!pal::WebUrlAllowed(url_, hosts_)) {
        stage.Toast(host_.empty() ? std::string("That address is not opened") : host_ + " is outside this page: not opened");
        return;
    }
    if (const Status opened = pal::OpenInBrowser(url_); !opened.ok) {
        stage.Toast("The browser could not be opened");
        log::Warn("web", "{}", opened.error);
        return;
    }
    opened_ = true;
    stage.Toast("Opened " + host_ + " in your browser");
}

bool Fallback::Tap(float x, float y, ui::Stage& stage) {
    if (!button_.Contains(x, y)) {
        return false;
    }
    OpenBrowser(stage);
    return true;
}

void Fallback::Update(ui::Stage& stage) {
    const double now = pal::NowSeconds();
    if (watch_ && now >= next_poll_) {
        next_poll_ = now + POLL_SECONDS;
        for (pal::DownloadWatch::File& file : watch_->Poll()) {
            found_.push_back(std::move(file));
        }
    }
    if (found_.empty() || stage.DialogOpen()) {
        return;
    }
    const pal::DownloadWatch::File file = found_.front();
    found_.erase(found_.begin());
    offered_ = file.name;
    ++offers_;
    const std::string folder = PathToUtf8(file.path.parent_path());
    stage.Confirm(
        std::format("Install {}?", file.name),
        std::format("It appeared in {} after this page was opened. EmuDEN takes a copy; your download stays where it is.",
                    folder),
        "Install",
        [shared = shared_, file, host = host_] {
            std::error_code ec;
            std::filesystem::create_directories(InboxDir(), ec);
            Download download;
            download.name = pal::WebSafeFileName(file.name);
            download.file = FreeFile(InboxDir(), download.name);
            download.host = host; // the site the page was opened for; the file came through the user's browser
            std::filesystem::copy_file(file.path, download.file, ec);
            if (ec) {
                shared->status = std::format("{} could not be copied: {}", file.name, ec.message());
                return;
            }
            shared->status = "Installed " + file.name;
            shared->accepted.push_back(std::move(download));
        },
        "Not this one", true);
}

std::vector<Download> Fallback::TakeAccepted() {
    std::vector<Download> out;
    out.swap(shared_->accepted);
    return out;
}

void Fallback::Draw(ui::Context& ctx, const gfx::Rect& area, bool selected) {
    const ui::Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    const float x = area.x + theme.margin, width = std::max(0.0f, std::min(area.w - 2 * theme.margin, 900.0f));
    float y = area.y + theme.margin;
    const std::string site = host_.empty() ? std::string("the site") : host_;
    canvas.DrawText(ctx.Text().Elide(std::format("Open {} in your browser", site), theme.text_title, width, gfx::FONT_BOLD),
                    x, y, theme.text_title, theme.fg, gfx::FONT_BOLD);
    y += theme.text_title + 22;

    const auto paragraph = [&](const std::string& words, gfx::Color color) {
        for (const std::string& line : Wrap(ctx.Text(), words, theme.text_body, width)) {
            canvas.DrawText(line, x, y, theme.text_body, color);
            y += theme.text_body + 8;
        }
        y += 10;
    };
    if (!reason_.empty()) {
        paragraph(reason_, theme.text_dim);
    }
    paragraph("Browse the site in your own browser and press its download button there.", theme.fg);
    if (watch_) {
        paragraph(std::format("EmuDEN watches {} and offers to install the {} files that arrive after now. "
                              "Files that were already there are left alone, and ROM images are never taken.",
                              Watching(), List(wanted_)),
                  theme.fg);
    } else {
        paragraph("EmuDEN found no Downloads folder to watch on this device, so a file downloaded there "
                  "cannot be picked up from this page.",
                  theme.fg);
    }

    // The one control of the page: it is the selection, so it carries the selection's border.
    const std::string label = opened_ ? "Open in browser again" : "Open in browser";
    const float label_w = ctx.Text().Measure(label, theme.text_body, gfx::FONT_BOLD);
    button_ = gfx::Rect{x, y + 6, label_w + 56, 56};
    canvas.FillRect(button_, theme.panel, theme.radius);
    canvas.DrawText(label, button_.x + 28, button_.y + (button_.h - theme.text_body) * 0.5f - 2, theme.text_body, theme.fg,
                    gfx::FONT_BOLD);
    if (selected) {
        ui::DrawSelectionBorder(canvas, button_, theme.radius, pal::NowSeconds());
    }
    y = button_.Bottom() + 28;

    if (!shared_->status.empty()) {
        canvas.DrawText(ctx.Text().Elide(shared_->status, theme.text_body, width), x, y, theme.text_body, theme.text_dim);
    }
}

} // namespace den::app::web
