// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>

#include "automation.h"
#include "den/app/app.h"
#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"

namespace den::app {

namespace {

std::string OneLine(std::string text) {
    std::erase(text, '\n');
    return text;
}

// A finger moved across the screen over several frames, so the UI sees real timing.
struct Gesture {
    bool active = false;
    std::uint64_t request = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double start = 0, move = 0, hold = 0; // seconds
};
Gesture g_gesture;

double Now() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

void Pointer(input::Event::Type type, float x, float y) {
    Services& svc = Svc();
    input::Event event;
    event.type = type;
    event.down = type != input::Event::Type::PointerUp;
    event.x = x * svc.canvas->Scale();
    event.y = y * svc.canvas->Scale();
    svc.input->Inject(event);
}

/// Moves the gesture's finger to where it is now; true once it has been lifted.
bool StepGesture() {
    Gesture& g = g_gesture;
    const double elapsed = Now() - g.start;
    const float t = g.move > 0 ? static_cast<float>(std::min(1.0, elapsed / g.move)) : 1.0f;
    const float x = g.x0 + (g.x1 - g.x0) * t, y = g.y0 + (g.y1 - g.y0) * t;
    if (elapsed <= g.move || g.hold <= 0) {
        Pointer(input::Event::Type::PointerMove, x, y);
    }
    if (elapsed < g.move + g.hold) {
        return false;
    }
    Pointer(input::Event::Type::PointerUp, x, y);
    g.active = false;
    return true;
}

} // namespace

void Automation::Start() {
    const std::string port = pal::GetEnv("EMUDEN_AUTOMATION");
    if (port.empty()) {
        return;
    }
    auto server = pal::StartLineServer(std::atoi(port.c_str()));
    if (!server) {
        log::Error("automation", "{}", server.error());
        return;
    }
    server_ = server.take();
    log::Info("automation", "listening on 127.0.0.1:{}", port);
}

void Automation::Pump() {
    if (!server_) {
        return;
    }
    Services& svc = Svc();
    if (g_gesture.active && StepGesture()) {
        server_->Reply(g_gesture.request, "ok");
    }
    while (auto request = server_->Next()) {
        std::istringstream in(request->line);
        std::string command;
        in >> command;
        std::string reply = "ok";
        if (command == "state") {
            auto state = ParseJson(svc.stage->StateJson());
            if (state) {
                state->members.emplace_back("family", JsonString(input::FamilyName(svc.input->ActiveFamily())));
                state->members.emplace_back("fps", JsonNumber(static_cast<double>(static_cast<int>(svc.fps + 0.5))));
                state->members.emplace_back("theme", JsonString(svc.theme->name));
                reply = OneLine(WriteJson(*state));
            }
        } else if (command == "key") {
            std::string name;
            in >> name;
            const input::Nav nav = input::NavFromName(name);
            if (nav == input::Nav::None) {
                reply = "error unknown key '" + name + "'";
            } else {
                svc.input->Inject(input::Event::MakeNav(nav));
            }
        } else if (command == "go") {
            std::string route, pair;
            in >> route;
            ui::Params params;
            while (in >> pair) {
                const auto eq = pair.find('=');
                if (eq != std::string::npos) {
                    params[pair.substr(0, eq)] = pair.substr(eq + 1);
                }
            }
            if (svc.stage->HasRoute(route)) {
                svc.stage->Push(route, std::move(params));
            } else {
                reply = "error no route '" + route + "'";
            }
        } else if (command == "tap") {
            // Logical units, as the UI lays out (the window is 720 units high).
            float x = 0, y = 0;
            in >> x >> y;
            input::Event event;
            event.x = x * svc.canvas->Scale();
            event.y = y * svc.canvas->Scale();
            event.type = input::Event::Type::PointerDown;
            svc.input->Inject(event);
            event.type = input::Event::Type::PointerUp;
            svc.input->Inject(event);
        } else if (command == "drag" || command == "fling") {
            // "drag x1 y1 x2 y2 [ms] [hold ms]": a finger goes down, moves in a straight line
            // over `ms`, rests for `hold ms` and lifts. "fling" is the same, 120 ms by default.
            // Answered when the finger lifts.
            Gesture g;
            double ms = command == "fling" ? 120 : 400, hold = 0;
            in >> g.x0 >> g.y0 >> g.x1 >> g.y1;
            if (in.fail()) {
                reply = "error " + command + " needs x1 y1 x2 y2 [ms] [hold ms]";
            } else if (g_gesture.active) {
                reply = "error another gesture is running";
            } else {
                in >> ms >> hold;
                g.active = true;
                g.request = request->id;
                g.start = Now();
                g.move = std::max(0.0, ms) / 1000.0;
                g.hold = std::max(0.0, hold) / 1000.0;
                g_gesture = g;
                Pointer(input::Event::Type::PointerDown, g.x0, g.y0);
                continue;
            }
        } else if (command == "touch") {
            // "touch down|move|up x y": one pointer event, for gestures built by hand.
            std::string what;
            float x = 0, y = 0;
            in >> what >> x >> y;
            if (what == "down") {
                Pointer(input::Event::Type::PointerDown, x, y);
            } else if (what == "move") {
                Pointer(input::Event::Type::PointerMove, x, y);
            } else if (what == "up") {
                Pointer(input::Event::Type::PointerUp, x, y);
            } else {
                reply = "error touch needs down, move or up";
            }
        } else if (command == "wheel") {
            // "wheel x y lines": the mouse wheel over a point; positive lines scroll up.
            input::Event event;
            float x = 0, y = 0;
            in >> x >> y >> event.dy;
            event.type = input::Event::Type::Wheel;
            event.x = x * svc.canvas->Scale();
            event.y = y * svc.canvas->Scale();
            svc.input->Inject(event);
        } else if (command == "screenshot") {
            std::string path;
            std::getline(in, path);
            path = Trim(path);
            if (path.empty()) {
                reply = "error screenshot needs a file path";
            } else {
                if (shot_id_) {
                    server_->Reply(shot_id_, "error replaced by a newer screenshot");
                }
                shot_id_ = request->id;
                shot_path_ = path;
                continue; // answered after the frame is drawn
            }
        } else if (command == "pad") {
            // "pad start 200": holds game buttons on the first port for some milliseconds.
            static const std::pair<const char*, input::PadButton> NAMES[] = {
                {"south", input::PadButton::South},   {"east", input::PadButton::East},
                {"west", input::PadButton::West},     {"north", input::PadButton::North},
                {"up", input::PadButton::DpadUp},     {"down", input::PadButton::DpadDown},
                {"left", input::PadButton::DpadLeft}, {"right", input::PadButton::DpadRight},
                {"l", input::PadButton::L1},          {"r", input::PadButton::R1},
                {"start", input::PadButton::Start},   {"select", input::PadButton::Select},
            };
            std::uint32_t buttons = 0;
            double ms = 150;
            std::string word;
            while (in >> word) {
                bool known = false;
                for (const auto& [name, button] : NAMES) {
                    if (word == name) {
                        buttons |= 1u << static_cast<std::uint32_t>(button);
                        known = true;
                    }
                }
                if (!known) {
                    ms = std::atof(word.c_str());
                }
            }
            svc.input->HoldButtons(buttons, ms / 1000.0);
        } else if (command == "quit") {
            svc.quit = true;
        } else {
            reply = "error unknown command '" + command + "'";
        }
        server_->Reply(request->id, reply);
    }
}

void Automation::AfterDraw(int width, int height) {
    if (!server_ || !shot_id_) {
        return;
    }
    const std::vector<std::uint8_t> pixels = Svc().device->ReadPixels(width, height);
    const std::filesystem::path file = PathFromUtf8(shot_path_);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const Status saved = gfx::SavePng(file, width, height, pixels.data());
    server_->Reply(shot_id_, saved ? "ok " + std::to_string(width) + "x" + std::to_string(height)
                                   : "error " + saved.error);
    shot_id_ = 0;
}

} // namespace den::app
