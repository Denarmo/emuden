// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The player's sound: the core's samples go into an SDL audio stream (the queue between the
// core and the device, resampled by SDL), whose playback rate is nudged so the queue stays
// near its target: the core and the sound card never run at exactly the same speed.

#pragma once

#include <atomic>
#include <cstdint>
#include <span>

#include "audio_capture.h"

namespace den::player {

class AudioOut {
public:
    AudioOut() = default;
    ~AudioOut();
    AudioOut(const AudioOut&) = delete;
    AudioOut& operator=(const AudioOut&) = delete;

    /// False when there is no sound device; samples are then counted and dropped. `steady`
    /// is false for a core whose frames come unevenly (one that draws with the GPU and builds
    /// shaders as it goes): its queue starts longer.
    bool Open(double sample_rate, bool steady = true);
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
    /// Times the device asked for more than was queued while the game played (each is a gap
    /// one can hear).
    int Underruns() const {
        return underruns_.load();
    }
    /// Sample frames pushed since Open.
    std::uint64_t FramesPushed() const {
        return pushed_;
    }
    AudioStats Stats() const;
    /// The totals and the queue's average start again; the stream is left alone.
    void ResetStats();

    /// Records the device's output for `seconds`; false without a device.
    bool StartCapture(double seconds);
    bool Capturing() const {
        return recording_.Active() && !recording_.Complete();
    }
    /// The finished (or interrupted) recording; stops listening to the device.
    const AudioRecording& FinishCapture();

    /// From the audio thread.
    void OnDeviceWants(int missing_bytes);
    AudioRecording& Recording() {
        return recording_;
    }

private:
    void Prime(double queued);
    void StopCapture();
    void Watch(double queued);

    void* stream_ = nullptr; // SDL_AudioStream
    double sample_rate_ = 44100.0;
    double ratio_ = 1.0;
    double base_target_ = 0.064;
    double target_ = 0.064;   // seconds the queue is kept at; grows when the game stalls
    double watch_low_ = -1.0; // the queue's lowest point since it was last judged
    int watch_frames_ = 0;
    double ratio_min_ = 1.0, ratio_max_ = 1.0;
    std::uint64_t pushed_ = 0;
    std::uint64_t dropped_ = 0;
    int overruns_ = 0;
    int refills_ = 0;
    double queue_sum_ = 0.0, queue_min_ = 0.0, queue_max_ = 0.0;
    std::uint64_t queue_samples_ = 0;
    bool audio_started_ = false;
    bool primed_ = false;
    bool paused_ = false;
    bool capture_hooked_ = false;
    // Written by the audio thread.
    std::atomic<bool> feeding_{false}; // the game is playing: an empty queue is a gap
    std::atomic<bool> starving_{false};
    std::atomic<int> underruns_{0};
    std::atomic<std::uint64_t> starved_{0};
    AudioRecording recording_;
};

/// The playback rate for a queue of `queued` seconds against a target: 1.0 on target, at most
/// 1.5 % faster or slower.
double DriftRatio(double queued, double target);

/// The queue's target after a stretch in which its lowest point was `lowest` seconds: higher
/// when it nearly ran dry, slowly back to the usual 64 ms when it stayed well filled.
double NextTarget(double target, double lowest, double base = 0.064);

} // namespace den::player
