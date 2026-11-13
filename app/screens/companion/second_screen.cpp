// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Drives the second screen every frame: decides whether it is wanted, routes its presses,
// and draws the companion stage (or the client that claimed the screen) into it before the
// main window's frame.
//
// With EMUDEN_AUTOMATION=<port> a test endpoint listens on <port>+2, one line in and one
// line out:
//   state                  JSON: present, ready, size, route, claimed, overlay, keys, game
//   tap <x> <y>            a press at a point in the second screen's logical units
//   drag <x1> <y1> <x2> <y2>  a finger dragged between two points over a few frames
//   screenshot <png path>  the second screen's next frame

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <format>
#include <memory>
#include <sstream>
#include <vector>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "../player/player_screen.h"
#include "den/platform/lifecycle.h"
#include "den/platform/second_display.h"
#include "second_screen.h"

namespace den::app {

namespace {

constexpr const char* ENABLED_KEY = "ui.companion.enabled";
constexpr const char* MODES[] = {"auto", "on", "off"};

struct Controller {
    std::unique_ptr<ui::Stage> stage;
    SecondScreenClient client;
    SecondScreenClient overlay; // drawn instead of the pages or the claim
    bool overlaid = false;
    bool claimed = false;
    bool ready_logged = false;
    std::string base; // the page the stage rests on: browsing or quick settings
    float scale = 1.0f;
    double last_frame = 0;
    std::vector<pal::SecondPointer> pointer;
    std::deque<pal::SecondPointer> script; // the test endpoint's drag, one event per frame
    // The game the main screen is about.
    std::string game;
    std::string focus_seen;
    std::uint64_t library_seen = 0;
    // The test endpoint.
    std::unique_ptr<pal::LineServer> server;
    std::uint64_t shot_id = 0;
    std::string shot_path;
};

Controller& Get() {
    static Controller controller;
    return controller;
}

int ModeIndex() {
    const std::string mode = Svc().settings->GetString(ENABLED_KEY, MODES[0]);
    return mode == MODES[1] ? 1 : mode == MODES[2] ? 2 : 0;
}

// The library entry a node id ends in ("home.recent.<game id>").
std::string GameInId(const std::string& id) {
    for (std::size_t dot = id.find('.'); dot != std::string::npos; dot = id.find('.', dot + 1)) {
        const std::string tail = id.substr(dot + 1);
        if (!tail.empty() && Svc().library->Find(tail)) {
            return tail;
        }
    }
    return {};
}

void TrackGame(Controller& c) {
    Services& svc = Svc();
    const ui::Page* top = svc.stage->Top();
    if (!top) {
        return;
    }
    const std::string focus = top->Focus() ? top->Focus()->id : std::string();
    const std::string seen = top->Route() + "|" + top->Param("id") + "|" + focus;
    if (seen == c.focus_seen && svc.library->Revision() == c.library_seen) {
        return;
    }
    c.focus_seen = seen;
    c.library_seen = svc.library->Revision();
    std::string game = top->Param("id");
    if (game.empty() || !svc.library->Find(game)) {
        game = GameInId(focus);
    }
    if (!game.empty()) {
        c.game = game;
    } else if (c.game.empty() || !svc.library->Find(c.game)) {
        const auto recent = svc.library->RecentlyPlayed(1);
        c.game = recent.empty() ? std::string() : recent.front().id;
    }
}

input::Event PointerEvent(const pal::SecondPointer& p) {
    input::Event event;
    event.type = p.kind == pal::SecondPointer::Kind::Down   ? input::Event::Type::PointerDown
                 : p.kind == pal::SecondPointer::Kind::Move ? input::Event::Type::PointerMove
                                                            : input::Event::Type::PointerUp;
    event.x = p.x;
    event.y = p.y;
    event.down = p.kind != pal::SecondPointer::Kind::Up;
    return event;
}

void PumpEndpoint(Controller& c) {
    if (!c.server) {
        return;
    }
    while (const auto request = c.server->Next()) {
        std::istringstream in(request->line);
        std::string command;
        in >> command;
        std::string reply = "ok";
        pal::SecondDisplay& display = pal::GetSecondDisplay();
        if (command == "state") {
            int w = 0, h = 0;
            display.PixelSize(w, h);
            const ui::Page* page = c.stage ? c.stage->Top() : nullptr;
            const ui::TextEntry* typing = Svc().stage ? Svc().stage->Entry() : nullptr;
            reply = std::format("{{\"present\":{},\"ready\":{},\"width\":{},\"height\":{},\"route\":\"{}\","
                                "\"claimed\":{},\"overlay\":{},\"keys\":{},\"game\":\"{}\"}}",
                                display.Present(), display.Ready(), w, h, page ? page->Route() : "",
                                c.claimed, c.overlaid, typing && typing->KeysApart(), c.game);
        } else if (command == "tap") {
            float x = 0, y = 0;
            in >> x >> y;
            c.pointer.push_back({pal::SecondPointer::Kind::Down, x * c.scale, y * c.scale});
            c.pointer.push_back({pal::SecondPointer::Kind::Up, x * c.scale, y * c.scale});
        } else if (command == "drag") {
            // A finger that goes down, moves over some frames and lifts.
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            in >> x0 >> y0 >> x1 >> y1;
            constexpr int STEPS = 8;
            c.script.push_back({pal::SecondPointer::Kind::Down, x0 * c.scale, y0 * c.scale});
            for (int i = 1; i <= STEPS; ++i) {
                const float t = static_cast<float>(i) / STEPS;
                c.script.push_back({pal::SecondPointer::Kind::Move, (x0 + (x1 - x0) * t) * c.scale,
                                    (y0 + (y1 - y0) * t) * c.scale});
            }
            c.script.push_back({pal::SecondPointer::Kind::Up, x1 * c.scale, y1 * c.scale});
        } else if (command == "screenshot") {
            std::string path;
            std::getline(in, path);
            path = Trim(path);
            if (path.empty() || !display.Ready()) {
                reply = path.empty() ? "error screenshot needs a file path" : "error no second screen";
            } else {
                if (c.shot_id) {
                    c.server->Reply(c.shot_id, "error replaced by a newer screenshot");
                }
                c.shot_id = request->id;
                c.shot_path = path;
                continue; // answered once the frame is drawn
            }
        } else {
            reply = "error unknown command '" + command + "'";
        }
        c.server->Reply(request->id, reply);
    }
}

void TakeShot(Controller& c, int width, int height) {
    if (!c.shot_id) {
        return;
    }
    const std::vector<std::uint8_t> pixels = Svc().device->ReadPixels(width, height);
    const std::filesystem::path file = PathFromUtf8(c.shot_path);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const Status saved = gfx::SavePng(file, width, height, pixels.data());
    c.server->Reply(c.shot_id, saved ? std::format("ok {}x{}", width, height) : "error " + saved.error);
    c.shot_id = 0;
}

void Start() {
    Controller& c = Get();
    c.stage = std::make_unique<ui::Stage>();
    // The companion pages register like every screen; their routes start with "companion.".
    for (const auto& [route, factory] : Registry::Get().Screens()) {
        if (route.rfind("companion.", 0) == 0) {
            c.stage->Register(route, factory);
        }
    }
    c.stage->on_back_at_root = [] {};
    if (const int port = std::atoi(pal::GetEnv("EMUDEN_AUTOMATION").c_str()); port > 0) {
        if (auto server = pal::StartLineServer(port + 2)) {
            c.server = std::move(*server);
        }
    }
}

void Frame() {
    Services& svc = Svc();
    Controller& c = Get();
    if (!c.stage || !svc.stage) {
        return;
    }
    pal::SecondDisplay& display = pal::GetSecondDisplay();
    const int mode = ModeIndex();
    bool wanted = c.claimed || mode == 1 || (mode == 0 && display.Present());
    if (pal::MainWindowHidden()) {
        // Another app covers the main window (Android). The second screen stays only for a
        // game that plays there; otherwise the app is left and gives it up as well.
        const PlayerScreen* playing = PlayerScreen::Current();
        wanted = wanted && playing && playing->WantsSecond();
    }
    display.SetWanted(wanted);
    c.pointer.clear();
    display.TakePointer(c.pointer);
    PumpEndpoint(c);
    if (!c.script.empty()) {
        c.pointer.push_back(c.script.front());
        c.script.pop_front();
    }
    if (!wanted || !display.Ready()) {
        c.ready_logged = false;
        return;
    }
    int width = 0, height = 0;
    display.PixelSize(width, height);
    if (width <= 0 || height <= 0) {
        return;
    }
    if (!c.ready_logged) {
        c.ready_logged = true;
        log::Info("companion", "second screen {}x{}", width, height);
    }
    c.scale = static_cast<float>(height) / COMPANION_HEIGHT;

    if (!c.claimed) {
        TrackGame(c);
        const player::State state = svc.player->GetSnapshot().state;
        // The quick settings while a game runs; the focused game while browsing; under the
        // Apps section, which is about no game, a page that says so.
        const ui::Page* top = svc.stage->Top();
        const char* route = state == player::State::Running || state == player::State::Paused
                                ? "companion.play"
                                : top && top->Route() == "apps" ? "companion.idle"
                                                                : "companion.browse";
        if (!c.stage->Top()) {
            c.stage->Push(route);
        } else if (c.base != route) {
            // Another base page: what was opened over the old one (the apps) goes with it.
            c.stage->PopToRoot();
            c.stage->Replace(route);
        }
        c.base = route;
    }
    // A text entry of the main window whose own keys belong here (the system keyboard did
    // not come): they are drawn over everything else and take the presses.
    ui::TextEntry* typing = svc.stage->Entry();
    const bool keys = typing && typing->KeysApart();
    for (const pal::SecondPointer& p : c.pointer) {
        const input::Event event = PointerEvent(p);
        if (keys) {
            typing->KeysPointer(event, c.scale);
        } else if (c.overlaid) {
            if (c.overlay.pointer) {
                c.overlay.pointer(event);
            }
        } else if (!c.claimed) {
            c.stage->HandleEvent(event);
        } else if (c.client.pointer) {
            c.client.pointer(event);
        }
    }

    if (!display.BeginFrame()) {
        return;
    }
    const double now = pal::NowSeconds();
    svc.device->BeginFrame(width, height, svc.theme->page);
    svc.canvas->Begin(width, height, c.scale);
    ui::Context ctx;
    ctx.canvas = svc.canvas;
    ctx.textures = svc.textures;
    ctx.theme = svc.theme;
    ctx.family = svc.input->ActiveFamily();
    ctx.dt = static_cast<float>(std::clamp(now - c.last_frame, 0.0, 0.1));
    ctx.time = now;
    c.last_frame = now;
    if (keys) {
        typing->DrawKeys(ctx);
    } else if (c.overlaid) {
        if (c.overlay.draw) {
            c.overlay.draw(ctx, gfx::Rect{0, 0, svc.canvas->Width(), svc.canvas->Height()});
        }
    } else if (!c.claimed) {
        c.stage->Frame(ctx);
    } else if (c.client.draw) {
        c.client.draw(ctx, gfx::Rect{0, 0, svc.canvas->Width(), svc.canvas->Height()});
    }
    svc.canvas->End();
    TakeShot(c, width, height);
    display.EndFrame();
}

std::vector<ui::RowSpec> Rows() {
    ui::RowSpec row = ChoiceRow(
        ENABLED_KEY, "Second screen", {"Automatic", "On", "Off"}, [] { return ModeIndex(); },
        [](int index) { Svc().settings->SetString(ENABLED_KEY, MODES[std::clamp(index, 0, 2)]); });
    row.description = "Game details while browsing and quick settings while playing, on the "
                      "second screen. Automatic: on when the device has two screens";
    return {row};
}

} // namespace

bool SecondScreenReady() {
    return pal::GetSecondDisplay().Ready();
}

void SecondScreenPixelSize(int& width, int& height) {
    pal::GetSecondDisplay().PixelSize(width, height);
}

float SecondScreenScale() {
    return Get().scale;
}

bool ClaimSecondScreen(SecondScreenClient client) {
    pal::SecondDisplay& display = pal::GetSecondDisplay();
    if (!display.Present() && !display.Ready()) {
        return false;
    }
    Controller& c = Get();
    c.client = std::move(client);
    c.claimed = true;
    return true;
}

void ReleaseSecondScreen() {
    Controller& c = Get();
    c.client = {};
    c.claimed = false;
}

bool SecondScreenClaimed() {
    return Get().claimed;
}

void SetSecondScreenOverlay(SecondScreenClient client) {
    Controller& c = Get();
    c.overlay = std::move(client);
    c.overlaid = true;
}

void ClearSecondScreenOverlay() {
    Controller& c = Get();
    c.overlay = {};
    c.overlaid = false;
}

void DrawCompanionStage(ui::Context& ctx) {
    if (Controller& c = Get(); c.stage) {
        c.stage->Frame(ctx);
    }
}

ui::Stage& CompanionStage() {
    return *Get().stage;
}

const std::string& CompanionGame() {
    return Get().game;
}

DEN_REGISTER_STARTUP(Start);
DEN_REGISTER_FRAME(Frame);
DEN_REGISTER_SETTINGS(15, "Second screen", Rows);

} // namespace den::app
