// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The state behind LibretroCore, shared by the files that implement it.

#pragma once

#include <array>
#include <deque>
#include <optional>

#include "core/core.h"
#include "core/hw_render.h"
#include "libretro.h"

namespace den::player {

struct LibretroCore::Impl {
    void* library = nullptr; // SDL_SharedObject
    void (*retro_init)() = nullptr;
    void (*retro_deinit)() = nullptr;
    unsigned (*retro_api_version)() = nullptr;
    void (*retro_get_system_info)(retro_system_info*) = nullptr;
    void (*retro_get_system_av_info)(retro_system_av_info*) = nullptr;
    void (*retro_set_environment)(retro_environment_t) = nullptr;
    void (*retro_set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*retro_set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*retro_set_input_poll)(retro_input_poll_t) = nullptr;
    void (*retro_set_input_state)(retro_input_state_t) = nullptr;
    void (*retro_set_controller_port_device)(unsigned, unsigned) = nullptr;
    void (*retro_reset)() = nullptr;
    void (*retro_run)() = nullptr;
    std::size_t (*retro_serialize_size)() = nullptr;
    bool (*retro_serialize)(void*, std::size_t) = nullptr;
    bool (*retro_unserialize)(const void*, std::size_t) = nullptr;
    bool (*retro_load_game)(const retro_game_info*) = nullptr;
    void (*retro_unload_game)() = nullptr;
    void* (*retro_get_memory_data)(unsigned) = nullptr;
    std::size_t (*retro_get_memory_size)(unsigned) = nullptr;
    bool (*retro_load_game_special)(unsigned, const retro_game_info*, std::size_t) = nullptr; // optional

    bool initialized = false;
    bool game_loaded = false;
    CoreSystemInfo info;
    std::vector<std::uint8_t> rom_data;
    std::vector<std::vector<std::uint8_t>> special_data; // the games of LoadGameSpecial
    std::vector<std::string> special_paths;
    unsigned save_ram_id = RETRO_MEMORY_SAVE_RAM;
    // The netpacket interface the core set, and the session's side of it.
    bool has_netpacket = false;
    retro_netpacket_callback netpacket{};
    std::string netpacket_version;
    bool netpacket_running = false;
    LibretroCore::PacketSend packet_send;
    std::function<void()> packet_poll;
    std::string core_path;
    std::string rom_path;
    std::string system_dir;
    std::string save_dir;
    retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
    VideoFrame frame;
    std::uint64_t frame_count = 0;
    std::array<std::uint16_t, MAX_PORTS> buttons{};
    std::vector<ButtonName> button_names;
    std::int16_t analog[MAX_PORTS][2][2]{};
    std::int16_t pointer_x = 0;
    std::int16_t pointer_y = 0;
    bool pointer_pressed = false;
    std::array<std::uint16_t, MAX_PORTS> rumble_strong{};
    std::array<std::uint16_t, MAX_PORTS> rumble_weak{};
    AudioSink audio_sink;
    bool audio_muted = false;
    // Values set by the player, and what the core declared (map nodes keep the strings
    // handed to the core in place).
    std::map<std::string, std::string> options;
    std::map<std::string, OptionInfo> option_table;
    std::vector<std::string> option_order;
    retro_core_options_update_display_callback_t update_display = nullptr;
    bool options_changed = false;
    // What the core announced while running; it wins over retro_get_system_av_info.
    std::optional<retro_game_geometry> geometry;
    std::optional<retro_system_timing> timing;
    bool av_changed = false;
    // Hardware rendering: what the core asked for, and whether its context is set up.
    HwRenderHost* hw_host = nullptr;
    retro_hw_render_callback hw{};
    bool hw_accepted = false;
    bool hw_refused = false;
    bool hw_vulkan = false;
    bool hw_ready = false;
    unsigned hw_width = 0;
    unsigned hw_height = 0;
    // The memory map the core declared, copied (its address space names too).
    std::vector<retro_memory_descriptor> memory_descriptors;
    std::deque<std::string> memory_names;
    retro_memory_map memory_map{};
    bool memory_map_set = false;
    bool memory_moved = false;

    /// Sizes the core's framebuffer for the largest picture it announced. It only grows.
    bool ResizeHwTarget(const retro_game_geometry& announced);
    /// The value the core reads for `key`, null when nothing is known about it.
    const std::string* OptionValue(const std::string& key) const;
    void DeclareOption(OptionInfo option);
};

/// Keeps the core's GL context current while the core runs code that may draw.
class HwScope {
public:
    explicit HwScope(LibretroCore::Impl& core) : host_(core.hw_accepted ? core.hw_host : nullptr) {
        if (host_) {
            host_->MakeCurrent();
        }
    }
    ~HwScope() {
        if (host_) {
            host_->Release();
        }
    }
    HwScope(const HwScope&) = delete;
    HwScope& operator=(const HwScope&) = delete;

private:
    HwRenderHost* host_;
};

// libretro callbacks carry no user pointer.
extern LibretroCore::Impl* g_core;

bool RETRO_CALLCONV EnvironmentCallback(unsigned command, void* data);
/// RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE (core_link.cpp).
bool SetNetpacketInterface(const void* data);
void RETRO_CALLCONV VideoCallback(const void* data, unsigned width, unsigned height,
                                  std::size_t pitch);
std::size_t RETRO_CALLCONV AudioBatchCallback(const std::int16_t* data, std::size_t frames);
void RETRO_CALLCONV AudioSampleCallback(std::int16_t left, std::int16_t right);
void RETRO_CALLCONV InputPollCallback();
std::int16_t RETRO_CALLCONV InputStateCallback(unsigned port, unsigned device, unsigned index,
                                               unsigned id);

} // namespace den::player
