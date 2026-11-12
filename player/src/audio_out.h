// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The player's sound: the core's samples go into an SDL audio stream (the queue between the
// core and the device, resampled by SDL), whose playback rate is nudged so the queue stays
// near its target: the core and the sound card never run at exactly the same speed.

#pragma once

#include <cstdint>
#include <span>

namespace den::player {

class AudioOut {
public:
    AudioOut() = default;
    ~AudioOut();
    AudioOut(const AudioOut&) = delete;
    AudioOut& operator=(const AudioOut&) = delete;

    /// False when there is no sound device; samples are then counted and dropped.
    bool Open(double sample_rate);
    void Close();
    /// Interleaved stereo.
    void Push(std::span<const std::int16_t> samples);
    void SetPaused(bool paused);
    /// Drops what is queued (after fast-forward, a loaded state).
    void Clear();
    /// Once per display frame: adjusts the playback rate to the queue.
    void Tune();
    void SetVolume(float volume);

    double QueuedSeconds() const;
    double RateRatio() const {
        return ratio_;
    }
    /// Sample frames pushed since Open.
    std::uint64_t FramesPushed() const {
        return pushed_;
    }

private:
    void* stream_ = nullptr; // SDL_AudioStream
    double sample_rate_ = 44100.0;
    double ratio_ = 1.0;
    std::uint64_t pushed_ = 0;
    bool audio_started_ = false;
};

/// The playback rate for a queue of `queued` seconds against a target: 1.0 on target, at most
/// 1.5 % faster or slower.
double DriftRatio(double queued, double target);

} // namespace den::player
