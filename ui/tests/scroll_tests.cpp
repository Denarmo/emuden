// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "den/ui/page.h"
#include "den/ui/scroller.h"
#include "den/ui/widgets.h"

using namespace den;
using namespace den::ui;
using Catch::Approx;

namespace {

/// A finger that moves the content at `speed` units/s for `seconds`, sampled at 120 Hz.
/// Returns the time it lifted.
double Drag(Scroller& s, float speed, double seconds, double start = 0) {
    s.BeginDrag(start);
    const int steps = static_cast<int>(std::lround(seconds * 120));
    for (int i = 1; i <= steps; ++i) {
        s.DragBy(speed / 120.0f, start + i / 120.0);
    }
    return start + seconds;
}

void Run(Scroller& s, int hz, double seconds) {
    const int frames = static_cast<int>(std::lround(seconds * hz));
    for (int i = 0; i < frames; ++i) {
        s.Tick(1.0f / static_cast<float>(hz));
    }
}

Scroller Flung(float start, float speed, float max = 5000, const std::vector<float>& snaps = {}) {
    Scroller s;
    s.SetBounds(max, 600);
    s.Jump(start);
    s.EndDrag(Drag(s, speed, 0.1), snaps);
    return s;
}

} // namespace

TEST_CASE("a fling starts at the finger's speed and friction stops it") {
    Scroller s = Flung(1000, 1200);
    CHECK(s.Position() == Approx(1120).margin(0.5));
    CHECK(s.Velocity() == Approx(1200).margin(15));
    CHECK(s.Kinetic());
    const float rest = s.Destination();
    CHECK(rest == Approx(1120 + 1200 / s.tuning.friction).margin(6));

    // Half the speed is gone after ln 2 / friction seconds, whatever the frame rate.
    Run(s, 120, std::log(2.0) / s.tuning.friction);
    CHECK(s.Velocity() == Approx(600).margin(12));
    Run(s, 60, 4);
    CHECK_FALSE(s.Moving());
    CHECK(s.Position() == Approx(rest).margin(2.5));
}

TEST_CASE("the motion is the same at 60, 90 and 120 Hz") {
    for (const float start : {1000.0f, 4900.0f}) { // free, and into the far end and back
        for (const double seconds : {0.1, 0.3, 1.0}) {
            Scroller a = Flung(start, 2400), b = Flung(start, 2400), c = Flung(start, 2400);
            Run(a, 60, seconds);
            Run(b, 90, seconds);
            Run(c, 120, seconds);
            CHECK(a.Position() == Approx(b.Position()).margin(0.05));
            CHECK(a.Position() == Approx(c.Position()).margin(0.05));
        }
    }
    Scroller a, b;
    a.SetBounds(5000, 600);
    b.SetBounds(5000, 600);
    a.ScrollTo(800);
    b.ScrollTo(800);
    Run(a, 60, 0.1);
    Run(b, 120, 0.1);
    CHECK(a.Position() == Approx(b.Position()).margin(0.05));
}

TEST_CASE("a fling is capped, and a finger that rests before lifting does not fling") {
    Scroller fast = Flung(100, 40000, 100000);
    CHECK(fast.Velocity() == Approx(fast.tuning.max_velocity).margin(1));

    Scroller s;
    s.SetBounds(5000, 600);
    const double lifted = Drag(s, 1500, 0.1);
    s.EndDrag(lifted + 0.2);
    CHECK_FALSE(s.Moving());
    CHECK(s.Position() == Approx(150).margin(0.5));

    // Slower than a fling: the content stays where the finger left it.
    Scroller slow = Flung(100, 40);
    CHECK_FALSE(slow.Moving());
}

