// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>

#include <SDL3/SDL_loadso.h>

#include "core/core_impl.h"
#include "den/base/files.h"
#include "den/platform/pal.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

std::vector<std::string> SplitExtensions(const char* text) {
    std::vector<std::string> out;
    std::string current;
    for (const char* at = text ? text : "";; ++at) {
        if (*at == '|' || *at == '\0') {
            if (!current.empty()) {
                out.push_back(ToLower(current));
            }
            current.clear();
            if (*at == '\0') {
                break;
            }
        } else {
            current.push_back(*at);
        }
    }
    return out;
}

} // namespace

LibretroCore::LibretroCore() : impl_{std::make_unique<Impl>()} {}

std::unique_ptr<LibretroCore> LibretroCore::Load(const LoadParams& params, std::string* error) {
    const auto fail = [&](std::string message) {
        if (error) {
            *error = std::move(message);
        }
        return nullptr;
    };
    if (g_core) {
        return fail("Another game is still running.");
    }
    std::unique_ptr<LibretroCore> core(new LibretroCore());
    Impl& c = *core->impl_;
    c.core_path = PathToUtf8(params.core);
    c.library = SDL_LoadObject(c.core_path.c_str());
    if (!c.library) {
        return fail("The core could not be loaded: " + c.core_path);
    }
    const char* missing = nullptr;
    const auto load = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
            SDL_LoadFunction(static_cast<SDL_SharedObject*>(c.library), name));
        if (!function && !missing) {
            missing = name;
        }
    };
#define DEN_LOAD(name) load(c.name, #name)
    DEN_LOAD(retro_init);
    DEN_LOAD(retro_deinit);
    DEN_LOAD(retro_api_version);
    DEN_LOAD(retro_get_system_info);
    DEN_LOAD(retro_get_system_av_info);
    DEN_LOAD(retro_set_environment);
    DEN_LOAD(retro_set_video_refresh);
    DEN_LOAD(retro_set_audio_sample);
    DEN_LOAD(retro_set_audio_sample_batch);
    DEN_LOAD(retro_set_input_poll);
    DEN_LOAD(retro_set_input_state);
    DEN_LOAD(retro_set_controller_port_device);
    DEN_LOAD(retro_reset);
    DEN_LOAD(retro_run);
    DEN_LOAD(retro_serialize_size);
    DEN_LOAD(retro_serialize);
    DEN_LOAD(retro_unserialize);
    DEN_LOAD(retro_load_game);
    DEN_LOAD(retro_unload_game);
    DEN_LOAD(retro_get_memory_data);
    DEN_LOAD(retro_get_memory_size);
#undef DEN_LOAD
    if (missing) {
        return fail(std::string("This file is not a libretro core (no ") + missing + ").");
    }
    load(c.retro_load_game_special, "retro_load_game_special");
    // A core built to be loaded by Java finds the Java VM through JNI_OnLoad, which only
    // System.loadLibrary calls; Play! asks for it with its own setter.
    if (void* vm = pal::JavaVm()) {
        auto* object = static_cast<SDL_SharedObject*>(c.library);
        if (auto on_load = reinterpret_cast<int (*)(void*, void*)>(SDL_LoadFunction(object, "JNI_OnLoad"))) {
            on_load(vm, nullptr);
        }
        if (auto set_vm = reinterpret_cast<void (*)(void*)>(
                SDL_LoadFunction(object, "_ZN9Framework7CJavaVM9SetJavaVMEP7_JavaVM"))) {
            set_vm(vm);
        }
    }
    if (c.retro_api_version() != RETRO_API_VERSION) {
        return fail("The core uses an unsupported libretro version.");
    }
    std::error_code ec;
    c.system_dir =
        PathToUtf8(params.system_dir.empty() ? fs::temp_directory_path(ec) : params.system_dir);
    c.save_dir = params.save_dir.empty() ? c.system_dir : PathToUtf8(params.save_dir);
    c.options = params.options;
    c.hw_host = params.hw_host;

    g_core = &c;
    c.retro_set_environment(EnvironmentCallback);
    c.retro_init();
    c.initialized = true;
    c.retro_set_video_refresh(VideoCallback);
    c.retro_set_audio_sample(AudioSampleCallback);
    c.retro_set_audio_sample_batch(AudioBatchCallback);
    c.retro_set_input_poll(InputPollCallback);
    c.retro_set_input_state(InputStateCallback);

    retro_system_info system{};
    c.retro_get_system_info(&system);
    c.info.name = system.library_name ? system.library_name : "";
    c.info.version = system.library_version ? system.library_version : "";
    c.info.extensions = SplitExtensions(system.valid_extensions);
    c.info.need_fullpath = system.need_fullpath;
    c.info.block_extract = system.block_extract;
    return core;
}

