// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-console hardware-inspired touch deck specifications, portrait/landscape layout solver,
// and multi-touch state machine for all bundled EmuDEN systems.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "den/gfx/canvas.h"

namespace den::app {

// Standard libretro RetroPad bit positions (matching player/src/pad_map.cpp).
inline constexpr int RETRO_B = 0;
inline constexpr int RETRO_Y = 1;
inline constexpr int RETRO_SELECT = 2;
inline constexpr int RETRO_START = 3;
inline constexpr int RETRO_UP = 4;
inline constexpr int RETRO_DOWN = 5;
inline constexpr int RETRO_LEFT = 6;
inline constexpr int RETRO_RIGHT = 7;
inline constexpr int RETRO_A = 8;
inline constexpr int RETRO_X = 9;
inline constexpr int RETRO_L = 10;
inline constexpr int RETRO_R = 11;
inline constexpr int RETRO_L2 = 12;
inline constexpr int RETRO_R2 = 13;
inline constexpr int RETRO_L3 = 14;
inline constexpr int RETRO_R3 = 15;

enum class DPadStyle { None, Cross, Disc, Split };
enum class StickStyle { None, Round, Concave, Octagon, Clicky, ArcadeBall };
enum class ButtonShape { Circle, Pill, AngledPill, Bumper, SquareFramed, TriangleStart };
enum class ButtonGlyph { None, PsCross, PsCircle, PsSquare, PsTriangle, ArrowUp, ArrowDown, ArrowLeft, ArrowRight };
enum class ShellAccent { None, GbSpeakerSlots, DsSpeakerDots, WoodgrainStrip, MetalStrip, PinstripeRed, PspRing, SwirlDot };

enum class TouchTargetKind {
    ButtonBit,
    LeftStickDir,
    RightStickDir,
    UtilityMenu,
    UtilityFastForward,
    UtilitySwap,
};

enum class TouchZone {
    LeftSide = 0,
    RightSide = 1,
    CenterBottom = 2,
    LeftShoulder = 3,
    RightShoulder = 4,
};

struct TouchButtonDef {
    const char* id = "";
    const char* label = "";
    ButtonShape shape = ButtonShape::Circle;
    ButtonGlyph glyph = ButtonGlyph::None;
    gfx::Color fill{};
    gfx::Color label_color{};
    TouchTargetKind kind = TouchTargetKind::ButtonBit;
    int bit_or_dir = 0; // RetroPad bit (ButtonBit) or 0=U,1=D,2=L,3=R (*StickDir)
    TouchZone zone = TouchZone::RightSide;
    float rx = 0.5f; // normalized anchor inside its zone (0..1)
    float ry = 0.5f;
    float w_scale = 1.0f;
    float h_scale = 1.0f;
    bool face_roll = false; // allows sliding thumb between adjacent face buttons
};

struct TouchConsoleSpec {
    const char* system_id = "";
    const char* display_badge = "";
    gfx::Color shell_bg{};
    gfx::Color bezel_bg{};
    gfx::Color shell_trim{};
    gfx::Color dpad_fill{};
    gfx::Color stick_fill{};
    gfx::Color cstick_fill{};
    ShellAccent accent = ShellAccent::None;
    bool power_led = false;
    DPadStyle dpad = DPadStyle::Cross;
    bool dpad_primary_left = true; // false: stick is upper-left, D-pad sits below
    bool right_dpad = false;       // secondary directional cross on right (e.g. Virtual Boy)
    bool right_dpad_drives_stick = true;
    StickStyle left_stick = StickStyle::None;
    StickStyle right_stick = StickStyle::None;
    float right_stick_rx = 0.25f;
    float right_stick_ry = 0.78f;
    float right_stick_scale = 0.85f;
    float deck_ratio = 0.41f;
    std::vector<TouchButtonDef> buttons;
};

struct PlacedDPad {
    gfx::Rect bounds{};
    float cx = 0;
    float cy = 0;
    float radius = 0;
    DPadStyle style = DPadStyle::None;
    gfx::Color fill{};
    bool drives_right_stick = false;
};

struct PlacedStick {
    gfx::Rect bounds{};
    float cx = 0;
    float cy = 0;
    float well_r = 0;
    float cap_r = 0;
    StickStyle style = StickStyle::None;
    gfx::Color fill{};
    bool right = false;
};

struct PlacedButton {
    std::string id;
    std::string label;
    gfx::Rect rect{};
    ButtonShape shape = ButtonShape::Circle;
    ButtonGlyph glyph = ButtonGlyph::None;
    gfx::Color fill{};
    gfx::Color label_color{};
    TouchTargetKind kind = TouchTargetKind::ButtonBit;
    int bit_or_dir = 0;
    bool face_roll = false;
};

struct TouchDeckLayout {
    bool portrait = false;
    std::string system_id;
    std::string display_badge;
    gfx::Color shell_bg{};
    gfx::Color bezel_bg{};
    gfx::Color shell_trim{};
    ShellAccent accent = ShellAccent::None;
    bool power_led = false;
    gfx::Rect area{};
    gfx::Rect game_room{};
    gfx::Rect bezel_rect{};
    gfx::Rect deck_rect{};
    PlacedDPad left_dpad{};
    PlacedDPad right_dpad{};
    PlacedStick left_stick{};
    PlacedStick right_stick{};
    std::vector<PlacedButton> buttons;
};

struct TouchLayoutInput {
    gfx::Rect area{};
    float safe_left = 0;
    float safe_top = 0;
    float safe_right = 0;
    float safe_bottom = 0;
    bool dual_screen = false;
    bool can_fast_forward = true;
};

struct TouchPadState {
    std::uint16_t buttons = 0;
    float lx = 0;
    float ly = 0;
    float rx = 0;
    float ry = 0;
    float lt = 0;
    float rt = 0;
};

const TouchConsoleSpec& TouchSpecForSystem(std::string_view system_id);
std::span<const std::string_view> AllBundledTouchSystems();

TouchDeckLayout LayoutTouchDeck(const TouchConsoleSpec& spec, const TouchLayoutInput& in);

/// Multi-finger touch state tracker independent of any windowing API.
class TouchController {
public:
    enum class UtilityHit { None, Menu, FastForward, Swap };

