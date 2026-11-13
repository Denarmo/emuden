// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "den/ui/scroller.h"

namespace den::ui {

namespace {

constexpr float REST_SPEED = 5;    // units/s at which a free fling counts as stopped
constexpr float SETTLE_SPEED = 40; // units/s at which a snapping fling hands over to the spring
constexpr float SETTLE = 8;        // 1/s: that spring, soft enough not to speed the content up again
constexpr float MAX_STEP = 0.1f;   // a stalled frame does not teleport the content

float Nearest(const std::vector<float>& snaps, float position) {
    float best = snaps.front();
    for (const float snap : snaps) {
        if (std::fabs(snap - position) < std::fabs(best - position)) {
            best = snap;
        }
    }
    return best;
}

} // namespace

void Scroller::SetBounds(float max, float view) {
    max_ = std::max(0.0f, max);
    view_ = view;
    bounded_ = true;
    if (mode_ == Mode::Idle) {
        pos_ = std::clamp(pos_, 0.0f, max_);
    } else if (mode_ == Mode::Spring) {
        target_ = std::clamp(target_, 0.0f, max_);
    }
}

float Scroller::Destination() const {
    if (mode_ == Mode::Fling) {
        return std::clamp(target_, 0.0f, max_);
    }
    return mode_ == Mode::Spring ? target_ : pos_;
}

bool Scroller::Kinetic() const {
    return Moving() && kinetic_ && std::fabs(vel_) > tuning.kinetic_speed;
}

void Scroller::Jump(float position) {
    mode_ = Mode::Idle;
    pos_ = bounded_ ? std::clamp(position, 0.0f, max_) : std::max(0.0f, position);
    vel_ = 0;
}

void Scroller::ScrollTo(float position) {
    if (mode_ == Mode::Drag) {
        return;
    }
    position = std::max(0.0f, position);
    if (bounded_) {
        position = std::min(position, max_);
    }
    if (!Moving() && std::fabs(position - pos_) < 0.01f) {
        return;
    }
    StartSpring(position, tuning.follow, false);
}

void Scroller::ScrollBy(float delta) {
    ScrollTo(Destination() + delta);
}

float Scroller::Band(float raw) const {
    const float view = view_ > 1 ? view_ : 720.0f;
    const auto stretch = [&](float over) { return view * (1.0f - 1.0f / (over * tuning.band / view + 1.0f)); };
    if (raw < 0) {
        return -stretch(-raw);
    }
    return raw > max_ ? max_ + stretch(raw - max_) : raw;
}

float Scroller::Unband(float shown) const {
    const float view = view_ > 1 ? view_ : 720.0f;
    const auto relax = [&](float over) {
        over = std::min(over, view * 0.99f);
        return view / tuning.band * (1.0f / (1.0f - over / view) - 1.0f);
    };
    if (shown < 0) {
        return -relax(-shown);
    }
    return shown > max_ ? max_ + relax(shown - max_) : shown;
}

void Scroller::BeginDrag(double time) {
    mode_ = Mode::Drag;
    vel_ = 0;
    raw_ = Unband(pos_);
    sample_count_ = sample_next_ = 0;
    DragBy(0, time);
}

void Scroller::DragBy(float delta, double time) {
    if (mode_ != Mode::Drag) {
        return;
    }
    raw_ += delta;
    pos_ = Band(raw_);
    samples_[static_cast<std::size_t>(sample_next_)] = Sample{time, raw_};
    sample_next_ = (sample_next_ + 1) % static_cast<int>(samples_.size());
    sample_count_ = std::min(sample_count_ + 1, static_cast<int>(samples_.size()));
}

float Scroller::ReleaseVelocity(double time) const {
    if (sample_count_ < 2) {
        return 0;
    }
    const int size = static_cast<int>(samples_.size());
    const auto at = [&](int back) { return samples_[static_cast<std::size_t>((sample_next_ - 1 - back + size * 2) % size)]; };
    const Sample newest = at(0);
    if (time - newest.time > tuning.pause) {
        return 0;
    }
    // The oldest sample inside the window; one older when the window holds a single burst.
    Sample oldest = newest;
    for (int back = 1; back < sample_count_; ++back) {
        const Sample sample = at(back);
        if (time - sample.time > tuning.velocity_window && newest.time - oldest.time >= 0.004) {
            break;
        }
        oldest = sample;
        if (time - sample.time > tuning.velocity_window) {
            break;
        }
    }
    const double span = newest.time - oldest.time;
    if (span < 0.001) {
        return 0;
    }
    const float velocity = static_cast<float>((newest.raw - oldest.raw) / span);
    return std::clamp(velocity, -tuning.max_velocity, tuning.max_velocity);
}

void Scroller::EndDrag(double time, const std::vector<float>& snaps) {
    if (mode_ != Mode::Drag) {
        return;
    }
    const float velocity = ReleaseVelocity(time);
    mode_ = Mode::Idle;
    vel_ = 0;
    if (pos_ < 0 || pos_ > max_) {
        StartSpring(std::clamp(pos_, 0.0f, max_), tuning.bounce, true);
        return;
    }
    if (std::fabs(velocity) < tuning.min_fling) {
        if (!snaps.empty()) {
            vel_ = velocity;
            StartSpring(std::clamp(Nearest(snaps, pos_), 0.0f, max_), tuning.follow, false);
        }
        return;
    }
    float target = pos_ + velocity / tuning.friction;
    exact_ = !snaps.empty();
    if (exact_) {
        target = Nearest(snaps, std::clamp(target, 0.0f, max_));
        if ((target - pos_) * velocity <= 0) {
            // A fling always moves on at least one position in its direction.
            for (const float snap : snaps) {
                if (velocity > 0 ? snap > pos_ + 0.5f : snap < pos_ - 0.5f) {
                    target = snap;
                    if (velocity > 0) {
                        break;
                    }
                }
            }
        }
        target = std::clamp(target, 0.0f, max_);
        if (std::fabs(target - pos_) < 0.25f) {
            pos_ = target;
            return;
        }
    }
    mode_ = Mode::Fling;
    kinetic_ = true;
    target_ = target;
    vel_ = (target_ - pos_) * tuning.friction;
}

void Scroller::StartSpring(float target, float omega, bool kinetic) {
    mode_ = Mode::Spring;
    target_ = target;
    omega_ = omega;
    kinetic_ = kinetic;
}

// A critically damped spring towards the target, solved exactly for the step.
void Scroller::StepSpring(float dt) {
    const float x = pos_ - target_;
    const float c = vel_ + omega_ * x;
    const float e = std::exp(-omega_ * dt);
    pos_ = target_ + (x + c * dt) * e;
    vel_ = (vel_ - omega_ * c * dt) * e;
    if (std::fabs(pos_ - target_) < 0.25f && std::fabs(vel_) < 4) {
        pos_ = target_;
        vel_ = 0;
        mode_ = Mode::Idle;
    }
}

void Scroller::Tick(float dt) {
    dt = std::clamp(dt, 0.0f, MAX_STEP);
    if (mode_ == Mode::Spring) {
        StepSpring(dt);
        return;
    }
    if (mode_ != Mode::Fling) {
        return;
    }
    const float k = tuning.friction;
    const auto bounce = [&](float bound, float left) {
        pos_ = bound;
        vel_ = std::clamp(vel_, -tuning.overshoot_velocity, tuning.overshoot_velocity);
        StartSpring(bound, tuning.bounce, true);
        StepSpring(left);
    };
    if (pos_ < 0 || pos_ > max_) {
        // The content shrank under the fling.
        vel_ = std::clamp(vel_, -tuning.overshoot_velocity, tuning.overshoot_velocity);
        StartSpring(std::clamp(pos_, 0.0f, max_), tuning.bounce, true);
        StepSpring(dt);
        return;
    }
    // The fling is an exponential approach to where friction would stop it:
    // distance left = d0 * exp(-k t), speed = distance left * k.
    const float dist = target_ - pos_;
    const float sign = dist < 0 ? -1.0f : 1.0f;
    if (target_ < 0 || target_ > max_) {
        const float bound = target_ < 0 ? 0.0f : max_;
        const float reach = -std::log((target_ - bound) / dist) / k;
        if (reach <= dt) {
            vel_ = (target_ - bound) * k;
            bounce(bound, dt - reach);
            return;
        }
    }
    const float tail = exact_ ? SETTLE_SPEED : REST_SPEED;
    const float speed = std::fabs(dist) * k;
    const float until_tail = speed > tail ? std::log(speed / tail) / k : 0.0f;
    if (until_tail <= dt) {
        if (speed > tail) {
            pos_ = target_ - sign * tail / k;
        }
        vel_ = sign * std::min(speed, tail);
        if (exact_) {
            StartSpring(target_, SETTLE, true);
            StepSpring(dt - until_tail);
        } else {
            vel_ = 0;
            mode_ = Mode::Idle;
        }
        return;
    }
    const float e = std::exp(-k * dt);
    pos_ = target_ - dist * e;
    vel_ = dist * e * k;
}

} // namespace den::ui
