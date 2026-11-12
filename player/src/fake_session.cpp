// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A stand-in player: it "runs" a moving test card instead of a core, so the player screen
// can be built and driven before the real session exists. Delete this file when the real
// player defines CreateSession.

#include <cmath>

#include "den/base/files.h"
#include "den/player/session.h"

namespace den::player {

namespace {

class FakeSession final : public Session {
public:
    Status Open(const OpenRequest& request) override {
        snapshot_ = Snapshot{};
        snapshot_.state = State::Running;
        snapshot_.core_name = "Test card";
        snapshot_.game_id = request.game_id;
        snapshot_.frame_width = 320;
        snapshot_.frame_height = 240;
        title_ = PathToUtf8(request.content.filename());
        return {};
    }
    void Close() override {
        snapshot_ = Snapshot{};
    }
    void Pause() override {
        if (snapshot_.state == State::Running) {
            snapshot_.state = State::Paused;
        }
    }
    void Resume() override {
        if (snapshot_.state == State::Paused) {
            snapshot_.state = State::Running;
        }
    }
    void SetPad(int port, const input::PadState& pad) override {
        if (port == 0) {
            pad_ = pad;
        }
    }
    void Advance(double seconds) override {
        if (snapshot_.state != State::Running) {
            return;
        }
        time_ += seconds * (snapshot_.fast_forward ? 4.0 : 1.0);
        snapshot_.frames = static_cast<std::uint64_t>(time_ * 60.0);
        snapshot_.measured_fps = 60.0;
    }
    void Draw(gfx::Canvas& canvas, const gfx::Rect& area) override {
        canvas.FillRect(area, gfx::Color::Rgb(0x000000));
        const gfx::Rect frame = gfx::FitRect(4, 3, area);
        canvas.FillRect(frame, gfx::Color::Rgb(0x101040));
        const float t = static_cast<float>(time_);
        const float size = frame.h * 0.12f;
        const float x = frame.x + (frame.w - size) * (0.5f + 0.45f * std::sin(t * 1.3f));
        const float y = frame.y + (frame.h - size) * (0.5f + 0.45f * std::cos(t * 1.7f));
        canvas.FillRect(gfx::Rect{x, y, size, size},
                        pad_.Down(input::PadButton::South) ? gfx::Color::Rgb(0xffffff)
                                                           : gfx::Color::Rgb(0x8090ff),
                        size * 0.2f);
        canvas.DrawText("No player yet: " + title_, frame.x + 16, frame.y + 16, 18,
                        gfx::Color::Rgb(0xffffff));
    }
    Status SaveState(int) override {
        return Status::Fail("The player is not built into this version yet.");
    }
    Status LoadState(int) override {
        return Status::Fail("The player is not built into this version yet.");
    }
    bool HasState(int) const override {
        return false;
    }
    void SetFastForward(bool on) override {
        snapshot_.fast_forward = on;
    }
    void SetRewind(bool on) override {
        snapshot_.rewinding = on;
    }
    void Reset() override {
        time_ = 0;
    }
    std::vector<CoreOption> Options() const override {
        return {};
    }
    void SetOption(const std::string&, const std::string&) override {}
    Snapshot GetSnapshot() const override {
        return snapshot_;
    }

private:
    Snapshot snapshot_;
    input::PadState pad_;
    std::string title_;
    double time_ = 0;
};

} // namespace

std::unique_ptr<Session> CreateSession(const Config&) {
    return std::make_unique<FakeSession>();
}

} // namespace den::player
