// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
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
