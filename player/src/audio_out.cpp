// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include "audio_out.h"
#include "den/base/log.h"

namespace den::player {

namespace {

constexpr double TARGET_SECONDS = 0.064;
constexpr double MAX_SECONDS = 0.25;
constexpr double MAX_DRIFT = 0.015;

SDL_AudioStream* Stream(void* stream) {
    return static_cast<SDL_AudioStream*>(stream);
}

} // namespace

double DriftRatio(double queued, double target) {
    const double error = (queued - target) / target;
    return 1.0 + std::clamp(error * MAX_DRIFT, -MAX_DRIFT, MAX_DRIFT);
}

AudioOut::~AudioOut() {
    Close();
}

bool AudioOut::Open(double sample_rate) {
    Close();
    sample_rate_ = sample_rate > 1000.0 ? sample_rate : 44100.0;
    pushed_ = 0;
    ratio_ = 1.0;
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
    SDL_ResumeAudioStreamDevice(Stream(stream_));
    return true;
}

void AudioOut::Close() {
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
        return;
    }
    // Too far behind (the app stalled): start over rather than play late for good.
    if (QueuedSeconds() > MAX_SECONDS) {
        SDL_ClearAudioStream(Stream(stream_));
    }
    SDL_PutAudioStreamData(Stream(stream_), samples.data(),
                           static_cast<int>(samples.size() * sizeof(std::int16_t)));
}

void AudioOut::SetPaused(bool paused) {
    if (!stream_) {
        return;
    }
    if (paused) {
        SDL_PauseAudioStreamDevice(Stream(stream_));
    } else {
        SDL_ResumeAudioStreamDevice(Stream(stream_));
    }
}

void AudioOut::Clear() {
    if (stream_) {
        SDL_ClearAudioStream(Stream(stream_));
    }
}

void AudioOut::Tune() {
    if (!stream_) {
        return;
    }
    // Smoothed, so the pitch never steps audibly.
    ratio_ += (DriftRatio(QueuedSeconds(), TARGET_SECONDS) - ratio_) * 0.05;
    SDL_SetAudioStreamFrequencyRatio(Stream(stream_), static_cast<float>(ratio_));
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
