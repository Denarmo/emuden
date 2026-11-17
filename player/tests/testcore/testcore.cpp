// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A tiny libretro core for the player's tests: it draws a moving pattern (64x48, RGB565),
// beeps, moves a square with the D-pad, counts presses of A in its save RAM, has one option
// and saves states. Content is optional; when given, its first byte seeds the pattern. The
// cheats it is given are written down for the tests (testcore_cheats).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "libretro.h"

namespace {

constexpr unsigned WIDTH = 64, HEIGHT = 48;
constexpr unsigned SAMPLE_RATE = 44100, SAMPLES_PER_FRAME = SAMPLE_RATE / 60;

struct State {
    std::uint32_t frame = 0;
    std::int32_t x = 8, y = 8;
    std::uint32_t phase = 0;
    std::uint8_t seed = 0;
    std::uint8_t a_was_down = 0;
    std::uint8_t pad[2] = {};
};

State g_state;
std::uint8_t g_save_ram[32];
std::uint16_t g_pixels[WIDTH * HEIGHT];
std::uint16_t g_square = 0xF800;

std::string g_cheats; // "<index>:<on|off>:<code>;" per retro_cheat_set since the last reset
int g_cheat_resets = 0;

retro_environment_t g_environment;
retro_video_refresh_t g_video;
retro_audio_sample_batch_t g_audio;
retro_input_poll_t g_input_poll;
retro_input_state_t g_input_state;

void ReadOptions() {
    retro_variable variable{"testcore_color", nullptr};
    if (g_environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value) {
        g_square = std::strcmp(variable.value, "green") == 0  ? 0x07E0
                   : std::strcmp(variable.value, "blue") == 0 ? 0x001F
                                                              : 0xF800;
    }
}

} // namespace

