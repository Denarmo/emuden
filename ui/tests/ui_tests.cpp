// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "den/ui/page.h"
#include "den/ui/theme.h"

using namespace den;
using namespace den::ui;

namespace {

Node* AddBox(Node& parent, float w, float h, bool focusable = true) {
    Node* node = parent.Add(std::make_unique<Node>());
    node->width = w;
    node->height = h;
    node->focusable = focusable;
    return node;
}

// Text that wraps: one line per 100 units of width, 20 units each.
class Wrapping final : public Node {
public:
    float HeightForWidth(Context&, float w) override {
        return 20.0f * std::ceil(300.0f / std::max(1.0f, w));
    }
};

} // namespace

TEST_CASE("a column gives a wrapping child the height its width needs") {
    Context ctx;
    Node root;
    root.gap = 10;
    root.scrollable = true;
    Node* group = root.Add(MakeBox(Axis::Column, 0)); // a plain column grows with what wraps in it
    Node* text = group->Add(std::make_unique<Wrapping>());
    Node* below = AddBox(root, AUTO, 40);
    root.Measure(ctx);
    root.Arrange(ctx, gfx::Rect{0, 0, 150, 500});
    CHECK(text->rect.h == Catch::Approx(40));
    CHECK(group->rect.h == Catch::Approx(40));
    CHECK(below->rect.y == Catch::Approx(50));
    CHECK(root.content_extent == Catch::Approx(90));
    root.Measure(ctx);
    root.Arrange(ctx, gfx::Rect{0, 0, 60, 500});
    CHECK(text->rect.h == Catch::Approx(100));
    CHECK(below->rect.y == Catch::Approx(110));
}

TEST_CASE("layout distributes free space to growing children") {
    Context ctx;
    Node root;
    root.axis = Axis::Row;
    root.gap = 10;
    Node* fixed = AddBox(root, 100, 50);
    Node* flexible = root.Add(std::make_unique<Node>());
    flexible->grow = 1;
    root.Measure(ctx);
    root.Arrange(ctx, gfx::Rect{0, 0, 400, 200});
    CHECK(fixed->rect.w == Catch::Approx(100));
    CHECK(fixed->rect.h == Catch::Approx(50));
    CHECK(flexible->rect.x == Catch::Approx(110));
    CHECK(flexible->rect.w == Catch::Approx(290));
    CHECK(flexible->rect.h == Catch::Approx(200));
}

TEST_CASE("focus moves inside a shelf before leaving it") {
    Context ctx;
    Node root;
    root.gap = 20;
    Node* shelf_a = root.Add(MakeBox(Axis::Row, 10));
    Node* shelf_b = root.Add(MakeBox(Axis::Row, 10));
    Node* a1 = AddBox(*shelf_a, 100, 100);
    Node* a2 = AddBox(*shelf_a, 100, 100);
    Node* b1 = AddBox(*shelf_b, 100, 100);
    Node* b2 = AddBox(*shelf_b, 100, 100);
    root.Measure(ctx);
    root.Arrange(ctx, gfx::Rect{0, 0, 800, 600});

    CHECK(FindFocusTarget(root, a1, input::Nav::Right) == a2);
    CHECK(FindFocusTarget(root, a2, input::Nav::Right) == nullptr);
    CHECK(FindFocusTarget(root, a2, input::Nav::Down) == b2);
    CHECK(FindFocusTarget(root, b1, input::Nav::Up) == a1);
    CHECK(FindFocusTarget(root, a1, input::Nav::Left) == nullptr);
    CHECK(FindFocusTarget(root, nullptr, input::Nav::Down) == a1);
}

TEST_CASE("a scrolling node clamps its offset to the content") {
    Context ctx;
    Node root;
    root.axis = Axis::Row;
    root.scrollable = true;
    for (int i = 0; i < 10; ++i) {
        AddBox(root, 100, 100);
    }
    root.scroller.Jump(5000);
    root.Measure(ctx);
    root.Arrange(ctx, gfx::Rect{0, 0, 300, 100});
    CHECK(root.scroll == Catch::Approx(700));
    CHECK(root.Children().back()->rect.Right() == Catch::Approx(300));
}

TEST_CASE("themes: light is the default and blue on white") {
    CHECK(ThemeByName("nonsense").name == "Light");
    CHECK(ThemeByName("dark").name == "Dark");
    CHECK(LightTheme().fg.b == 255);
    CHECK(LightTheme().fg.r == 0);
    CHECK(LightTheme().page.r == 255);
}

