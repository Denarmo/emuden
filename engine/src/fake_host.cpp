// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A stand-in engine host: no engine is ever available. Delete this file when the real host
// defines CreateHost.

#include "den/engine/host.h"

namespace den::engine {

namespace {

class FakeHost final : public Host {
public:
    bool Available(const std::string&) const override {
        return false;
    }
    std::string UnavailableReason(const std::string&) const override {
        return "The engine host is not built into this version yet.";
    }
    Status Start(const LaunchRequest& request) override {
        Event event;
        event.type = Event::Type::Failed;
        event.game_id = request.game_id;
        event.message = UnavailableReason(request.system);
        events_.push_back(event);
        return Status::Fail(event.message);
    }
    bool Running() const override {
        return false;
    }
    void Stop() override {}
    std::vector<Event> PollEvents() override {
        std::vector<Event> out;
        out.swap(events_);
        return out;
    }

private:
    std::vector<Event> events_;
};

} // namespace

std::unique_ptr<Host> CreateHost(const Config&) {
    return std::make_unique<FakeHost>();
}

} // namespace den::engine
