// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include "audio_out.h"
#include "den/base/log.h"

namespace den::player {

namespace {

constexpr double TARGET_SECONDS = 0.064;
constexpr double UNSTEADY_TARGET_SECONDS = 0.112;
constexpr double TARGET_MAX_SECONDS = 0.192;
constexpr double WATCH_SECONDS = 2.0; // the queue's lowest point is judged this often
constexpr double MAX_DRIFT = 0.015;
constexpr double STARVED_SECONDS = 0.004;

SDL_AudioStream* Stream(void* stream) {
    return static_cast<SDL_AudioStream*>(stream);
}

void SDLCALL DeviceWants(void* userdata, SDL_AudioStream*, int additional, int) {
    static_cast<AudioOut*>(userdata)->OnDeviceWants(additional);
}

void SDLCALL DevicePlays(void* userdata, const SDL_AudioSpec* spec, float* buffer, int bytes) {
    static_cast<AudioOut*>(userdata)->Recording().Add(
        spec->freq, spec->channels,
        std::span<const float>(buffer, static_cast<std::size_t>(bytes) / sizeof(float)));
}

} // namespace

double DriftRatio(double queued, double target) {
    const double error = (queued - target) / target;
    return 1.0 + std::clamp(error * MAX_DRIFT, -MAX_DRIFT, MAX_DRIFT);
}

AudioOut::~AudioOut() {
    Close();
}

bool AudioOut::Open(double sample_rate, bool steady) {
    Close();
    sample_rate_ = sample_rate > 1000.0 ? sample_rate : 44100.0;
    ratio_ = 1.0;
    base_target_ = steady ? TARGET_SECONDS : UNSTEADY_TARGET_SECONDS;
    target_ = base_target_;
    watch_low_ = -1.0;
    watch_frames_ = 0;
    primed_ = false;
    paused_ = false;
    feeding_ = false;
    ResetStats();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::Warn("player", "no sound: {}", SDL_GetError());
        return false;
    }
    audio_started_ = true;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(sample_rate_ + 0.5);
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        log::Warn("player", "no sound device: {}", SDL_GetError());
        return false;
    }
    SDL_SetAudioStreamGetCallback(Stream(stream_), DeviceWants, this);
    SDL_ResumeAudioStreamDevice(Stream(stream_));
    return true;
}

void AudioOut::OnDeviceWants(int missing_bytes) {
    if (missing_bytes <= 0 || !feeding_.load()) {
        starving_ = false;
        return;
    }
    starved_ += static_cast<std::uint64_t>(missing_bytes) / 4;
    if (!starving_.exchange(true)) {
        ++underruns_;
    }
}

AudioStats AudioOut::Stats() const {
    AudioStats stats;
    stats.sample_rate = sample_rate_;
    stats.frames_queued = pushed_;
    stats.frames_dropped = dropped_;
    stats.overruns = overruns_;
    stats.underruns = underruns_.load();
    stats.frames_starved = starved_.load();
    stats.refills = refills_;
    stats.queue_ms = QueuedSeconds() * 1000.0;
    stats.queue_avg_ms = queue_samples_ ? queue_sum_ / static_cast<double>(queue_samples_) * 1000.0 : 0.0;
    stats.queue_min_ms = queue_min_ * 1000.0;
    stats.queue_max_ms = queue_max_ * 1000.0;
    stats.target_ms = target_ * 1000.0;
    stats.ratio = ratio_;
    stats.ratio_min = ratio_min_;
    stats.ratio_max = ratio_max_;
    return stats;
}

void AudioOut::ResetStats() {
    pushed_ = 0;
    dropped_ = 0;
    overruns_ = 0;
    refills_ = 0;
    underruns_ = 0;
    starved_ = 0;
    queue_sum_ = 0.0;
    queue_samples_ = 0;
    queue_min_ = queue_max_ = 0.0;
    ratio_min_ = ratio_max_ = ratio_;
}

bool AudioOut::StartCapture(double seconds) {
    if (!stream_) {
        return false;
    }
    StopCapture();
    recording_.Begin(seconds);
    capture_hooked_ = SDL_SetAudioPostmixCallback(SDL_GetAudioStreamDevice(Stream(stream_)), DevicePlays, this);
    return capture_hooked_;
}

void AudioOut::StopCapture() {
    if (capture_hooked_ && stream_) {
        SDL_SetAudioPostmixCallback(SDL_GetAudioStreamDevice(Stream(stream_)), nullptr, nullptr);
    }
    capture_hooked_ = false;
}

const AudioRecording& AudioOut::FinishCapture() {
    StopCapture();
    return recording_;
}

