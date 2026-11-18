// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Multi-finger touch controller tracking independent thumbs across D-Pads, sticks, rolling face
// clusters, shoulders, Start/Select pills, and utility actions.

#include <algorithm>
#include <cmath>

#include "touch_spec.h"

namespace den::app {

namespace {

constexpr float DPAD_DEADZONE = 0.16f;
constexpr float DPAD_HIT_FACTOR = 1.24f;
constexpr float STICK_DEADZONE = 0.08f;
constexpr float STICK_HIT_FACTOR = 1.25f;
constexpr float STICK_TRAVEL = 0.72f;
constexpr float DIRECT_SLOP = 4.0f;

TouchController::UtilityHit UtilityForKind(TouchTargetKind kind) {
    switch (kind) {
    case TouchTargetKind::UtilityMenu:
        return TouchController::UtilityHit::Menu;
    case TouchTargetKind::UtilityFastForward:
        return TouchController::UtilityHit::FastForward;
    case TouchTargetKind::UtilitySwap:
        return TouchController::UtilityHit::Swap;
    default:
        return TouchController::UtilityHit::None;
    }
}

void ApplyBitMask(std::uint16_t& buttons, std::uint8_t mask) {
    if (mask & 1u) buttons |= static_cast<std::uint16_t>(1u << RETRO_UP);
    if (mask & 2u) buttons |= static_cast<std::uint16_t>(1u << RETRO_DOWN);
    if (mask & 4u) buttons |= static_cast<std::uint16_t>(1u << RETRO_LEFT);
    if (mask & 8u) buttons |= static_cast<std::uint16_t>(1u << RETRO_RIGHT);
}

std::uint8_t MaskFromAxes(float ax, float ay, float thresh = 0.40f) {
    std::uint8_t mask = 0;
    if (ay < -thresh) mask |= 1u;
    if (ay > thresh) mask |= 2u;
    if (ax < -thresh) mask |= 4u;
    if (ax > thresh) mask |= 8u;
    return mask;
}

} // namespace

void TouchController::SetLayout(TouchDeckLayout layout) {
    const std::size_t prev_count = layout_.buttons.size();
    layout_ = std::move(layout);
    button_down_.assign(layout_.buttons.size(), false);
    if (prev_count != layout_.buttons.size()) {
        fingers_.clear();
    }
    Recompute();
}

std::uint8_t TouchController::EvalDPad(const PlacedDPad& pad, float x, float y) {
    if (pad.style == DPadStyle::None || pad.radius <= 0.0f) {
        return 0;
    }
    const float dx = x - pad.cx;
    const float dy = y - pad.cy;
    const float dist = std::hypot(dx, dy);
    if (dist < pad.radius * DPAD_DEADZONE) {
        return 0;
    }
    const float ax = std::fabs(dx);
    const float ay = std::fabs(dy);
    // Cardinal sectors use a ~25-degree half-cone (tan 25 deg ~ 0.466) so clean cardinals do not
    // wobble onto diagonals, while diagonals engage smoothly across the corner zone.
    constexpr float CARDINAL_TAN = 0.466f;
    std::uint8_t mask = 0;
    if (ax >= ay) {
        mask |= dx < 0.0f ? 4u : 8u;
        if (ay > ax * CARDINAL_TAN) {
            mask |= dy < 0.0f ? 1u : 2u;
        }
    } else {
        mask |= dy < 0.0f ? 1u : 2u;
        if (ax > ay * CARDINAL_TAN) {
            mask |= dx < 0.0f ? 4u : 8u;
        }
    }
    return mask;
}

std::pair<float, float> TouchController::EvalStick(const PlacedStick& stick, float x, float y) {
    if (stick.style == StickStyle::None || stick.well_r <= 0.0f) {
        return {0.0f, 0.0f};
    }
    const float travel = std::max(8.0f, stick.well_r * STICK_TRAVEL);
    float dx = (x - stick.cx) / travel;
    float dy = (y - stick.cy) / travel;
    const float mag = std::hypot(dx, dy);
    if (mag < STICK_DEADZONE) {
        return {0.0f, 0.0f};
    }
    if (mag > 1.0f) {
        dx /= mag;
        dy /= mag;
    }
    return {dx, dy};
}

int TouchController::HitDirectButton(float x, float y, bool include_utility) const {
    for (std::size_t i = 0; i < layout_.buttons.size(); ++i) {
        const PlacedButton& b = layout_.buttons[i];
        const bool is_util = UtilityForKind(b.kind) != UtilityHit::None;
        if (b.face_roll || (!include_utility && is_util)) {
            continue;
        }
        if (b.rect.Inset(-DIRECT_SLOP).Contains(x, y)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<int> TouchController::HitFaceCluster(float x, float y) const {
    std::vector<int> hits;
    for (std::size_t i = 0; i < layout_.buttons.size(); ++i) {
        const PlacedButton& b = layout_.buttons[i];
        if (!b.face_roll) {
            continue;
        }
        if (b.shape == ButtonShape::Circle) {
            const float r = std::max(b.rect.w, b.rect.h) * 0.56f;
            if (std::hypot(x - b.rect.CenterX(), y - b.rect.CenterY()) <= r) {
                hits.push_back(static_cast<int>(i));
            }
        } else if (b.rect.Inset(-DIRECT_SLOP).Contains(x, y)) {
            hits.push_back(static_cast<int>(i));
        }
    }
    return hits;
}

TouchController::StepResult TouchController::FingerDown(std::uint64_t finger, float x, float y) {
    // Drop any stale entry with the same finger id before re-assigning.
    std::erase_if(fingers_, [finger](const ActiveFinger& f) { return f.finger == finger; });

    if (const int direct = HitDirectButton(x, y, true); direct >= 0) {
        const PlacedButton& btn = layout_.buttons[static_cast<std::size_t>(direct)];
        const UtilityHit util = UtilityForKind(btn.kind);
        ActiveFinger entry;
        entry.finger = finger;
        entry.kind = util != UtilityHit::None ? CaptureKind::Utility : CaptureKind::Button;
        entry.primary_index = direct;
        entry.utility = util;
        fingers_.push_back(std::move(entry));
        Recompute();
        return {true, true, util};
    }

    if (std::vector<int> cluster = HitFaceCluster(x, y); !cluster.empty()) {
        ActiveFinger entry;
        entry.finger = finger;
        entry.kind = CaptureKind::FaceCluster;
        entry.cluster_indices = std::move(cluster);
        fingers_.push_back(std::move(entry));
        Recompute();
        return {true, true, UtilityHit::None};
    }

    auto try_stick = [&](const PlacedStick& s, bool right_side) -> bool {
        if (s.style == StickStyle::None || std::hypot(x - s.cx, y - s.cy) > s.well_r * STICK_HIT_FACTOR) {
            return false;
        }
        ActiveFinger entry;
        entry.finger = finger;
        entry.kind = CaptureKind::Stick;
        entry.right_side = right_side;
        const auto [ax, ay] = EvalStick(s, x, y);
        entry.axis_x = ax;
        entry.axis_y = ay;
        fingers_.push_back(std::move(entry));
        Recompute();
        return true;
    };
    if (try_stick(layout_.left_stick, false) || try_stick(layout_.right_stick, true)) {
        return {true, true, UtilityHit::None};
    }

    auto try_dpad = [&](const PlacedDPad& d, bool right_side) -> bool {
        if (d.style == DPadStyle::None || std::hypot(x - d.cx, y - d.cy) > d.radius * DPAD_HIT_FACTOR) {
            return false;
        }
        ActiveFinger entry;
        entry.finger = finger;
        entry.kind = CaptureKind::DPad;
        entry.right_side = right_side;
        entry.dpad_mask = EvalDPad(d, x, y);
        fingers_.push_back(std::move(entry));
        Recompute();
        return true;
    };
    if (try_dpad(layout_.left_dpad, false) || try_dpad(layout_.right_dpad, true)) {
        return {true, true, UtilityHit::None};
    }

    Recompute();
    return {false, false, UtilityHit::None};
}

TouchController::StepResult TouchController::FingerMove(std::uint64_t finger, float x, float y) {
    auto it = std::find_if(fingers_.begin(), fingers_.end(),
                           [finger](const ActiveFinger& f) { return f.finger == finger; });
    if (it == fingers_.end()) {
        return {false, false, UtilityHit::None};
    }

    bool haptic = false;
    switch (it->kind) {
    case CaptureKind::Utility:
        break;
    case CaptureKind::Button: {
        const auto idx = static_cast<std::size_t>(it->primary_index);
        if (idx < layout_.buttons.size() && layout_.buttons[idx].rect.Inset(-DIRECT_SLOP * 2.0f).Contains(x, y)) {
            it->cluster_indices = {it->primary_index};
        } else {
            it->cluster_indices.clear();
        }
        break;
    }
    case CaptureKind::FaceCluster: {
        std::vector<int> next = HitFaceCluster(x, y);
        for (const int idx : next) {
            if (std::find(it->cluster_indices.begin(), it->cluster_indices.end(), idx) == it->cluster_indices.end()) {
                haptic = true;
                break;
            }
        }
        it->cluster_indices = std::move(next);
        break;
    }
    case CaptureKind::DPad: {
        const PlacedDPad& pad = it->right_side ? layout_.right_dpad : layout_.left_dpad;
        const std::uint8_t next = EvalDPad(pad, x, y);
        if (next != it->dpad_mask && next != 0) {
            haptic = true;
        }
        it->dpad_mask = next;
        break;
    }
    case CaptureKind::Stick: {
        const PlacedStick& s = it->right_side ? layout_.right_stick : layout_.left_stick;
        const auto [ax, ay] = EvalStick(s, x, y);
        it->axis_x = ax;
        it->axis_y = ay;
        break;
    }
    }

    Recompute();
    return {true, haptic, UtilityHit::None};
}

TouchController::StepResult TouchController::FingerUp(std::uint64_t finger, float /*x*/, float /*y*/) {
    const auto erased = std::erase_if(fingers_, [finger](const ActiveFinger& f) { return f.finger == finger; });
    if (erased == 0) {
        return {false, false, UtilityHit::None};
    }
    Recompute();
    return {true, false, UtilityHit::None};
}

void TouchController::ReleaseAll() {
    fingers_.clear();
    Recompute();
}

void TouchController::Recompute() {
    state_ = TouchPadState{};
    left_dpad_mask_ = 0;
    right_dpad_mask_ = 0;
    button_down_.assign(layout_.buttons.size(), false);

    float ldx = 0.0f, ldy = 0.0f, rdx = 0.0f, rdy = 0.0f;

    const auto apply_button = [&](std::size_t idx) {
        if (idx >= layout_.buttons.size()) {
            return;
        }
        button_down_[idx] = true;
        const PlacedButton& b = layout_.buttons[idx];
        switch (b.kind) {
        case TouchTargetKind::ButtonBit:
            if (b.bit_or_dir >= 0 && b.bit_or_dir < 16) {
                state_.buttons |= static_cast<std::uint16_t>(1u << b.bit_or_dir);
            }
            if (b.bit_or_dir == RETRO_L2) {
                state_.lt = 1.0f;
            } else if (b.bit_or_dir == RETRO_R2) {
                state_.rt = 1.0f;
            }
            break;
        case TouchTargetKind::LeftStickDir:
            if (b.bit_or_dir == 0) ldy -= 1.0f;
            if (b.bit_or_dir == 1) ldy += 1.0f;
            if (b.bit_or_dir == 2) ldx -= 1.0f;
            if (b.bit_or_dir == 3) ldx += 1.0f;
            break;
        case TouchTargetKind::RightStickDir:
            if (b.bit_or_dir == 0) rdy -= 1.0f;
            if (b.bit_or_dir == 1) rdy += 1.0f;
            if (b.bit_or_dir == 2) rdx -= 1.0f;
            if (b.bit_or_dir == 3) rdx += 1.0f;
            break;
        default:
            break;
        }
    };

    for (const ActiveFinger& f : fingers_) {
        switch (f.kind) {
        case CaptureKind::Utility:
            if (f.primary_index >= 0 && static_cast<std::size_t>(f.primary_index) < button_down_.size()) {
                button_down_[static_cast<std::size_t>(f.primary_index)] = true;
            }
            break;
        case CaptureKind::Button:
            if (f.cluster_indices.empty() && f.primary_index >= 0) {
                apply_button(static_cast<std::size_t>(f.primary_index));
            } else {
                for (const int idx : f.cluster_indices) {
                    apply_button(static_cast<std::size_t>(idx));
                }
            }
            break;
        case CaptureKind::FaceCluster:
            for (const int idx : f.cluster_indices) {
                apply_button(static_cast<std::size_t>(idx));
            }
            break;
        case CaptureKind::DPad:
            if (f.right_side) {
                right_dpad_mask_ |= f.dpad_mask;
            } else {
                left_dpad_mask_ |= f.dpad_mask;
            }
            break;
        case CaptureKind::Stick:
            if (f.right_side) {
                state_.rx = std::clamp(state_.rx + f.axis_x, -1.0f, 1.0f);
                state_.ry = std::clamp(state_.ry + f.axis_y, -1.0f, 1.0f);
            } else {
                state_.lx = std::clamp(state_.lx + f.axis_x, -1.0f, 1.0f);
                state_.ly = std::clamp(state_.ly + f.axis_y, -1.0f, 1.0f);
            }
            break;
        }
    }

    ApplyBitMask(state_.buttons, left_dpad_mask_);
    if (right_dpad_mask_ != 0) {
        if (layout_.right_dpad.drives_right_stick) {
            if (right_dpad_mask_ & 1u) rdy -= 1.0f;
            if (right_dpad_mask_ & 2u) rdy += 1.0f;
            if (right_dpad_mask_ & 4u) rdx -= 1.0f;
            if (right_dpad_mask_ & 8u) rdx += 1.0f;
        } else {
            // WonderSwan Y-cursor: map to RetroPad right/face secondary buttons when present or X/Y/L/R.
            if (right_dpad_mask_ & 1u) state_.buttons |= static_cast<std::uint16_t>(1u << RETRO_X);
            if (right_dpad_mask_ & 2u) state_.buttons |= static_cast<std::uint16_t>(1u << RETRO_L);
            if (right_dpad_mask_ & 4u) state_.buttons |= static_cast<std::uint16_t>(1u << RETRO_Y);
            if (right_dpad_mask_ & 8u) state_.buttons |= static_cast<std::uint16_t>(1u << RETRO_R);
        }
    }

    if (layout_.left_stick.style == StickStyle::Clicky || layout_.left_stick.style == StickStyle::ArcadeBall) {
        ApplyBitMask(state_.buttons, MaskFromAxes(state_.lx, state_.ly));
    }

    state_.lx = std::clamp(state_.lx + ldx, -1.0f, 1.0f);
    state_.ly = std::clamp(state_.ly + ldy, -1.0f, 1.0f);
    state_.rx = std::clamp(state_.rx + rdx, -1.0f, 1.0f);
    state_.ry = std::clamp(state_.ry + rdy, -1.0f, 1.0f);
}

TouchPadState TouchController::State() const {
    return state_;
}

bool TouchController::ButtonDown(std::size_t index) const {
    return index < button_down_.size() && button_down_[index];
}

std::uint8_t TouchController::DPadMask(bool right_pad) const {
    return right_pad ? right_dpad_mask_ : left_dpad_mask_;
}

std::pair<float, float> TouchController::StickOffset(bool right_stick) const {
    return right_stick ? std::pair{state_.rx, state_.ry} : std::pair{state_.lx, state_.ly};
}

int TouchController::ActiveFingerCount() const {
    return static_cast<int>(fingers_.size());
}

} // namespace den::app
