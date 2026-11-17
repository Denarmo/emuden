// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Taps and presses on a page of settings rows: what the finger lands on, and what happens.

#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "den/ui/page.h"
#include "den/ui/widgets.h"

using namespace den;
using namespace den::ui;
using Catch::Approx;

namespace {

constexpr float ROW_H = 56, GAP = 8, HEADER_H = 60;

struct Model {
    std::vector<bool> toggles = std::vector<bool>(20, false);
    int choice = 0;
    int actions = 0;
    std::vector<std::string> events; // focus and activate, in order
    int builds = 0;
};
Model g_model;

// A header above a list of rows: an action, a choice, then toggles.
class RowsPage final : public Page {
public:
    void Build() override {
        ++g_model.builds;
        Root().axis = Axis::Column;
        Node* header = Root().Add(MakeBox(Axis::Row));
        header->height = HEADER_H;
        header->id = "header";
        Node* list = Root().Add(MakeList(GAP));
        list->id = "list";
        list->grow = 1;
        RowSpec action;
        action.id = "row.action";
        action.action = [] { ++g_model.actions; };
        RowSpec choice;
        choice.kind = RowSpec::Kind::Choice;
        choice.id = "row.choice";
        choice.options = {"A", "B", "C"};
        choice.get_index = [] { return g_model.choice; };
        choice.set_index = [](int i) { g_model.choice = i; };
        for (RowSpec spec : {action, choice}) {
            list->Emplace<Row>(std::move(spec))->height = ROW_H;
        }
        for (int i = 2; i < 20; ++i) {
            RowSpec toggle;
            toggle.kind = RowSpec::Kind::Toggle;
            toggle.id = "row." + std::to_string(i);
            toggle.get_bool = [i] { return g_model.toggles[i]; };
            toggle.set_bool = [i](bool on) { g_model.toggles[i] = on; };
            Row* row = list->Emplace<Row>(std::move(toggle));
            row->height = ROW_H;
            row->on_focus = [i] { g_model.events.push_back("focus " + std::to_string(i)); };
        }
    }
};

class OtherPage final : public Page {
public:
    void Build() override {}
};

struct Fixture {
    Stage stage;
    Context ctx;
    double now = 0;

    Fixture() {
        g_model = Model();
        stage.Register("rows", [] { return std::make_unique<RowsPage>(); });
        stage.Register("other", [] { return std::make_unique<OtherPage>(); });
        stage.Push("rows");
        Frame();
    }
    Page& Top() {
        return *stage.Top();
    }
    Node* List() {
        return Top().Root().FindById("list");
    }
    void Frame(float dt = 1 / 60.0f) {
        input::Event nothing;
        nothing.type = input::Event::Type::Resized;
        stage.HandleEvent(nothing, now); // applies the pending navigation
        Node& root = Top().Root();
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
    void Tap(float x, float y) {
        Pointer(input::Event::Type::PointerDown, x, y);
        Frame();
        Pointer(input::Event::Type::PointerUp, x, y);
    }
    /// The page is built again, as when its data changes while a finger is on it.
    void RebuildNow() {
        Top().Rebuild();
        stage.Push("other");
        Frame();
        stage.Pop();
        Frame();
    }
    /// The middle of row `i` on screen, with the list where it is now.
    float RowY(int i) {
        return HEADER_H + static_cast<float>(i) * (ROW_H + GAP) + ROW_H / 2 - List()->scroller.Position();
    }
};

} // namespace

TEST_CASE("a tap flips a toggle, steps a choice and runs an action") {
    Fixture f;
    f.Tap(400, f.RowY(0));
    CHECK(g_model.actions == 1);
    f.Tap(400, f.RowY(1));
    CHECK(g_model.choice == 1);
    f.Tap(400, f.RowY(2));
    CHECK(g_model.toggles[2]);
    REQUIRE(f.Top().Focus());
    CHECK(f.Top().Focus()->id == "row.2");
    f.Tap(400, f.RowY(2));
    CHECK_FALSE(g_model.toggles[2]);
}

TEST_CASE("a tap reaches the row under the finger in a scrolled list, not one scrolled away") {
    Fixture f;
    f.List()->scroller.Jump(5 * (ROW_H + GAP));
    f.Frame();
    REQUIRE(f.List()->scroller.Position() == Approx(5 * (ROW_H + GAP)));
    f.Tap(400, f.RowY(7));
    CHECK(g_model.toggles[7]);
    CHECK_FALSE(g_model.toggles[2]);
    // The header covers where rows 4 and earlier would be: they are out of the list's window.
    f.Tap(400, HEADER_H / 2);
    CHECK(g_model.actions == 0);
    CHECK_FALSE(g_model.toggles[3]);
    CHECK(f.Top().Focus()->id == "row.7");
}

TEST_CASE("a tap focuses the row before it acts on it") {
    Fixture f;
    f.Tap(400, f.RowY(4));
    REQUIRE(g_model.events.size() == 1);
    CHECK(g_model.events[0] == "focus 4");
    CHECK(g_model.toggles[4]);
}

TEST_CASE("a press and a drag hold on through a rebuild of the page") {
    Fixture f;
    // A tap whose page is rebuilt between the press and the lift (live data, a width check).
    const int builds = g_model.builds;
    f.Pointer(input::Event::Type::PointerDown, 400, f.RowY(3));
    f.RebuildNow();
    REQUIRE(g_model.builds > builds);
    f.Pointer(input::Event::Type::PointerUp, 400, f.RowY(3));
    CHECK(g_model.toggles[3]);

    // A drag goes on scrolling the rebuilt list, and does not tap.
    f.Pointer(input::Event::Type::PointerDown, 400, 400);
    for (int i = 1; i <= 4; ++i) {
        f.Frame();
        f.Pointer(input::Event::Type::PointerMove, 400, 400 - 20.0f * static_cast<float>(i));
    }
    const float before = f.List()->scroller.Position();
    REQUIRE(before > 0);
    f.RebuildNow();
    for (int i = 5; i <= 8; ++i) {
        f.Frame();
        f.Pointer(input::Event::Type::PointerMove, 400, 400 - 20.0f * static_cast<float>(i));
    }
    CHECK(f.List()->scroller.Position() == Approx(before + 80).margin(1));
    f.Pointer(input::Event::Type::PointerUp, 400, 240);
    int on = 0;
    for (const bool toggle : g_model.toggles) {
        on += toggle ? 1 : 0;
    }
    CHECK(on == 1);
}
