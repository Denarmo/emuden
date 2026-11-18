// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Controllers: the connected pads by name, which of them is player 1, 2, ..., and a test view
// that shows what is pressed.

#include <algorithm>
#include <cmath>
#include <format>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/app/touch_settings.h"
#include "den/ui/glyphs.h"

namespace den::app {

namespace {

using input::PadButton;
using input::PadFamily;

constexpr const char* PLAYERS_KEY = "input.players"; // pad names in player order
constexpr float LEAVE_SECONDS = 1.0f;

std::string LayoutText(PadFamily family) {
    switch (family) {
    case PadFamily::Xbox:
        return "Xbox layout";
    case PadFamily::PlayStation:
        return "PlayStation layout";
    case PadFamily::Nintendo:
        return "Nintendo layout";
    default:
        return "Keyboard";
    }
}

std::string PadNames() {
    std::string names;
    const input::System& input = *Svc().input;
    for (int i = 0; i < input.PadCount(); ++i) {
        names += input.Pad(i).name + "\n";
    }
    return names;
}

void RememberPlayers() {
    std::vector<std::string> names;
    const input::System& input = *Svc().input;
    for (int i = 0; i < input.PadCount(); ++i) {
        names.push_back(input.Pad(i).name);
    }
    Svc().settings->SetList(PLAYERS_KEY, names);
}

// Puts the pads back in the order the user chose, as far as those pads are connected.
void ApplyRememberedPlayers() {
    input::System& input = *Svc().input;
    const std::vector<std::string> wanted = Svc().settings->GetList(PLAYERS_KEY);
    int player = 0;
    for (const std::string& name : wanted) {
        for (int i = player; i < input.PadCount(); ++i) {
            if (input.Pad(i).name == name) {
                input.SetPlayer(i, player++);
                break;
            }
        }
    }
}

std::vector<ui::RowSpec> ControllerRows() {
    std::vector<ui::RowSpec> rows;
    const input::System& input = *Svc().input;
    const int count = input.PadCount();
    std::vector<std::string> players;
    for (int i = 0; i < count; ++i) {
        players.push_back(TrF("Player {}", i + 1));
    }
    for (int i = 0; i < count; ++i) {
        const input::PadState& pad = input.Pad(i);
        ui::RowSpec row = ChoiceRow(
            std::format("pad.{}", i), pad.name.empty() ? "Controller" : pad.name, players, [i] { return i; },
            [i](int player) {
                Svc().input->SetPlayer(i, player);
                RememberPlayers();
                RebuildTop();
            });
        row.description = LayoutText(pad.family);
        rows.push_back(std::move(row));
    }
    if (count == 0) {
        rows.push_back(ValueRow("pad.none", "No controller connected",
                                [] { return std::string("Connect one at any time"); }));
    }
    ui::RowSpec keyboard = ValueRow("pad.keyboard", "Keyboard", [] { return std::string("Player 1"); });
    keyboard.description = "Arrows, Z X A S, Q W, Enter, right Shift";
    rows.push_back(std::move(keyboard));
    rows.push_back(ActionRow("pad.test", "Test buttons", [] { Svc().stage->Push("controller_test"); },
                             "Shows what each controller presses"));
    ui::RowSpec active = ValueRow(
        "pad.active", "Button hints", [] { return LayoutText(Svc().input->ActiveFamily()); },
        [] { Svc().stage->Push("controller_glyphs"); });
    active.description = "Follow the device used last. Select to see every symbol";
    rows.push_back(std::move(active));
    for (ui::RowSpec& row : TouchSettingRows()) {
        rows.push_back(std::move(row));
    }
    return rows;
}

/// A pad seen from the front: every button as its glyph, lit while it is held.
class PadView final : public ui::Node {
public:
    input::PadState pad;
    std::string title;

protected:
    void DrawContent(ui::Context& ctx) override {
        const ui::Theme& theme = *ctx.theme;
        gfx::Canvas& canvas = *ctx.canvas;
        const float u = std::min(rect.w / 16.0f, (rect.h - 96) / 8.6f);
        const float cx = rect.CenterX(), cy = rect.y + 40 + 4.9f * u;
        canvas.DrawText(title, rect.x, rect.y, theme.text_body, theme.text_dim);

        canvas.FillRect(gfx::Rect{cx - 7.2f * u, cy - 2.4f * u, 14.4f * u, 5.6f * u}, theme.panel, 2.2f * u);
        const float g = u * 1.15f;
        const auto button = [&](PadButton which, float dx, float dy) {
            const float w = ui::PadGlyphWidth(ctx, pad.family, which, g);
            if (w <= 0) {
                return;
            }
            const float x = cx + dx * u - w * 0.5f, y = cy + dy * u - g * 0.5f;
            const bool held = pad.Down(which);
            if (held) {
                canvas.FillRect(gfx::Rect{x - 6, y - 6, w + 12, g + 12}, theme.panel_strong, (g + 12) * 0.5f);
            }
            ui::DrawPadGlyph(ctx, pad.family, which, x, y, g, held ? theme.fg : theme.text_dim.WithAlpha(0.55f),
                             held);
        };
        button(PadButton::L2, -4.6f, -4.2f);
        button(PadButton::R2, 4.6f, -4.2f);
        button(PadButton::L1, -4.6f, -3.05f);
        button(PadButton::R1, 4.6f, -3.05f);
        if (pad.family == PadFamily::Keyboard) {
            button(PadButton::DpadUp, -4.6f, -1.15f);
            button(PadButton::DpadDown, -4.6f, 1.15f);
            button(PadButton::DpadLeft, -5.75f, 0);
            button(PadButton::DpadRight, -3.45f, 0);
        } else {
            // One cross, each arm lit while it is held.
            const float arm = 1.05f * u, reach = 1.2f * u, dx = cx - 4.6f * u;
            const auto part = [&](PadButton which, float ox, float oy) {
                const bool held = which != PadButton::Count && pad.Down(which);
                canvas.FillRect(gfx::Rect{dx + ox * reach - arm * 0.5f, cy + oy * reach - arm * 0.5f, arm, arm},
                                held ? theme.fg : theme.panel_strong, u * 0.18f);
            };
            part(PadButton::Count, 0, 0);
            part(PadButton::DpadUp, 0, -0.9f);
            part(PadButton::DpadDown, 0, 0.9f);
            part(PadButton::DpadLeft, -0.9f, 0);
            part(PadButton::DpadRight, 0.9f, 0);
        }
        button(PadButton::North, 4.6f, -1.15f);
        button(PadButton::South, 4.6f, 1.15f);
        button(PadButton::West, 3.45f, 0);
        button(PadButton::East, 5.75f, 0);
        button(PadButton::Select, -1.4f, -1.2f);
        button(PadButton::Guide, 0, -1.2f);
        button(PadButton::Start, 1.4f, -1.2f);
        if (pad.family != PadFamily::Keyboard) {
            Stick(ctx, cx - 1.9f * u, cy + 1.6f * u, u, pad.lx, pad.ly, pad.Down(PadButton::L3), "L");
            Stick(ctx, cx + 1.9f * u, cy + 1.6f * u, u, pad.rx, pad.ry, pad.Down(PadButton::R3), "R");
        }

        std::string held;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(PadButton::Count); ++i) {
            if (pad.Down(static_cast<PadButton>(i))) {
                held += (held.empty() ? "" : ", ") + ui::PadButtonName(pad.family, static_cast<PadButton>(i));
            }
        }
        const std::string line = held.empty() ? "Press any button" : TrF("Pressed: {}", held);
        const float line_w = ctx.Text().Measure(line, theme.text_body, gfx::FONT_BOLD);
        canvas.DrawText(line, cx - line_w * 0.5f, cy + 3.6f * u, theme.text_body,
                        held.empty() ? theme.text_dim : theme.fg, gfx::FONT_BOLD);
    }

private:
    // The stick's well, and its cap where the stick is pushed.
    static void Stick(ui::Context& ctx, float cx, float cy, float u, float x, float y, bool pressed,
                      const char* letter) {
        const ui::Theme& theme = *ctx.theme;
        gfx::Canvas& canvas = *ctx.canvas;
        const float well = 1.9f * u, cap = 1.15f * u;
        canvas.StrokeRect(gfx::Rect{cx - well * 0.5f, cy - well * 0.5f, well, well}, theme.text_dim, 2, well * 0.5f);
        const float px = cx + x * 0.36f * u, py = cy + y * 0.36f * u;
        const bool moved = std::max(std::abs(x), std::abs(y)) > 0.2f;
        canvas.FillRect(gfx::Rect{px - cap * 0.5f, py - cap * 0.5f, cap, cap},
                        pressed || moved ? theme.fg : theme.panel_strong, cap * 0.5f);
        const float size = cap * 0.5f;
        const float w = ctx.Text().Measure(letter, size, gfx::FONT_BOLD);
        canvas.DrawText(letter, px - w * 0.5f, py - size * 0.61f, size, pressed || moved ? theme.page : theme.fg,
                        gfx::FONT_BOLD);
    }
};

class ControllerTestScreen final : public ui::Page {
public:
    ~ControllerTestScreen() override {
        if (Svc().input) {
            Svc().input->SetNavFromPads(true);
        }
    }

