// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <format>
#include <iterator>

#include "den/app/second_screen.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/base/log.h"
#include "den/system/settings.h"
#include "dual_screen.h"

namespace den::app {

namespace {

using player::ScreenLayout;

const system::Key<std::string> MODE_KEY{
    "player.dual_screen.mode", "auto",
    {.label = "Two-screen consoles",
     .section = "Player",
     .options = {"auto", "top-on-main", "top-on-second", "single"}}};
constexpr const char* LAYOUT_KEY = "player.dual_screen.layout";
constexpr const char* SWAP_KEY = "player.dual_screen.swap";

constexpr const char* MODES[] = {"auto", "top-on-main", "top-on-second", "single"};
constexpr const char* LAYOUTS[] = {"stacked", "side-by-side", "large-small", "one"};
constexpr int MODE_TOP_ON_SECOND = 2, MODE_SINGLE = 3;
constexpr double SHORTEST_PRESS = 0.06; // seconds: a tap lasts a few frames of the game
constexpr float BUTTON_W = 84, BUTTON_H = 40, BUTTON_MARGIN = 12;

// The instance whose game is on screen: it owns the claim on the second display.
DualScreens* g_owner = nullptr;

template <std::size_t N>
int IndexOf(const char* const (&names)[N], const std::string& name, int fallback) {
    const auto it = std::find(std::begin(names), std::end(names), name);
    return it == std::end(names) ? fallback : static_cast<int>(it - std::begin(names));
}

int Mode() {
    const std::string mode = system::HasStore() ? system::Store().Get(MODE_KEY)
                                                : Svc().settings->GetString(MODE_KEY.name(), "auto");
    return IndexOf(MODES, mode, 0);
}

void SetMode(int index) {
    const std::string mode = MODES[std::clamp(index, 0, MODE_SINGLE)];
    if (system::HasStore()) {
        system::Store().Set(MODE_KEY, mode);
    } else {
        Svc().settings->SetString(MODE_KEY.name(), mode);
    }
}

// The layout of one display; not chosen yet, the shape of the room decides.
ScreenLayout LayoutFor(const gfx::Rect& room) {
    const int chosen = IndexOf(LAYOUTS, Svc().settings->GetString(LAYOUT_KEY, ""), -1);
    if (chosen >= 0) {
        return static_cast<ScreenLayout>(chosen);
    }
    return room.w >= room.h ? ScreenLayout::SideBySide : ScreenLayout::Stacked;
}

void SetLayout(int index) {
    Svc().settings->SetString(LAYOUT_KEY, LAYOUTS[std::clamp(index, 0, player::SCREEN_LAYOUTS - 1)]);
}

bool Swapped() {
    return Svc().settings->GetBool(SWAP_KEY, false);
}

const std::vector<std::string>& ModeNames() {
    static const std::vector<std::string> names{"Automatic", "Top on main", "Top on second", "One display"};
    return names;
}

const std::vector<std::string>& LayoutNames() {
    static const std::vector<std::string> names{"Stacked", "Side by side", "Large and small", "One screen"};
    return names;
}

ui::RowSpec ModeRow() {
    ui::RowSpec row = ChoiceRow(
        "screens.mode", "Displays", ModeNames(), [] { return Mode(); }, [](int index) { SetMode(index); });
    row.description = "Where the two screens of a game go on a device with two displays";
    return row;
}

ui::RowSpec LayoutRow(const gfx::Rect& room) {
    ui::RowSpec row = ChoiceRow(
        "screens.layout", "On one display", LayoutNames(),
        [room] { return static_cast<int>(LayoutFor(room)); }, [](int index) { SetLayout(index); });
    row.description = "How the two screens of a game share one display";
    return row;
}

std::string RectJson(const gfx::Rect& r) {
    return std::format("[{:.1f},{:.1f},{:.1f},{:.1f}]", r.x, r.y, r.w, r.h);
}

} // namespace

DualScreens::~DualScreens() {
    Stop();
}

void DualScreens::Stop() {
    if (g_owner == this) {
        if (claimed_) {
            ReleaseSecondScreen();
        }
        g_owner = nullptr;
    }
    claimed_ = false;
    split_ = false;
    held_display_ = -1;
    release_at_ = 0;
    screens_.clear();
}

void DualScreens::Update() {
    Services& svc = Svc();
    screens_ = svc.player->Screens();
    if (g_owner != this) {
        // A game opened over another: the newer screen has the display.
        claimed_ = false;
        g_owner = this;
    }
    const bool want = Active() && Mode() != MODE_SINGLE;
    if (want && !claimed_) {
        SecondScreenClient client;
        client.draw = [this](ui::Context& ctx, const gfx::Rect& area) { DrawSecond(ctx, area); };
        client.pointer = [this](const input::Event& event) {
            const float scale = std::max(0.001f, SecondScreenScale());
            Touch(1, event.type, event.x / scale, event.y / scale);
        };
        claimed_ = ClaimSecondScreen(std::move(client));
    } else if (!want && claimed_) {
        ReleaseSecondScreen();
        claimed_ = false;
    }
    const bool split = claimed_ && SecondScreenReady();
    if (split != split_) {
        split_ = split;
        // A finger that was down belongs to a layout that is gone.
        if (held_display_ >= 0) {
            svc.player->SetScreenPointer(0, 0, 0, false);
            held_display_ = -1;
        }
    }
    if (!split_) {
        rects_[1] = {};
    }
    if (release_at_ > 0 && pal::NowSeconds() >= release_at_) {
        release_at_ = 0;
        svc.player->SetScreenPointer(held_screen_, 0, 0, false);
    }
    Note();
}

void DualScreens::Note() {
    const std::string now = Summary();
    if (now != noted_) {
        noted_ = now;
        if (Active()) {
            log::Info("player", "two screens: {}", now);
        }
    }
}

std::string DualScreens::Summary() const {
    if (!Active()) {
        return "One screen";
    }
    if (split_) {
        return Mode() == MODE_TOP_ON_SECOND ? "Top on second" : "Top on main";
    }
    std::string text = LayoutNames()[static_cast<std::size_t>(LayoutFor(room_))];
    if (Swapped()) {
        text += ", swapped";
    }
    return text;
}

void DualScreens::DrawMain(ui::Context& ctx, const gfx::Rect& room, bool integer, bool button, bool blank) {
    gfx::Canvas& canvas = *ctx.canvas;
    room_ = room;
    integer_ = integer;
    rects_[0] = {};
    button_ = {};
    if (!Active()) {
        return;
    }
    const float scale = canvas.Scale();
    ScreenLayout layout = ScreenLayout::One;
    if (split_) {
        const std::size_t shown = Mode() == MODE_TOP_ON_SECOND ? 1 : 0;
        rects_[0][shown] = player::FitScreen(screens_[shown], room, scale, integer);
    } else {
        layout = LayoutFor(room);
        rects_[0] = player::LayoutScreens({layout, room, scale, integer, Swapped()}, screens_[0], screens_[1]);
    }
    // Blank: a black frame; the screens keep their places for the touches.
    for (int i = 0; i < 2 && !blank; ++i) {
        Svc().player->DrawScreen(canvas, i, rects_[0][static_cast<std::size_t>(i)]);
    }
    if (button && !split_ && layout == ScreenLayout::One) {
        // The other screen is one tap away.
        button_ = gfx::Rect{room.Right() - BUTTON_W - BUTTON_MARGIN, room.y + BUTTON_MARGIN, BUTTON_W, BUTTON_H};
        const float size = ctx.theme->text_small;
        const std::string text = "Swap";
        canvas.FillRect(button_, gfx::Color{0, 0, 0, 150}, 8);
        canvas.DrawText(text, button_.CenterX() - ctx.Text().Measure(text, size, gfx::FONT_BOLD) * 0.5f,
                        button_.CenterY() - size * 0.6f, size, gfx::Color::Rgb(0xffffff), gfx::FONT_BOLD);
    }
}

void DualScreens::DrawSecond(ui::Context& ctx, const gfx::Rect& area) {
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(area, gfx::Color::Rgb(0x000000));
    rects_[1] = {};
    if (!Active() || !split_) {
        return;
    }
    const std::size_t shown = Mode() == MODE_TOP_ON_SECOND ? 0 : 1;
    const gfx::Rect target = player::FitScreen(screens_[shown], area, canvas.Scale(), integer_);
    rects_[1][shown] = target;
    Svc().player->DrawScreen(canvas, static_cast<int>(shown), target);
}

void DualScreens::Touch(int display, input::Event::Type type, float x, float y) {
    using Type = input::Event::Type;
    if (!Active() || display < 0 || display > 1) {
        return;
    }
    const auto& rects = rects_[static_cast<std::size_t>(display)];
    if (type == Type::PointerDown) {
        for (int i = 0; i < 2; ++i) {
            const gfx::Rect& r = rects[static_cast<std::size_t>(i)];
            if (screens_[static_cast<std::size_t>(i)].touch && r.w > 0 && r.h > 0 && r.Contains(x, y)) {
                held_display_ = display;
                held_screen_ = i;
                pressed_at_ = pal::NowSeconds();
                release_at_ = 0;
            }
        }
    }
    if (held_display_ != display || held_screen_ < 0) {
        return;
    }
    const gfx::Rect& r = rects[static_cast<std::size_t>(held_screen_)];
    if (type == Type::PointerUp || r.w <= 0 || r.h <= 0) {
        held_display_ = -1;
        // A tap that began and ended between two frames still reaches the game.
        if (pal::NowSeconds() - pressed_at_ < SHORTEST_PRESS) {
            release_at_ = pressed_at_ + SHORTEST_PRESS;
        } else {
            Svc().player->SetScreenPointer(held_screen_, 0, 0, false);
        }
        return;
    }
    // A finger that slides off the screen stays at its edge until it lifts.
    Svc().player->SetScreenPointer(held_screen_, (x - r.x) / r.w, (y - r.y) / r.h, true);
}

bool DualScreens::Pointer(const input::Event& event, float x, float y) {
    if (event.type == input::Event::Type::PointerDown && button_.w > 0 && button_.Contains(x, y)) {
        Swap();
        return true;
    }
    Touch(0, event.type, x, y);
    return false;
}

void DualScreens::Swap() {
    if (split_) {
        SetMode(Mode() == MODE_TOP_ON_SECOND ? 1 : MODE_TOP_ON_SECOND);
    } else {
        Svc().settings->SetBool(SWAP_KEY, !Swapped());
    }
    Note();
}

std::vector<ui::RowSpec> DualScreens::Rows() {
    std::vector<ui::RowSpec> rows;
    rows.push_back(ActionRow("screens.swap", "Swap screens", [this] {
        Swap();
        RebuildTop();
    }));
    rows.push_back(ModeRow());
    rows.push_back(LayoutRow(room_));
    for (ui::RowSpec& row : rows) {
        row.description.clear(); // the menu's rows are one line high
    }
    return rows;
}

std::vector<ui::RowSpec> DualScreenSettingRows() {
    // Without a game on screen the layout is the one a wide window gets.
    return {ModeRow(), LayoutRow(gfx::Rect{0, 0, 16, 9})};
}

std::string DualScreens::Json() const {
    if (!Active()) {
        return "null";
    }
    return std::format("{{\"screens\":2,\"split\":{},\"mode\":\"{}\",\"summary\":\"{}\",\"main\":[{},{}],"
                       "\"second\":[{},{}]}}",
                       split_, MODES[Mode()], Summary(), RectJson(rects_[0][0]), RectJson(rects_[0][1]),
                       RectJson(rects_[1][0]), RectJson(rects_[1][1]));
}

std::string DualScreenJson() {
    return g_owner ? g_owner->Json() : "null";
}

} // namespace den::app