extern "C" {

RETRO_API unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

RETRO_API void retro_set_environment(retro_environment_t callback) {
    g_environment = callback;
    bool no_game = true;
    callback(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    static const retro_variable VARIABLES[] = {
        {"testcore_color", "Square colour; red|green|blue"},
        {nullptr, nullptr},
    };
    callback(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable*>(VARIABLES));
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t callback) {
    g_video = callback;
}
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback) {
    g_audio = callback;
}
RETRO_API void retro_set_input_poll(retro_input_poll_t callback) {
    g_input_poll = callback;
}
RETRO_API void retro_set_input_state(retro_input_state_t callback) {
    g_input_state = callback;
}

RETRO_API void retro_init(void) {
    g_state = State{};
    std::memset(g_save_ram, 0, sizeof(g_save_ram));
}
RETRO_API void retro_deinit(void) {}

RETRO_API void retro_get_system_info(retro_system_info* info) {
    std::memset(info, 0, sizeof(*info));
    info->library_name = "DenTestCore";
    info->library_version = "1";
    info->valid_extensions = "tst|bin";
    info->need_fullpath = false;
}

RETRO_API void retro_get_system_av_info(retro_system_av_info* info) {
    info->geometry.base_width = WIDTH;
    info->geometry.base_height = HEIGHT;
    info->geometry.max_width = WIDTH;
    info->geometry.max_height = HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = 60.0;
    info->timing.sample_rate = SAMPLE_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

RETRO_API void retro_reset(void) {
    const std::uint8_t seed = g_state.seed;
    g_state = State{};
    g_state.seed = seed;
}

RETRO_API bool retro_load_game(const retro_game_info* game) {
    retro_pixel_format format = RETRO_PIXEL_FORMAT_RGB565;
    if (!g_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) {
        return false;
    }
    if (game && game->data && game->size > 0) {
        g_state.seed = *static_cast<const std::uint8_t*>(game->data);
        // A game whose first byte is 'X' is one this core refuses.
        if (g_state.seed == 'X') {
            return false;
        }
    }
    ReadOptions();
    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info*, size_t) {
    return false;
}
RETRO_API void retro_unload_game(void) {}
RETRO_API unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

RETRO_API void retro_run(void) {
    bool updated = false;
    if (g_environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
        ReadOptions();
    }
    g_input_poll();
    const auto down = [](unsigned id) {
        return g_input_state(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;
    };
    g_state.x += (down(RETRO_DEVICE_ID_JOYPAD_RIGHT) ? 1 : 0) - (down(RETRO_DEVICE_ID_JOYPAD_LEFT) ? 1 : 0);
    g_state.y += (down(RETRO_DEVICE_ID_JOYPAD_DOWN) ? 1 : 0) - (down(RETRO_DEVICE_ID_JOYPAD_UP) ? 1 : 0);
    g_state.x = g_state.x < 0 ? 0 : g_state.x > static_cast<int>(WIDTH) - 8 ? WIDTH - 8 : g_state.x;
    g_state.y = g_state.y < 0 ? 0 : g_state.y > static_cast<int>(HEIGHT) - 8 ? HEIGHT - 8 : g_state.y;
    const bool a = down(RETRO_DEVICE_ID_JOYPAD_A);
    if (a && !g_state.a_was_down) {
        ++g_save_ram[0];
    }
    g_state.a_was_down = a;
    ++g_state.frame;

    for (unsigned y = 0; y < HEIGHT; ++y) {
        for (unsigned x = 0; x < WIDTH; ++x) {
            const unsigned band = ((x + g_state.frame + g_state.seed) / 8) & 1;
            g_pixels[y * WIDTH + x] = band ? 0x0010 : 0x0008;
        }
    }
    for (int y = g_state.y; y < g_state.y + 8; ++y) {
        for (int x = g_state.x; x < g_state.x + 8; ++x) {
            g_pixels[y * WIDTH + x] = g_square;
        }
    }
    g_video(g_pixels, WIDTH, HEIGHT, WIDTH * sizeof(std::uint16_t));

    // A quiet 440 Hz beep for a tenth of every second.
    std::int16_t samples[SAMPLES_PER_FRAME * 2];
    const bool beep = g_state.frame % 60 < 6;
    for (unsigned i = 0; i < SAMPLES_PER_FRAME; ++i) {
        const double t = static_cast<double>(g_state.phase++) / SAMPLE_RATE;
        const auto value =
            static_cast<std::int16_t>(beep ? std::sin(t * 440.0 * 6.283185307) * 3000.0 : 0.0);
        samples[i * 2] = samples[i * 2 + 1] = value;
    }
    g_audio(samples, SAMPLES_PER_FRAME);
}

RETRO_API size_t retro_serialize_size(void) {
    return sizeof(State);
}
RETRO_API bool retro_serialize(void* data, size_t size) {
    if (size < sizeof(State)) {
        return false;
    }
    std::memcpy(data, &g_state, sizeof(State));
    return true;
}
RETRO_API bool retro_unserialize(const void* data, size_t size) {
    if (size < sizeof(State)) {
        return false;
    }
    std::memcpy(&g_state, data, sizeof(State));
    return true;
}

RETRO_API void retro_cheat_reset(void) {
    g_cheats.clear();
    ++g_cheat_resets;
}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code) {
    g_cheats += std::to_string(index) + (enabled ? ":on:" : ":off:") + (code ? code : "") + ";";
}

// For the tests: the cheats set since the last reset, and how many resets there were.
RETRO_API const char* testcore_cheats(void) {
    return g_cheats.c_str();
}
RETRO_API int testcore_cheat_resets(void) {
    return g_cheat_resets;
}

RETRO_API void* retro_get_memory_data(unsigned id) {
    return id == RETRO_MEMORY_SAVE_RAM ? g_save_ram : nullptr;
}
RETRO_API size_t retro_get_memory_size(unsigned id) {
    return id == RETRO_MEMORY_SAVE_RAM ? sizeof(g_save_ram) : 0;
}

} // extern "C"