LibretroCore::~LibretroCore() {
    Impl& c = *impl_;
    {
        HwScope scope(c);
        if (c.hw_ready && c.hw.context_destroy) {
            c.hw.context_destroy();
        }
        NetpacketStop();
        if (c.game_loaded) {
            c.retro_unload_game();
        }
        if (c.initialized) {
            c.retro_deinit();
        }
    }
    // After the core is done: it may free what it drew with while it unloads.
    if (c.hw_accepted) {
        c.hw_host->Shutdown();
    }
    if (g_core == &c) {
        g_core = nullptr;
    }
    if (c.library) {
        SDL_UnloadObject(static_cast<SDL_SharedObject*>(c.library));
    }
}

const CoreSystemInfo& LibretroCore::Info() const {
    return impl_->info;
}

bool LibretroCore::LoadGame(const fs::path& rom_path, std::string* error,
                            std::vector<std::uint8_t> content) {
    Impl& c = *impl_;
    const auto fail = [&](std::string message) {
        if (error) {
            *error = std::move(message);
        }
        return false;
    };
    if (c.game_loaded) {
        return fail("A game is already loaded.");
    }
    retro_game_info game{};
    if (!rom_path.empty()) {
        c.rom_path = PathToUtf8(rom_path);
        game.path = c.rom_path.c_str();
        if (!c.info.need_fullpath) {
            if (!content.empty()) {
                c.rom_data = std::move(content);
            } else {
                auto data = ReadBinaryFile(rom_path);
                if (!data) {
                    return fail("The game file could not be read: " + c.rom_path);
                }
                c.rom_data = std::move(*data);
            }
            game.data = c.rom_data.data();
            game.size = c.rom_data.size();
        }
    }
    if (!c.retro_load_game(rom_path.empty() ? nullptr : &game)) {
        return fail(c.hw_refused ? "The core needs a kind of graphics this device does not give it."
                                 : "The core could not start this game.");
    }
    c.game_loaded = true;
    if (c.hw_accepted) {
        // The core draws with the GPU: it gets its framebuffer, then is told to set itself up.
        retro_system_av_info av{};
        c.retro_get_system_av_info(&av);
        HwScope scope(c);
        if (!c.ResizeHwTarget(av.geometry)) {
            return fail("The graphics for the core could not be set up.");
        }
        c.hw_ready = true;
        if (c.hw.context_reset) {
            c.hw.context_reset();
        }
    }
    for (unsigned port = 0; port < MAX_PORTS; ++port) {
        c.retro_set_controller_port_device(port, RETRO_DEVICE_JOYPAD);
    }
    return true;
}

bool LibretroCore::GameLoaded() const {
    return impl_->game_loaded;
}

void LibretroCore::Reset() {
    if (impl_->game_loaded) {
        HwScope scope(*impl_);
        impl_->retro_reset();
    }
}

const std::vector<ButtonName>& LibretroCore::ButtonNames() const {
    return impl_->button_names;
}

void LibretroCore::SetButtons(unsigned port, std::uint16_t buttons) {
    if (port < MAX_PORTS) {
        impl_->buttons[port] = buttons;
    }
}

void LibretroCore::SetAnalog(unsigned port, unsigned stick, std::int16_t x, std::int16_t y) {
    if (port < MAX_PORTS && stick < 2) {
        impl_->analog[port][stick][0] = x;
        impl_->analog[port][stick][1] = y;
    }
}

void LibretroCore::SetPointer(std::int16_t x, std::int16_t y, bool pressed) {
    impl_->pointer_x = x;
    impl_->pointer_y = y;
    impl_->pointer_pressed = pressed;
}

void LibretroCore::Rumble(unsigned port, std::uint16_t* strong, std::uint16_t* weak) const {
    *strong = port < MAX_PORTS ? impl_->rumble_strong[port] : 0;
    *weak = port < MAX_PORTS ? impl_->rumble_weak[port] : 0;
}

void LibretroCore::RunFrame() {
    if (impl_->game_loaded) {
        HwScope scope(*impl_);
        impl_->retro_run();
        ++impl_->frame_count;
    }
}

const VideoFrame& LibretroCore::Frame() const {
    return impl_->frame;
}

bool LibretroCore::HardwareRendered() const {
    return impl_->hw_ready;
}

bool LibretroCore::HardwareRefused() const {
    return impl_->hw_refused;
}

bool LibretroCore::BottomLeftOrigin() const {
    return impl_->hw_ready && !impl_->hw_vulkan && impl_->hw.bottom_left_origin;
}