TEST_CASE("dragging past an end stretches a rubber band that springs back") {
    Scroller s;
    s.SetBounds(1000, 600);
    s.BeginDrag(0);
    s.DragBy(-300, 0.05);
    CHECK(s.Position() < 0);
    CHECK(s.Position() > -150); // less than half of what the finger moved
    const float at_300 = s.Position();
    s.DragBy(-3000, 0.1);
    CHECK(s.Position() < at_300);
    CHECK(s.Position() > -600); // never further than the window
    // Dragging back retraces the band: no jump when the finger returns.
    s.DragBy(3000, 0.15);
    CHECK(s.Position() == Approx(at_300).margin(0.5));

    s.EndDrag(0.4);
    CHECK(s.Moving());
    float last = s.Position();
    for (int i = 0; i < 120; ++i) {
        s.Tick(1 / 60.0f);
        CHECK(s.Position() >= last - 0.01f); // straight back, without swinging past the end
        last = s.Position();
    }
    CHECK(s.Position() == Approx(0));
    CHECK_FALSE(s.Moving());
}

TEST_CASE("a fling that reaches an end overshoots a little and returns") {
    Scroller s = Flung(200, -3000, 1000);
    float lowest = 0;
    for (int i = 0; i < 240; ++i) {
        s.Tick(1 / 120.0f);
        lowest = std::min(lowest, s.Position());
    }
    CHECK(lowest < -20);
    CHECK(lowest > -120);
    CHECK(s.Position() == Approx(0));
    CHECK_FALSE(s.Moving());
}

TEST_CASE("a snapping fling rests on a snap position and always moves on") {
    const std::vector<float> snaps{0, 200, 400, 600, 800, 1000};
    Scroller s = Flung(0, 900, 1000, snaps);
    Run(s, 60, 3);
    CHECK_FALSE(s.Moving());
    const float rest = s.Position();
    CHECK(std::fmod(rest, 200.0f) == Approx(0).margin(0.001));
    CHECK(rest >= 400);

    // A weak flick still goes one position on, not back.
    Scroller weak = Flung(400, 100, 1000, snaps);
    Run(weak, 60, 3);
    CHECK(weak.Position() == Approx(600));

    // Let go without speed: the nearest position.
    Scroller let_go;
    let_go.SetBounds(1000, 600);
    let_go.Jump(400);
    let_go.EndDrag(Drag(let_go, 300, 0.2) + 0.3, snaps);
    Run(let_go, 60, 2);
    CHECK(let_go.Position() == Approx(400));
}

TEST_CASE("wheel and focus moves are animated and add up") {
    Scroller s;
    s.SetBounds(2000, 600);
    s.ScrollBy(90);
    s.ScrollBy(90);
    CHECK(s.Destination() == Approx(180));
    s.Tick(1 / 60.0f);
    CHECK(s.Position() > 0);
    CHECK(s.Position() < 60);
    CHECK_FALSE(s.Kinetic()); // a tap during it still counts as a tap
    Run(s, 60, 1);
    CHECK(s.Position() == Approx(180));
    s.ScrollBy(-9000);
    CHECK(s.Destination() == Approx(0));
}

namespace {

int g_activated = 0;

// A vertical page of rows; the second row is a horizontal shelf of tiles.
class ShelfPage final : public Page {
public:
    void Build() override {
        Node* list = Root().Add(MakeList(10));
        list->id = "list";
        for (int row = 0; row < 12; ++row) {
            if (row == 1) {
                Node* shelf = list->Add(MakeShelf(10));
                shelf->id = "shelf";
                shelf->height = 100;
                for (int i = 0; i < 20; ++i) {
                    Tile(shelf, 150);
                }
            } else {
                Tile(list, AUTO);
            }
        }
    }

private:
    static void Tile(Node* parent, float width) {
        Node* tile = parent->Add(std::make_unique<Node>());
        tile->width = width;
        tile->height = 100;
        tile->focusable = true;
        tile->on_activate = [] { ++g_activated; };
    }
};

struct Fixture {
    Stage stage;
    Context ctx;
    double now = 0;
    Node* list = nullptr;
    Node* shelf = nullptr;