void AudioOut::Close() {
    feeding_ = false;
    StopCapture();
    if (stream_) {
        SDL_DestroyAudioStream(Stream(stream_));
        stream_ = nullptr;
    }
    if (audio_started_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio_started_ = false;
    }
}

void AudioOut::Push(std::span<const std::int16_t> samples) {
    pushed_ += samples.size() / 2;
    if (!stream_ || samples.empty()) {
        dropped_ += samples.size() / 2;
        return;
    }
    // Too far behind (the app stalled): start over rather than play late for good.
    double queued = QueuedSeconds();
    if (queued > std::max(0.25, target_ * 2.5)) {
        feeding_ = false;
        SDL_ClearAudioStream(Stream(stream_));
        dropped_ += static_cast<std::uint64_t>(queued * sample_rate_);
        ++overruns_;
        primed_ = false;
        queued = 0.0;
    }
    // An empty queue plays every hiccup of the frame loop as a crackle: start (and start
    // again after the queue ran dry) with the target's worth of silence in front.
    if (!primed_ || queued < STARVED_SECONDS) {
        if (primed_) {
            // The game stalled for longer than the queue lasts: keep more from now on.
            ++refills_;
            target_ = std::min(target_ * 1.5, TARGET_MAX_SECONDS);
        }
        Prime(queued);
    }
    SDL_PutAudioStreamData(Stream(stream_), samples.data(),
                           static_cast<int>(samples.size() * sizeof(std::int16_t)));
}

void AudioOut::Prime(double queued) {
    const double missing = target_ - queued;
    if (missing > 0) {
        const std::vector<std::int16_t> silence(
            static_cast<std::size_t>(missing * sample_rate_) * 2, 0);
        SDL_PutAudioStreamData(Stream(stream_), silence.data(),
                               static_cast<int>(silence.size() * sizeof(std::int16_t)));
    }
    primed_ = true;
    ratio_ = 1.0;
    feeding_ = true;
}

void AudioOut::SetPaused(bool paused) {
    if (!stream_) {
        return;
    }
    paused_ = paused;
    if (paused) {
        SDL_PauseAudioStreamDevice(Stream(stream_));
    } else {
        SDL_ResumeAudioStreamDevice(Stream(stream_));
    }
}

void AudioOut::Clear() {
    feeding_ = false;
    if (stream_) {
        SDL_ClearAudioStream(Stream(stream_));
    }
    primed_ = false;
}

void AudioOut::Tune() {
    if (!stream_) {
        return;
    }
    const double queued = QueuedSeconds();
    // Smoothed, so the pitch never steps audibly.
    ratio_ += (DriftRatio(queued, target_) - ratio_) * 0.05;
    SDL_SetAudioStreamFrequencyRatio(Stream(stream_), static_cast<float>(ratio_));
    if (primed_ && !paused_) {
        Watch(queued);
        queue_min_ = queue_samples_ ? std::min(queue_min_, queued) : queued;
        queue_max_ = queue_samples_ ? std::max(queue_max_, queued) : queued;
        queue_sum_ += queued;
        ++queue_samples_;
        ratio_min_ = std::min(ratio_min_, ratio_);
        ratio_max_ = std::max(ratio_max_, ratio_);
    }
}

void AudioOut::Watch(double queued) {
    // A game whose frames come unevenly (loading, shaders being built) empties the queue in
    // dips. A dip that came close to empty raises the target before one gets there; a long
    // calm stretch lets it come back down, a little at a time.
    watch_low_ = watch_low_ < 0 ? queued : std::min(watch_low_, queued);
    if (++watch_frames_ * (1.0 / 60.0) < WATCH_SECONDS) {
        return;
    }
    target_ = NextTarget(target_, watch_low_, base_target_);
    watch_low_ = -1.0;
    watch_frames_ = 0;
}

double NextTarget(double target, double lowest, double base) {
    if (lowest < target * 0.4) {
        return std::min(target * 1.4, TARGET_MAX_SECONDS);
    }
    if (lowest > target * 0.7) {
        return std::max(target * 0.98, base);
    }
    return target;
}

void AudioOut::SetVolume(float volume) {
    if (stream_) {
        SDL_SetAudioStreamGain(Stream(stream_), std::clamp(volume, 0.0f, 1.0f));
    }
}

double AudioOut::QueuedSeconds() const {
    if (!stream_) {
        return 0.0;
    }
    const int bytes = SDL_GetAudioStreamQueued(Stream(stream_));
    return bytes > 0 ? bytes / (4.0 * sample_rate_) : 0.0;
}

} // namespace den::player