    struct StepResult {
        bool consumed = false;
        bool haptic = false;
        UtilityHit utility = UtilityHit::None;
    };

    void SetLayout(TouchDeckLayout layout);
    const TouchDeckLayout& Layout() const {
        return layout_;
    }

    StepResult FingerDown(std::uint64_t finger, float x, float y);
    StepResult FingerMove(std::uint64_t finger, float x, float y);
    StepResult FingerUp(std::uint64_t finger, float x, float y);
    void ReleaseAll();

    TouchPadState State() const;
    bool ButtonDown(std::size_t index) const;
    std::uint8_t DPadMask(bool right_pad = false) const;
    std::pair<float, float> StickOffset(bool right_stick = false) const;
    int ActiveFingerCount() const;

private:
    enum class CaptureKind { FaceCluster, Button, DPad, Stick, Utility };

    struct ActiveFinger {
        std::uint64_t finger = 0;
        CaptureKind kind = CaptureKind::Button;
        bool right_side = false;
        int primary_index = -1;
        std::vector<int> cluster_indices;
        std::uint8_t dpad_mask = 0;
        float axis_x = 0;
        float axis_y = 0;
        UtilityHit utility = UtilityHit::None;
    };

    void Recompute();
    int HitDirectButton(float x, float y, bool include_utility) const;
    std::vector<int> HitFaceCluster(float x, float y) const;
    static std::uint8_t EvalDPad(const PlacedDPad& pad, float x, float y);
    static std::pair<float, float> EvalStick(const PlacedStick& stick, float x, float y);

    TouchDeckLayout layout_;
    std::vector<ActiveFinger> fingers_;
    std::vector<bool> button_down_;
    std::uint8_t left_dpad_mask_ = 0;
    std::uint8_t right_dpad_mask_ = 0;
    TouchPadState state_{};
};

} // namespace den::app