    Fixture() {
        g_activated = 0;
        stage.Register("shelves", [] { return std::make_unique<ShelfPage>(); });
        stage.Push("shelves");
        Frame(0);
        list = stage.Top()->Root().FindById("list");
        shelf = stage.Top()->Root().FindById("shelf");
    }
    void Frame(float dt) {
        input::Event nothing;
        nothing.type = input::Event::Type::Resized;
        stage.HandleEvent(nothing, now); // applies the pending navigation
        Node& root = stage.Top()->Root();
        ctx.dt = dt;
        now += dt;
        root.Tick(ctx);
        root.Measure(ctx);
        root.Arrange(ctx, gfx::Rect{0, 0, 800, 600});
    }
    void Pointer(input::Event::Type type, float x, float y) {
        input::Event event;
        event.type = type;
        event.x = x;
        event.y = y;
        stage.HandleEvent(event, now);
    }
    /// A finger from one point to another in `frames` frames at 60 Hz, then lifted.
    void Swipe(float x0, float y0, float x1, float y1, int frames = 6) {
        Pointer(input::Event::Type::PointerDown, x0, y0);
        for (int i = 1; i <= frames; ++i) {
            Frame(1 / 60.0f);
            const float t = static_cast<float>(i) / static_cast<float>(frames);
            Pointer(input::Event::Type::PointerMove, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
        }
        Pointer(input::Event::Type::PointerUp, x1, y1);
    }
    void Settle() {
        for (int i = 0; i < 600; ++i) {
            Frame(1 / 60.0f);
        }
    }
};

} // namespace

TEST_CASE("a vertical swipe flings the page, a horizontal one the shelf under the finger") {
    Fixture f;
    f.Swipe(400, 400, 400, 250);
    CHECK(f.list->scroller.Moving());
    CHECK(f.list->scroller.Position() > 100); // followed the finger past the drag threshold
    CHECK(f.shelf->scroller.Position() == Approx(0));
    CHECK(g_activated == 0);
    const float lifted = f.list->scroller.Position();
    f.Settle();
    CHECK(f.list->scroller.Position() > lifted + 100); // and carried on by its weight
    CHECK_FALSE(f.list->scroller.Moving());

    Fixture g;
    g.Swipe(600, 160, 400, 170); // on the shelf, slightly off the horizontal
    CHECK(g.shelf->scroller.Moving());
    CHECK(g.list->scroller.Position() == Approx(0)); // locked to one axis
    g.Settle();
    // The shelf rests with a tile at its start (tiles are 150 wide, 10 apart).
    CHECK(g.shelf->scroller.Position() >= 160);
    CHECK(std::fmod(g.shelf->scroller.Position(), 160.0f) == Approx(0).margin(0.01));
}

TEST_CASE("a tap during a fling stops it without activating, the next tap activates") {
    Fixture f;
    f.Swipe(400, 400, 400, 200);
    f.Frame(1 / 60.0f);
    f.Frame(1 / 60.0f);
    REQUIRE(f.list->scroller.Kinetic());
    f.Pointer(input::Event::Type::PointerDown, 400, 300);
    const float stopped = f.list->scroller.Position();
    f.Frame(1 / 60.0f);
    f.Pointer(input::Event::Type::PointerUp, 400, 300);
    f.Settle();
    CHECK(g_activated == 0);
    CHECK(f.list->scroller.Position() == Approx(stopped));

    f.Pointer(input::Event::Type::PointerDown, 400, 300);
    f.Pointer(input::Event::Type::PointerUp, 400, 300);
    CHECK(g_activated == 1);
}

TEST_CASE("the wheel and focus scroll through the same motion") {
    Fixture f;
    input::Event wheel;
    wheel.type = input::Event::Type::Wheel;
    wheel.x = 400;
    wheel.y = 300;
    wheel.dy = -2;
    f.stage.HandleEvent(wheel, f.now);
    f.Frame(1 / 60.0f);
    CHECK(f.list->scroller.Position() > 0);
    CHECK(f.list->scroller.Position() < 180); // on its way, not there in one frame
    f.Settle();
    CHECK(f.list->scroller.Position() == Approx(180));

    // Focus walking down the page pulls the list after it, animated as well.
    for (int i = 0; i < 9; ++i) {
        f.stage.HandleEvent(input::Event::MakeNav(input::Nav::Down), f.now);
        f.Frame(1 / 60.0f);
    }
    CHECK(f.list->scroller.Moving());
    f.Settle();
    const Node* focus = f.stage.Top()->Focus();
    REQUIRE(focus);
    CHECK(focus->rect.y >= f.list->rect.y);
    CHECK(focus->rect.Bottom() <= f.list->rect.Bottom());
}