    void Build() override {
        ui::Node* content = BuildPlainFrame(*this, "Test buttons");
        content->pad_right = Svc().theme->margin;
        view_ = content->Emplace<PadView>();
        view_->id = "controller_test.view";
        view_->grow = 1;
    }

    // The buttons are being tested: they must not move around the app meanwhile.
    void OnShow() override {
        Svc().input->SetNavFromPads(false);
    }
    void OnHide() override {
        Svc().input->SetNavFromPads(true);
    }

    void Update(ui::Context& ctx) override {
        const input::System& input = *Svc().input;
        // The device shown is the one a button was pressed on last.
        for (int i = 0; i < input.PadCount(); ++i) {
            if (input.Pad(i).buttons != 0) {
                shown_ = i;
            }
        }
        if (input.KeyboardPad().buttons != 0) {
            shown_ = -1;
        }
        if (shown_ >= input.PadCount()) {
            shown_ = input.PadCount() - 1;
        }
        if (!started_) {
            started_ = true;
            shown_ = input.PadCount() > 0 ? 0 : -1;
        }
        view_->pad = shown_ < 0 ? input.KeyboardPad() : input.Pad(shown_);
        view_->title = shown_ < 0 ? "Keyboard  \xC2\xB7  Player 1"
                                  : std::format("{}  \xC2\xB7  Player {}  \xC2\xB7  {}", view_->pad.name, shown_ + 1,
                                                LayoutText(view_->pad.family));

        // Back held on a pad leaves; a short press is only shown.
        bool leaving = false;
        for (int i = 0; i < input.PadCount(); ++i) {
            const input::PadState& pad = input.Pad(i);
            leaving = leaving || pad.Down(input::ButtonForNav(pad.family, input::Nav::Back));
        }
        held_ = leaving ? held_ + ctx.dt : 0.0f;
        if (held_ >= LEAVE_SECONDS) {
            held_ = 0;
            GetStage().Pop();
        }
    }