bool LibretroCore::ReadPicture(unsigned* width, unsigned* height,
                               std::vector<std::uint8_t>* rgba) const {
    const VideoFrame& frame = impl_->frame;
    if (frame.hardware) {
        return impl_->hw_ready && impl_->hw_host->ReadPicture(width, height, rgba);
    }
    *width = frame.width;
    *height = frame.height;
    *rgba = frame.rgba;
    return frame.width != 0 && frame.height != 0;
}

std::uint64_t LibretroCore::FrameCount() const {
    return impl_->frame_count;
}

AvInfo LibretroCore::GetAvInfo() const {
    retro_system_av_info av{};
    impl_->retro_get_system_av_info(&av);
    if (impl_->geometry) {
        av.geometry.base_width = impl_->geometry->base_width;
        av.geometry.base_height = impl_->geometry->base_height;
        av.geometry.aspect_ratio = impl_->geometry->aspect_ratio;
        av.geometry.max_width = std::max(av.geometry.max_width, impl_->geometry->max_width);
        av.geometry.max_height = std::max(av.geometry.max_height, impl_->geometry->max_height);
    }
    if (impl_->timing) {
        av.timing = *impl_->timing;
    }
    AvInfo out;
    out.fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
    out.sample_rate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100.0;
    out.base_width = av.geometry.base_width;
    out.base_height = av.geometry.base_height;
    out.max_width = av.geometry.max_width;
    out.max_height = av.geometry.max_height;
    out.aspect_ratio = av.geometry.aspect_ratio > 0 || av.geometry.base_height == 0
                           ? av.geometry.aspect_ratio
                           : static_cast<float>(av.geometry.base_width) /
                                 static_cast<float>(av.geometry.base_height);
    return out;
}

bool LibretroCore::TakeAvChanged() {
    return std::exchange(impl_->av_changed, false);
}

void LibretroCore::SetAudioSink(AudioSink sink) {
    impl_->audio_sink = std::move(sink);
}

void LibretroCore::SetAudioMuted(bool muted) {
    impl_->audio_muted = muted;
}

std::size_t LibretroCore::StateSize() const {
    if (!impl_->game_loaded) {
        return 0;
    }
    // A GPU core may finish drawing its frame before it can measure its state.
    HwScope scope(*impl_);
    return impl_->retro_serialize_size();
}

bool LibretroCore::SaveState(std::span<std::uint8_t> out) const {
    if (!impl_->game_loaded) {
        return false;
    }
    HwScope scope(*impl_);
    return impl_->retro_serialize(out.data(), out.size());
}

bool LibretroCore::LoadState(std::span<const std::uint8_t> state) {
    if (!impl_->game_loaded) {
        return false;
    }
    HwScope scope(*impl_);
    return impl_->retro_unserialize(state.data(), state.size());
}

std::span<std::uint8_t> LibretroCore::SaveRam() const {
    return Memory(impl_->save_ram_id);
}

std::span<std::uint8_t> LibretroCore::Memory(unsigned id) const {
    if (!impl_->game_loaded) {
        return {};
    }
    auto* data = static_cast<std::uint8_t*>(impl_->retro_get_memory_data(id));
    const std::size_t size = impl_->retro_get_memory_size(id);
    return data && size ? std::span<std::uint8_t>(data, size) : std::span<std::uint8_t>();
}

const retro_memory_map* LibretroCore::MemoryMap() const {
    return impl_->game_loaded && impl_->memory_map_set ? &impl_->memory_map : nullptr;
}

bool LibretroCore::TakeMemoryMoved() {
    return std::exchange(impl_->memory_moved, false);
}

void LibretroCore::SetOption(const std::string& key, const std::string& value) {
    const auto set = impl_->options.find(key);
    if (set != impl_->options.end() && set->second == value) {
        return;
    }
    impl_->options[key] = value;
    impl_->options_changed = true;
}

std::string LibretroCore::GetOption(const std::string& key) const {
    const std::string* value = impl_->OptionValue(key);
    return value ? *value : std::string();
}

std::vector<OptionInfo> LibretroCore::Options() const {
    // The core hides and shows options to match the current values.
    if (impl_->update_display && impl_->initialized) {
        impl_->update_display();
    }
    std::vector<OptionInfo> out;
    out.reserve(impl_->option_order.size());
    for (const std::string& key : impl_->option_order) {
        OptionInfo option = impl_->option_table.at(key);
        option.value = *impl_->OptionValue(key);
        out.push_back(std::move(option));
    }
    return out;
}

} // namespace den::player
