// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The start-up sound: a quick rising D, A, then a D major chord that rings out, in a soft
// mallet tone (a bell's uneven overtones, a little detuned for warmth).

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

#include "den/player/boot_sound.h"

namespace den::player {

namespace {

constexpr int RATE = 48000;
constexpr double PI = 3.14159265358979323846;

std::vector<float> MakeSound() {
    struct Note {
        double start, freq, length, gain, pan; // pan: -1 left .. 1 right
    };
    const Note notes[] = {
        {0.00, 587.33, 0.30, 0.55, -0.25},  // D5
        {0.075, 880.00, 0.34, 0.55, 0.25},  // A5
        {0.15, 1174.66, 1.00, 0.70, 0.0},   // D6
        {0.15, 1479.98, 0.95, 0.38, 0.35},  // F#6
        {0.15, 587.33, 0.90, 0.30, -0.35},  // D5 under it
    };
    const std::size_t total = static_cast<std::size_t>(RATE * 1.2);
    std::vector<float> out(total * 2, 0.0f);
    for (const Note& note : notes) {
        const std::size_t first = static_cast<std::size_t>(note.start * RATE);
        const std::size_t count = static_cast<std::size_t>(note.length * RATE);
        const float left = static_cast<float>(std::sqrt(0.5 * (1 - note.pan)));
        const float right = static_cast<float>(std::sqrt(0.5 * (1 + note.pan)));
        for (std::size_t i = 0; i < count && first + i < total; ++i) {
            const double t = static_cast<double>(i) / RATE;
            const double attack = std::min(1.0, t / 0.006);
            const double tail = std::min(1.0, (note.length - t) / 0.08); // no click at the end
            const double body = std::exp(-t * 4.2);
            const double ping = std::exp(-t * 16.0);
            const double f = note.freq;
            const double tone = std::sin(2 * PI * f * t) + 0.5 * std::sin(2 * PI * f * 1.003 * t) +
                                0.22 * ping * std::sin(2 * PI * f * 2.76 * t) +
                                0.08 * ping * std::sin(2 * PI * f * 5.40 * t);
            const float value = static_cast<float>(0.13 * note.gain * attack * tail * body * tone);
            out[(first + i) * 2] += value * left;
            out[(first + i) * 2 + 1] += value * right;
        }
    }
    return out;
}

} // namespace

void PlayBootSound(float volume) {
    static std::mutex mutex;
    static SDL_AudioStream* stream = nullptr;
    static bool tried = false;
    static const std::vector<float> sound = MakeSound();
    std::lock_guard lock(mutex);
    if (!tried) {
        tried = true;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, RATE};
            stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (stream) {
                SDL_ResumeAudioStreamDevice(stream);
            }
        }
    }
    if (!stream) {
        return;
    }
    std::vector<float> samples = sound;
    const float gain = std::clamp(volume, 0.0f, 1.0f);
    for (float& sample : samples) {
        sample *= gain;
    }
    SDL_ClearAudioStream(stream);
    SDL_PutAudioStreamData(stream, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
}

} // namespace den::player