    std::vector<ui::Hint> Hints() const override {
        const bool keyboard = Svc().input->ActiveFamily() == PadFamily::Keyboard;
        return {{input::Nav::Back, keyboard ? "Back" : "Hold to go back"}};
    }

private:
    PadView* view_ = nullptr;
    int shown_ = -1;
    bool started_ = false;
    float held_ = 0;
};

/// Every button of every kind of controller, as the hints draw it.
class GlyphSheet final : public ui::Node {
protected:
    void DrawContent(ui::Context& ctx) override {
        const ui::Theme& theme = *ctx.theme;
        const float g = 34;
        float y = rect.y;
        for (const PadFamily family :
             {PadFamily::Xbox, PadFamily::PlayStation, PadFamily::Nintendo, PadFamily::Keyboard}) {
            ctx.canvas->DrawText(LayoutText(family), rect.x, y, theme.text_body, theme.fg, gfx::FONT_BOLD);
            float x = rect.x;
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(PadButton::Count); ++i) {
                const float w = ui::DrawPadGlyph(ctx, family, static_cast<PadButton>(i), x, y + 34, g, theme.fg);
                x += w > 0 ? w + 12 : 0;
            }
            x = rect.x;
            for (const auto& [nav, text] : {std::pair{input::Nav::Confirm, "Select"},
                                            std::pair{input::Nav::Back, "Back"},
                                            std::pair{input::Nav::Option, "Options"},
                                            std::pair{input::Nav::Menu, "Menu"},
                                            std::pair{input::Nav::ShoulderL, "Previous"},
                                            std::pair{input::Nav::ShoulderR, "Next"}}) {
                x += ui::DrawHint(ctx, family, nav, text, x, y + 80, 24, theme.text_dim) + 22;
            }
            y += 132;
        }
    }
};

class GlyphScreen final : public ui::Page {
    void Build() override {
        ui::Node* content = BuildPlainFrame(*this, "Button symbols");
        content->Emplace<GlyphSheet>()->grow = 1;
    }
    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Back, "Back"}};
    }
};

} // namespace

DEN_REGISTER_SCREEN("controller_test", ControllerTestScreen);
DEN_REGISTER_SCREEN("controller_glyphs", GlyphScreen);
DEN_REGISTER_SETTINGS(40, "Controllers", ControllerRows);
DEN_REGISTER_FRAME([] {
    // Pads connect and disconnect at any time: the remembered order is applied again.
    static std::string known;
    if (std::string names = PadNames(); names != known) {
        ApplyRememberedPlayers();
        known = PadNames();
    }
});

} // namespace den::app
