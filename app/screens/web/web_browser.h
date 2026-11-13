// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The in-app browser page (route "web"): a site shown with the platform's web engine, browsed
// by the user. When the user starts a download of a file the opener asked for, the file lands
// in the app's inbox and the opener gets its path. The app never reads the page.
// See docs/in-app-browser.md.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace den::app::web {

/// A file the user downloaded in the page.
struct Download {
    std::filesystem::path file; // in the inbox; it is the opener's from here on
    std::string name;           // the name the site gave it
    std::string host;           // where it came from
    std::string mime;
};

/// Who opened the page and what they take from it.
struct Opener {
    /// File extensions wanted, lower case without the dot; empty = DefaultExtensions(). ROM
    /// image extensions are dropped: this page never saves a ROM.
    std::vector<std::string> extensions;
    /// Runs on the main thread once a wanted file is complete.
    std::function<void(const Download&)> on_download;
};

struct OpenRequest {
    std::string url;
    std::string title;                      // shown until the page has a title of its own
    std::vector<std::string> allowed_hosts; // empty = the host of `url`
    Opener opener;
};

/// Opens the page for this request.
void Open(OpenRequest request);
/// Names an opener, for routes pushed by hand:
/// stage.Push("web", {{"url", ...}, {"hosts", "a.com,b.com"}, {"title", ...}, {"opener", name}}).
/// The optional parameter "ext" ("bps,zip") narrows the extensions for that visit.
void RegisterOpener(const std::string& name, Opener opener);
/// The opener registered under `name`, null when there is none.
const Opener* FindOpener(const std::string& name);

/// Patch and archive formats: bps, ips, ups, xdelta, vcdiff, aps, ppf, zip, 7z.
const std::vector<std::string>& DefaultExtensions();
/// The extension of a file name in lower case without the dot.
std::string ExtensionOf(const std::string& file_name);
/// Cartridge, disc and card images (gb, gba, nds, iso, chd, ...).
bool IsRomImage(const std::string& extension);
enum class Verdict { Take, NotWanted, RomImage };
Verdict JudgeDownload(const std::string& file_name, const std::vector<std::string>& wanted);
/// `<data>/inbox/web`: where downloads arrive.
std::filesystem::path InboxDir();

/// What the page is doing, for the automation endpoint. Never anything of the page's content.
struct PageState {
    bool open = false;
    bool available = false;
    bool ready = false;
    bool view_visible = false;
    bool loading = false;
    bool can_go_back = false, can_go_forward = false;
    bool pointer = false; // the pad's pointer is shown
    std::string url, title;
    float cursor_x = 0, cursor_y = 0; // logical units inside the view
    float view_x = 0, view_y = 0, view_w = 0, view_h = 0;
    std::string downloading; // the file arriving now
    int progress = -1;       // its percentage, -1 when the size is unknown
    std::string last_download;
    std::string last_refused_download;
    std::string last_refused_host;
    int downloads = 0, refused_downloads = 0, refused_navigations = 0;
};
const PageState& CurrentState();
/// Puts the pad's pointer at a point of the view (logical units) and shows it.
void SetCursor(float x, float y);

} // namespace den::app::web
