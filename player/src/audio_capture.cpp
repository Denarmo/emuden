// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio_capture.h"
#include "den/base/files.h"

namespace den::player {

namespace {

constexpr float SILENT = 1.0f / 32768.0f; // below one step of a 16-bit sample
constexpr double GAP_MS = 5.0;            // shorter runs of zeros are part of any wave

void Put16(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    Put16(out, value & 0xffff);
    Put16(out, value >> 16);
}

} // namespace

AudioCapture MeasureAudio(std::span<const float> samples, int sample_rate, int channels) {
    AudioCapture out;
    out.sample_rate = sample_rate;
    out.channels = channels;
    if (channels <= 0 || sample_rate <= 0) {
        return out;
    }
    const auto step = static_cast<std::size_t>(channels);
    out.frames = samples.size() / step;
    double squares = 0.0;
    std::uint64_t run = 0, longest = 0, gap_frames = 0;
    const auto gap = static_cast<std::uint64_t>(GAP_MS * sample_rate / 1000.0);
    const auto end_run = [&] {
        longest = std::max(longest, run);
        if (run > gap) {
            gap_frames += run;
            ++out.silences;
        }
        run = 0;
    };
    for (std::size_t frame = 0; frame < out.frames; ++frame) {
        bool silent = true;
        for (std::size_t c = 0; c < step; ++c) {
            const float value = samples[frame * step + c];
            out.peak = std::max(out.peak, static_cast<double>(std::fabs(value)));
            squares += static_cast<double>(value) * value;
            silent = silent && std::fabs(value) < SILENT;
        }
        if (silent) {
            ++run;
        } else {
            end_run();
        }
    }
    end_run();
    out.rms = samples.empty() ? 0.0 : std::sqrt(squares / static_cast<double>(samples.size()));
    out.longest_silence_ms = static_cast<double>(longest) * 1000.0 / sample_rate;
    out.silence_ms = static_cast<double>(gap_frames) * 1000.0 / sample_rate;
    return out;
}

void AudioRecording::Begin(double seconds) {
    const std::lock_guard lock(mutex_);
    samples_.clear();
    seconds_ = std::clamp(seconds, 0.1, 120.0);
    sample_rate_ = 0;
    channels_ = 0;
    complete_ = false;
    active_ = true;
}

void AudioRecording::Add(int sample_rate, int channels, std::span<const float> samples) {
    if (!active_.load() || complete_.load() || sample_rate <= 0 || channels <= 0) {
        return;
    }
    const std::lock_guard lock(mutex_);
    if (sample_rate_ == 0) {
        sample_rate_ = sample_rate;
        channels_ = channels;
        samples_.reserve(static_cast<std::size_t>(seconds_ * sample_rate) * static_cast<std::size_t>(channels));
    }
    if (sample_rate != sample_rate_ || channels != channels_) {
        return; // the device changed its format: keep what fits
    }
    const auto wanted = static_cast<std::size_t>(seconds_ * sample_rate_) * static_cast<std::size_t>(channels_);
    const std::size_t take = std::min(samples.size(), wanted - std::min(wanted, samples_.size()));
    samples_.insert(samples_.end(), samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(take));
    if (samples_.size() >= wanted) {
        complete_ = true;
    }
}

AudioCapture AudioRecording::Measure() const {
    const std::lock_guard lock(mutex_);
    return MeasureAudio(samples_, sample_rate_, channels_);
}

Status AudioRecording::WriteWav(const std::filesystem::path& file) const {
    const std::lock_guard lock(mutex_);
    if (samples_.empty() || channels_ <= 0) {
        return Status::Fail("No sound was recorded.");
    }
    const auto data_bytes = static_cast<std::uint32_t>(samples_.size() * 2);
    const auto channels = static_cast<std::uint32_t>(channels_);
    const auto rate = static_cast<std::uint32_t>(sample_rate_);
    std::vector<std::uint8_t> out;
    out.reserve(44 + data_bytes);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    Put32(out, 36 + data_bytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    Put32(out, 16);
    Put16(out, 1); // PCM
    Put16(out, channels);
    Put32(out, rate);
    Put32(out, rate * channels * 2);
    Put16(out, channels * 2);
    Put16(out, 16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    Put32(out, data_bytes);
    for (const float value : samples_) {
        const long scaled = std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
        Put16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled)));
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (!WriteFileAtomic(file, std::string_view(reinterpret_cast<const char*>(out.data()), out.size()))) {
        return Status::Fail("The recording could not be written.");
    }
    return {};
}

} // namespace den::player
