// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser page's openers and its inbox; what they take is in web_rules.cpp.

#include <algorithm>
#include <chrono>
#include <map>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "web_browser.h"

namespace den::app::web {

namespace {

std::map<std::string, Opener>& Openers() {
    static std::map<std::string, Opener> openers;
    return openers;
}

// A download nobody collected (its opener was gone, or the app was closed first) does not
// stay for ever: what is older than a day goes when the app starts.
void CleanInbox() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(24);
    std::vector<fs::path> old;
    for (fs::directory_iterator it(InboxDir(), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code failed;
        const auto written = fs::last_write_time(it->path(), failed);
        if (!failed && written < limit) {
            old.push_back(it->path());
        }
    }
    for (const fs::path& path : old) {
        fs::remove_all(path, ec);
    }
}

std::string Join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ",") + item;
    }
    return out;
}

} // namespace

std::filesystem::path InboxDir() {
    return Svc().data_dir / "inbox" / "web";
}

void RegisterOpener(const std::string& name, Opener opener) {
    CleanExtensions(opener.extensions);
    Openers()[name] = std::move(opener);
}

const Opener* FindOpener(const std::string& name) {
    const auto it = Openers().find(name);
    return it == Openers().end() ? nullptr : &it->second;
}

DEN_REGISTER_STARTUP(CleanInbox);

void Open(OpenRequest request) {
    // One visit at a time: the name is reused, so openers do not pile up.
    RegisterOpener("request", std::move(request.opener));
    Svc().stage->Push("web", {{"url", request.url},
                              {"title", request.title},
                              {"hosts", request.browser ? std::string(ANY) : Join(request.allowed_hosts)},
                              {"opener", "request"},
                              {"mode", request.browser ? "browser" : ""}});
}

} // namespace den::app::web
