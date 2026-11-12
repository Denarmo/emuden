// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The video, audio and input callbacks of a core.

#include <algorithm>
#include <cstring>

#include "core/core_impl.h"

namespace den::player {

namespace {

std::uint8_t Expand5(std::uint32_t c) {
    return static_cast<std::uint8_t>((c << 3) | (c >> 2));
}

std::uint8_t Expand6(std::uint32_t c) {
    return static_cast<std::uint8_t>((c << 2) | (c >> 4));
}

} // namespace

void RETRO_CALLCONV VideoCallback(const void* data, unsigned width, unsigned height,
                                  std::size_t pitch) {
    if (!data || !g_core) {
        return; // a duplicated frame: keep the previous picture
    }
    VideoFrame& frame = g_core->frame;
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        if (!g_core->hw_ready || width == 0 || height == 0) {
            return;
        }
        frame.width = std::min(width, g_core->hw_width);
        frame.height = std::min(height, g_core->hw_height);
        frame.rgba.clear();
        frame.hardware = true;
        ++frame.serial;
        g_core->hw_host->Present(frame.width, frame.height);
        return;
    }
    frame.hardware = false;
    frame.width = width;
    frame.height = height;
    ++frame.serial;
    frame.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const auto* source = static_cast<const std::uint8_t*>(data);
    const retro_pixel_format format = g_core->pixel_format;
    for (unsigned y = 0; y < height; ++y) {
        const std::uint8_t* row = source + static_cast<std::size_t>(y) * pitch;
        std::uint8_t* out = frame.rgba.data() + static_cast<std::size_t>(y) * width * 4;
        if (format == RETRO_PIXEL_FORMAT_XRGB8888) {
            for (unsigned x = 0; x < width; ++x, row += 4, out += 4) {
                out[0] = row[2];
                out[1] = row[1];
                out[2] = row[0];
                out[3] = 255;
            }
            continue;
        }
        for (unsigned x = 0; x < width; ++x, out += 4) {
            std::uint16_t value;
            std::memcpy(&value, row + x * 2, 2);
            if (format == RETRO_PIXEL_FORMAT_RGB565) {
                out[0] = Expand5((value >> 11) & 0x1F);
                out[1] = Expand6((value >> 5) & 0x3F);
                out[2] = Expand5(value & 0x1F);
            } else {
                out[0] = Expand5((value >> 10) & 0x1F);
                out[1] = Expand5((value >> 5) & 0x1F);
                out[2] = Expand5(value & 0x1F);
            }
            out[3] = 255;
        }
    }
}

std::size_t RETRO_CALLCONV AudioBatchCallback(const std::int16_t* data, std::size_t frames) {
    if (g_core && g_core->audio_sink && !g_core->audio_muted && data) {
        g_core->audio_sink(std::span<const std::int16_t>(data, frames * 2));
    }
    return frames;
}

void RETRO_CALLCONV AudioSampleCallback(std::int16_t left, std::int16_t right) {
    const std::int16_t samples[2] = {left, right};
    AudioBatchCallback(samples, 1);
}

void RETRO_CALLCONV InputPollCallback() {}

std::int16_t RETRO_CALLCONV InputStateCallback(unsigned port, unsigned device, unsigned index,
                                               unsigned id) {
    if (!g_core || port >= MAX_PORTS) {
        return 0;
    }
    switch (device & RETRO_DEVICE_MASK) {
    case RETRO_DEVICE_JOYPAD:
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
            return static_cast<std::int16_t>(g_core->buttons[port]);
        }
        return id < 16 ? static_cast<std::int16_t>((g_core->buttons[port] >> id) & 1) : 0;
    case RETRO_DEVICE_ANALOG:
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
            // How far a button is pressed: all the way or not at all.
            return id < 16 && ((g_core->buttons[port] >> id) & 1) != 0 ? 0x7fff : 0;
        }
        return index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT && id <= 1
                   ? g_core->analog[port][index][id]
                   : 0;
    case RETRO_DEVICE_POINTER:
        if (index != 0 || port != 0) {
            return 0; // one touch at a time
        }
        switch (id) {
        case RETRO_DEVICE_ID_POINTER_X:
            return g_core->pointer_x;
        case RETRO_DEVICE_ID_POINTER_Y:
            return g_core->pointer_y;
        case RETRO_DEVICE_ID_POINTER_PRESSED:
        case RETRO_DEVICE_ID_POINTER_COUNT:
            return g_core->pointer_pressed ? 1 : 0;
        default:
            return 0;
        }
    default:
        return 0;
    }
}

} // namespace den::player
