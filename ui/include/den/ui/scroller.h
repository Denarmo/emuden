// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

namespace den::ui {

/// The motion of one scrolling axis: a finger that drags it, the fling after the finger
/// leaves, the rubber band past the ends and the animated moves of the wheel and of focus.
/// Positions are in logical units, times in seconds. Every step is a closed form of the
/// elapsed time, so the motion is the same at 60, 90 and 120 Hz.
class Scroller {
public:
    struct Tuning {
        float friction = 2.6f;           // 1/s: a fling loses speed as exp(-friction * t)
        float max_velocity = 6000;       // units/s a fling can start with
        float min_fling = 70;            // units/s below which lifting the finger just stops
        float overshoot_velocity = 2600; // units/s kept when a fling reaches an end
        float bounce = 13;               // 1/s: the spring that returns from past an end
        float follow = 20;               // 1/s: the spring of wheel and focus scrolling
        float band = 0.55f;              // rubber band stiffness while dragging past an end
        float velocity_window = 0.1f;    // seconds of finger history the velocity is read from
        float pause = 0.08f;             // a finger at rest this long before lifting does not fling
        float kinetic_speed = 50;        // units/s above which a touch stops the motion
    };
    Tuning tuning;

    /// The furthest position (content minus window) and the window's length.
    void SetBounds(float max, float view);
    float Position() const {
        return pos_;
    }
    float Velocity() const {
        return vel_;
    }
    float Max() const {
        return max_;
    }
    /// Where the motion will come to rest (the position itself when idle).
    float Destination() const;
    bool Moving() const {
        return mode_ == Mode::Fling || mode_ == Mode::Spring;
    }
    bool Dragging() const {
        return mode_ == Mode::Drag;
    }
    /// A fling or its bounce that is still fast enough for a touch to stop it rather than
    /// to count as a tap.
    bool Kinetic() const;

    /// Moves there at once and stops.
    void Jump(float position);
    /// Animated move (focus scrolling). Not clamped at the far end until bounds are known.
    void ScrollTo(float position);
    /// Animated move relative to where the motion was heading (the wheel).
    void ScrollBy(float delta);

    /// A finger took hold: any motion stops under it.
    void BeginDrag(double time);
    /// The finger moved the content by `delta` (positive scrolls forward).
    void DragBy(float delta, double time);
    /// The finger left: flings, springs back from past an end, or settles. With `snaps`
    /// (ascending rest positions) the motion ends on one of them.
    void EndDrag(double time, const std::vector<float>& snaps = {});

    void Tick(float dt);

private:
    enum class Mode { Idle, Drag, Fling, Spring };
    struct Sample {
        double time = 0;
        float raw = 0;
    };

    float Band(float raw) const;
    float Unband(float shown) const;
    float ReleaseVelocity(double time) const;
    void StartSpring(float target, float omega, bool kinetic);
    void StepSpring(float dt);

    Mode mode_ = Mode::Idle;
    float pos_ = 0, vel_ = 0, max_ = 0, view_ = 0;
    float target_ = 0, omega_ = 0;
    float raw_ = 0;        // where the finger has put the content, before the rubber band
    bool kinetic_ = false; // the motion came from a finger
    bool exact_ = false;   // the fling must end exactly on its target (a snap position)
    bool bounded_ = false;
    std::array<Sample, 24> samples_{};
    int sample_count_ = 0, sample_next_ = 0;
};

} // namespace den::ui
