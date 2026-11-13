// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser page's test endpoint. With EMUDEN_AUTOMATION=<port> it listens on <port>+3, one
// line in and one line out:
//
//   open url=<url> [hosts=a.com,b.com] [ext=bps,zip] [title=<text>]
//                         opens the page for a test opener that notes what it is handed
//   state                 JSON: address, title, loading, history, the view's place, the pad's
//                         pointer, downloads taken and refused, what the opener was handed;
//                         without an engine also the folders watched and the file asked about
//   cursor <x> <y>        puts the pad's pointer at a point of the view (logical units)
//   close                 closes the page
//
// It reports what the app knows about its own page, never anything the page contains.

#include <cstdlib>
#include <memory>
#include <sstream>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "web_browser.h"

namespace den::app::web {

namespace {

constexpr const char* TEST_OPENER = "automation";

struct Endpoint {
    std::unique_ptr<pal::LineServer> server;
    int opener_calls = 0;
    std::string opener_file, opener_name, opener_host;
};

Endpoint& Get() {
    static Endpoint endpoint;
    return endpoint;
}

std::string StateJson() {
    const PageState& s = CurrentState();
    const Endpoint& e = Get();
    const auto number = [](double value) { return JsonNumber(value); };
    std::string text = WriteJson(JsonObject({
        {"open", JsonBool(s.open)},
        {"available", JsonBool(s.available)},
        {"fallback", JsonBool(s.fallback)},
        {"watching", JsonString(s.watching)},
        {"offered", JsonString(s.offered)},
        {"offers", number(s.offers)},
        {"ready", JsonBool(s.ready)},
        {"view_visible", JsonBool(s.view_visible)},
        {"url", JsonString(s.url)},
        {"title", JsonString(s.title)},
        {"loading", JsonBool(s.loading)},
        {"can_go_back", JsonBool(s.can_go_back)},
        {"can_go_forward", JsonBool(s.can_go_forward)},
        {"view", JsonArray({number(s.view_x), number(s.view_y), number(s.view_w), number(s.view_h)})},
        {"pointer", JsonBool(s.pointer)},
        {"cursor", JsonArray({number(s.cursor_x), number(s.cursor_y)})},
        {"downloading", JsonString(s.downloading)},
        {"progress", number(s.progress)},
        {"downloads", number(s.downloads)},
        {"last_download", JsonString(s.last_download)},
        {"refused_downloads", number(s.refused_downloads)},
        {"last_refused_download", JsonString(s.last_refused_download)},
        {"refused_navigations", number(s.refused_navigations)},
        {"last_refused_host", JsonString(s.last_refused_host)},
        {"leave_asked", JsonString(s.leave_asked)},
        {"left_to", JsonString(s.left_to)},
        {"opener_calls", number(e.opener_calls)},
        {"opener_file", JsonString(e.opener_file)},
        {"opener_name", JsonString(e.opener_name)},
        {"opener_host", JsonString(e.opener_host)},
    }));
    std::erase(text, '\n');
    return text;
}

void Pump() {
    Endpoint& endpoint = Get();
    if (!endpoint.server) {
        return;
    }
    while (auto request = endpoint.server->Next()) {
        std::istringstream in(request->line);
        std::string command;
        in >> command;
        std::string reply = "ok";
        if (command == "state") {
            reply = StateJson();
        } else if (command == "open") {
            ui::Params params{{"opener", TEST_OPENER}};
            for (std::string pair; in >> pair;) {
                if (const auto eq = pair.find('='); eq != std::string::npos) {
                    params[pair.substr(0, eq)] = pair.substr(eq + 1);
                }
            }
            if (params["url"].empty()) {
                reply = "error open needs url=<address>";
            } else {
                Svc().stage->Push("web", std::move(params));
            }
        } else if (command == "cursor") {
            float x = 0, y = 0;
            in >> x >> y;
            if (in.fail() || !CurrentState().open) {
                reply = "error cursor needs x y and an open page";
            } else {
                SetCursor(x, y);
            }
        } else if (command == "close") {
            if (CurrentState().open && Svc().stage->Top() && Svc().stage->Top()->Route() == "web") {
                Svc().stage->Pop();
            } else {
                reply = "error the page is not on top";
            }
        } else {
            reply = "error unknown command '" + command + "'";
        }
        endpoint.server->Reply(request->id, reply);
    }
}

void Start() {
    const int port = std::atoi(pal::GetEnv("EMUDEN_AUTOMATION").c_str());
    if (port <= 0) {
        return;
    }
    auto server = pal::StartLineServer(port + 3);
    if (!server) {
        log::Error("automation", "browser test endpoint: {}", server.error());
        return;
    }
    Get().server = server.take();
    log::Info("automation", "browser test endpoint on 127.0.0.1:{}", port + 3);
    Opener opener;
    opener.on_download = [](const Download& download) {
        Endpoint& endpoint = Get();
        ++endpoint.opener_calls;
        endpoint.opener_file = PathToUtf8(download.file);
        endpoint.opener_name = download.name;
        endpoint.opener_host = download.host;
    };
    RegisterOpener(TEST_OPENER, std::move(opener));
}

} // namespace

DEN_REGISTER_STARTUP([] { Start(); });
DEN_REGISTER_FRAME([] { Pump(); });

} // namespace den::app::web
