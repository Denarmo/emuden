// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The unlock sound: two bell-like notes made here, played on a stream of their own beside
// the game's sound.

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

#include "den/achievements/chime.h"

namespace den::achievements {

namespace {

constexpr int RATE = 48000;

std::vector<float> MakeChime() {
    constexpr double PI = 3.14159265358979323846;
    const struct {
        double start, freq, length;
    } notes[] = {{0.0, 1318.51, 0.45}, {0.11, 1975.53, 0.6}}; // E6, then B6
    const std::size_t total = static_cast<std::size_t>(RATE * 0.75);
    std::vector<float> out(total * 2, 0.0f);
    for (const auto& note : notes) {
        const std::size_t first = static_cast<std::size_t>(note.start * RATE);
        const std::size_t count = static_cast<std::size_t>(note.length * RATE);
        for (std::size_t i = 0; i < count && first + i < total; ++i) {
            const double t = static_cast<double>(i) / RATE;
            const double attack = std::min(1.0, t / 0.004);
            const double envelope = attack * std::exp(-t * 7.0);
            const double tone = std::sin(2 * PI * note.freq * t) + 0.3 * std::sin(4 * PI * note.freq * t);
            const float value = static_cast<float>(0.22 * envelope * tone);
            out[(first + i) * 2] += value;
            out[(first + i) * 2 + 1] += value;
        }
    }
    return out;
}

} // namespace

void PlayChime(float volume) {
    static std::mutex mutex;
    static SDL_AudioStream* stream = nullptr;
    static bool tried = false;
    static const std::vector<float> chime = MakeChime();
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
    std::vector<float> samples = chime;
    const float gain = std::clamp(volume, 0.0f, 1.0f);
    for (float& sample : samples) {
        sample *= gain;
    }
    SDL_ClearAudioStream(stream);
    SDL_PutAudioStreamData(stream, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
}

} // namespace den::achievements
