// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Ties the engine host to the app: the window is hidden while an engine runs, its play time
// is booked in the library, and a page that takes no pad input covers the app meanwhile
// (the pad belongs to the game).

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

constexpr const char* RUNNING_ROUTE = "engine.running";

class EngineRunningScreen final : public ui::Page {
public:
    void Build() override {
        const engine::LaunchRequest* request = Svc().engine->Current();
        ui::Node* content = BuildPlainFrame(*this, request ? request->title : "Playing");
        content->Add(std::make_unique<ui::Label>("The game is running in its own window."));
        auto stop = std::make_unique<ui::Button>("Stop the game", [] { Svc().engine->Stop(); });
        stop->id = "engine.stop";
        SetFocus(content->Add(std::move(stop)));
    }
    bool OnEvent(const input::Event& event) override {
        return event.type == input::Event::Type::Nav && event.family != input::PadFamily::Keyboard;
    }
    bool OnNav(input::Nav nav) override {
        return nav == input::Nav::Back;
    }
    std::vector<ui::Hint> Hints() const override {
        return {};
    }
};

void Startup() {
    engine::Hooks hooks;
    hooks.window = Svc().window;
    hooks.record_play = [](const std::string& id, std::int64_t started, std::int64_t seconds) {
        if (Svc().library) {
            Svc().library->RecordPlay(id, started, seconds);
        }
    };
    Svc().engine->Attach(std::move(hooks));
}

void Frame() {
    Services& svc = Svc();
    if (!svc.engine || !svc.stage) {
        return;
    }
    svc.engine->Pump();
    const ui::Page* top = svc.stage->Top();
    const bool covered = top && top->Route() == RUNNING_ROUTE;
    if (svc.engine->Running() && !covered) {
        svc.stage->Push(RUNNING_ROUTE);
    } else if (!svc.engine->Running() && covered) {
        svc.stage->Pop();
    }
}

} // namespace

DEN_REGISTER_SCREEN(RUNNING_ROUTE, EngineRunningScreen);
DEN_REGISTER_STARTUP(Startup);
DEN_REGISTER_FRAME(Frame);

} // namespace den::app
