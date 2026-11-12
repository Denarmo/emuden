// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A recording of what the sound device was given: filled from the audio thread, then
// measured and written as a WAV file on the main thread.

#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <span>
#include <vector>

#include "den/base/result.h"
#include "den/player/session.h"

namespace den::player {

class AudioRecording {
public:
    /// Starts over, to hold `seconds` once the first samples tell the format.
    void Begin(double seconds);
    /// From the audio thread: interleaved float samples as the device plays them.
    void Add(int sample_rate, int channels, std::span<const float> samples);
    bool Active() const {
        return active_.load();
    }
    /// True once Begin's seconds are all there.
    bool Complete() const {
        return complete_.load();
    }
    AudioCapture Measure() const;
    Status WriteWav(const std::filesystem::path& file) const;

private:
    mutable std::mutex mutex_;
    std::vector<float> samples_;
    double seconds_ = 0.0;
    int sample_rate_ = 0;
    int channels_ = 0;
    std::atomic<bool> active_{false};
    std::atomic<bool> complete_{false};
};

/// Peak, RMS and the runs of zero samples in interleaved float samples.
AudioCapture MeasureAudio(std::span<const float> samples, int sample_rate, int channels);

} // namespace den::player
