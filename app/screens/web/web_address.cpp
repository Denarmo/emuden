// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser page's address bar and bookmarks (<data>/browser/bookmarks.json).

#include <algorithm>
#include <cctype>
#include <format>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/base/json.h"
#include "den/platform/webview.h"
#include "web_browser.h"

namespace den::app::web {

namespace {

std::string Encode(const std::string& text) {
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out += std::format("%{:02X}", byte);
        }
    }
    return out;
}

} // namespace

std::string AddressFor(const std::string& typed) {
    const std::string text = Trim(typed);
    if (text.empty()) {
        return {};
    }
    const std::string lower = ToLower(text);
    if (lower.starts_with("https://") || lower.starts_with("http://")) {
        return text;
    }
    // "example.com/page" is an address; "pokemon hacks" or "tetris" a search.
    const bool spaces = text.find(' ') != std::string::npos;
    const std::size_t dot = text.find('.');
    const std::size_t slash = text.find('/');
    const bool host_like = dot != std::string::npos && dot > 0 && dot + 1 < text.size() &&
                           (slash == std::string::npos || dot < slash);
    if (!spaces && (host_like || lower.starts_with("localhost"))) {
        return "https://" + text;
    }
    return std::string(SEARCH_ADDRESS) + Encode(text);
}

std::filesystem::path BookmarksFile() {
    return Svc().data_dir / "browser" / "bookmarks.json";
}

std::vector<Bookmark> LoadBookmarks() {
    std::vector<Bookmark> out;
    const auto text = ReadTextFile(BookmarksFile());
    const auto json = text ? ParseJson(*text) : std::nullopt;
    if (json && json->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : json->items) {
            if (const std::string url = item.String("url"); !url.empty()) {
                out.push_back({item.String("title"), url});
            }
        }
    }
    return out;
}

void SaveBookmarks(const std::vector<Bookmark>& bookmarks) {
    std::vector<JsonValue> items;
    for (const Bookmark& bookmark : bookmarks) {
        items.push_back(JsonObject({{"title", JsonString(bookmark.title)}, {"url", JsonString(bookmark.url)}}));
    }
    std::error_code ec;
    std::filesystem::create_directories(BookmarksFile().parent_path(), ec);
    WriteFileAtomic(BookmarksFile(), WriteJson(JsonArray(std::move(items))));
}

bool ToggleBookmark(const std::string& url, const std::string& title) {
    std::vector<Bookmark> bookmarks = LoadBookmarks();
    const auto found = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& b) { return b.url == url; });
    const bool adding = found == bookmarks.end();
    if (adding) {
        bookmarks.push_back({title.empty() ? pal::WebUrlHost(url) : title, url});
    } else {
        bookmarks.erase(found);
    }
    SaveBookmarks(bookmarks);
    return adding;
}

bool IsBookmarked(const std::string& url) {
    const std::vector<Bookmark> bookmarks = LoadBookmarks();
    return std::any_of(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& b) { return b.url == url; });
}

} // namespace den::app::web