TEST_CASE("the text entry types with navigation and finishes with Menu") {
    using input::Event;
    using input::Nav;
    TextRequest request;
    request.max_length = 4;
    std::string result;
    TextEntry entry(request, [&](const std::string& text) { result = text; });
    const auto nav = [&](Nav which) { return entry.HandleEvent(Event::MakeNav(which, input::PadFamily::Xbox), 1.0f); };

    // Empty: Done is refused, and the first letter is a capital.
    CHECK(nav(Nav::Menu) == TextEntry::Outcome::Open);
    CHECK(entry.SelectedKey() == "Q");
    nav(Nav::Confirm);
    CHECK(entry.SelectedKey() == "q");
    nav(Nav::Right);
    nav(Nav::Confirm);
    nav(Nav::Extra);
    nav(Nav::Confirm);
    nav(Nav::Confirm); // over the limit of four characters
    CHECK(entry.Text() == "Qw w");
    nav(Nav::Option);
    nav(Nav::Option);
    CHECK(entry.Text() == "Qw");

    // The bottom row has wide keys; left and right step over whole keys and wrap.
    for (int i = 0; i < 4; ++i) {
        nav(Nav::Down);
    }
    CHECK(entry.SelectedKey() == "Shift");
    nav(Nav::Left);
    CHECK(entry.SelectedKey() == "Done");
    CHECK(nav(Nav::Confirm) == TextEntry::Outcome::Done);
    entry.Finish();
    CHECK(result == "Qw");
}

TEST_CASE("a real keyboard types into the text entry without acting twice") {
    using input::Event;
    TextEntry entry(TextRequest{}, {});
    const auto key = [&](int code, const char* text) {
        Event down;
        down.type = Event::Type::Key;
        down.key = code;
        down.down = true;
        entry.HandleEvent(down, 1.0f);
        // The same key as navigation (X is Option, Backspace is Back, Enter is Confirm).
        const input::Nav as_nav = code == 'x' ? input::Nav::Option : code == 8 ? input::Nav::Back : input::Nav::Confirm;
        const TextEntry::Outcome outcome = entry.HandleEvent(Event::MakeNav(as_nav), 1.0f);
        if (text) {
            Event typed;
            typed.type = Event::Type::Text;
            typed.text = text;
            entry.HandleEvent(typed, 1.0f);
        }
        return outcome;
    };
    CHECK(key('x', "x") == TextEntry::Outcome::Open);
    CHECK(key('x', "x") == TextEntry::Outcome::Open);
    CHECK(entry.Text() == "xx");
    CHECK(key(8, nullptr) == TextEntry::Outcome::Open);
    CHECK(entry.Text() == "x");
    CHECK(key(13, nullptr) == TextEntry::Outcome::Done);
    // Escape arrives as navigation only.
    CHECK(entry.HandleEvent(Event::MakeNav(input::Nav::Back), 1.0f) == TextEntry::Outcome::Cancelled);
}

namespace {

bool g_second_level = false;

// The rail and a column at the first level; at the second, a padded column without the rail.
class LevelsPage final : public Page {
public:
    void Build() override {
        Node& root = Root();
        if (g_second_level) {
            root.axis = Axis::Column;
            root.gap = 16;
            root.SetPadding(32, 28);
            AddBox(root, AUTO, 48);
            return;
        }
        root.axis = Axis::Row;
        Node* rail = AddBox(root, 232, AUTO, false);
        rail->id = "rail";
        rail->chrome = true;
        Node* content = root.Add(MakeBox(Axis::Column, 16));
        content->id = "content";
        content->grow = 1;
        AddBox(*content, AUTO, 48);
    }
};

class EmptyPage final : public Page {
public:
    void Build() override {}
};

} // namespace

TEST_CASE("a page rebuilt into its first level lays out as when it was first built") {
    Stage stage;
    Context ctx;
    stage.Register("levels", [] { return std::make_unique<LevelsPage>(); });
    stage.Register("other", [] { return std::make_unique<EmptyPage>(); });
    const auto layout = [&] {
        Node& root = stage.Top()->Root();
        root.Measure(ctx);
        root.Arrange(ctx, gfx::Rect{0, 0, 1280, 672});
    };
    // A rebuild of the page under "other", applied when that one goes.
    const auto rebuild = [&] {
        stage.Top()->Rebuild();
        stage.Push("other");
        stage.Pop();
        stage.HandleEvent(input::Event::MakeNav(input::Nav::None));
        layout();
    };
    g_second_level = false;
    stage.Push("levels");
    stage.HandleEvent(input::Event::MakeNav(input::Nav::None));
    layout();
    const gfx::Rect rail = stage.Top()->Root().FindById("rail")->rect;
    const gfx::Rect content = stage.Top()->Root().FindById("content")->rect;
    CHECK(rail.x == 0);
    CHECK(rail.y == 0);

    g_second_level = true;
    rebuild();
    CHECK(stage.Top()->Root().FindById("rail") == nullptr);
    g_second_level = false;
    rebuild();
    const Node& root = stage.Top()->Root();
    CHECK(root.pad_left == 0);
    CHECK(root.pad_top == 0);
    CHECK(root.gap == 0);
    const gfx::Rect rail_after = stage.Top()->Root().FindById("rail")->rect;
    const gfx::Rect content_after = stage.Top()->Root().FindById("content")->rect;
    CHECK(rail_after.x == rail.x);
    CHECK(rail_after.y == rail.y);
    CHECK(rail_after.w == rail.w);
    CHECK(rail_after.h == rail.h);
    CHECK(content_after.x == content.x);
    CHECK(content_after.y == content.y);
    CHECK(content_after.w == content.w);
    CHECK(content_after.h == content.h);
}
