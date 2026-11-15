// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A libretro core loaded from a shared library: software-rendered cores and cores that draw
// with OpenGL (hw_render.h), joypad and analog input, a touch pointer, save states, save RAM
// and core options.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct retro_memory_map;

namespace den::player {

inline constexpr unsigned MAX_PORTS = 4;

/// RetroPad buttons, as bits of the mask given to SetButtons (RETRO_DEVICE_ID_JOYPAD_*).
enum RetroButton : std::uint16_t {
    RETRO_B = 1 << 0,
    RETRO_Y = 1 << 1,
    RETRO_SELECT = 1 << 2,
    RETRO_START = 1 << 3,
    RETRO_UP = 1 << 4,
    RETRO_DOWN = 1 << 5,
    RETRO_LEFT = 1 << 6,
    RETRO_RIGHT = 1 << 7,
    RETRO_A = 1 << 8,
    RETRO_X = 1 << 9,
    RETRO_L = 1 << 10,
    RETRO_R = 1 << 11,
    RETRO_L2 = 1 << 12,
    RETRO_R2 = 1 << 13,
    RETRO_L3 = 1 << 14,
    RETRO_R3 = 1 << 15,
};

/// What a RetroPad button of a port does in the game (SET_INPUT_DESCRIPTORS). `button` is
/// the bit number of a RetroButton.
struct ButtonName {
    unsigned port = 0;
    unsigned button = 0;
    std::string name;
};

/// The picture of the last frame: RGBA bytes, row 0 at the top, no padding.
struct VideoFrame {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> rgba;
    /// The core drew the frame with the GPU: `rgba` is empty and the picture is the one the
    /// HwRenderHost was last told to present.
    bool hardware = false;
    /// Grows whenever the picture changes.
    std::uint64_t serial = 0;
};

struct AvInfo {
    double fps = 60.0;
    double sample_rate = 44100.0;
    unsigned base_width = 0;
    unsigned base_height = 0;
    unsigned max_width = 0;
    unsigned max_height = 0;
    float aspect_ratio = 0.0f;
};

struct CoreSystemInfo {
    std::string name;
    std::string version;
    /// Content extensions the core accepts, lower case without dots.
    std::vector<std::string> extensions;
    bool need_fullpath = false;
    /// The core opens archives itself (retro_system_info::block_extract).
    bool block_extract = false;
};

struct OptionChoice {
    std::string value;
    std::string label;
};

/// A setting the core declared.
struct OptionInfo {
    std::string key;
    std::string label;
    std::string description;
    std::string category;
    std::vector<OptionChoice> values; // never empty
    std::string default_value;
    std::string value; // what the core reads now
    bool visible = true;
};

/// Interleaved stereo samples of one audio batch.
using AudioSink = std::function<void(std::span<const std::int16_t> samples)>;

class HwRenderHost;

class LibretroCore {
public:
    struct LoadParams {
        std::filesystem::path core;
        std::filesystem::path system_dir;
        std::filesystem::path save_dir;
        /// Option values the core reads from its very first call on.
        std::map<std::string, std::string> options;
        /// Lets the core draw with OpenGL; without it only software cores start. It must
        /// outlive the core.
        HwRenderHost* hw_host = nullptr;
    };

    /// Loads a core. libretro callbacks carry no user pointer, so only one core can be loaded
    /// in the process at a time; a second Load fails until the first is destroyed.
    static std::unique_ptr<LibretroCore> Load(const LoadParams& params, std::string* error);
    ~LibretroCore();
    LibretroCore(const LibretroCore&) = delete;
    LibretroCore& operator=(const LibretroCore&) = delete;

    const CoreSystemInfo& Info() const;

    /// `content`, when not empty, is what a core that takes its game in memory is given
    /// rather than the bytes of the file.
    bool LoadGame(const std::filesystem::path& rom_path, std::string* error,
                  std::vector<std::uint8_t> content = {});
    bool GameLoaded() const;
    void Reset();

    const std::vector<ButtonName>& ButtonNames() const;
    void SetButtons(unsigned port, std::uint16_t buttons);
    /// stick 0 = left, 1 = right; x and y from -0x7fff to 0x7fff, +y down.
    void SetAnalog(unsigned port, unsigned stick, std::int16_t x, std::int16_t y);
    /// Touch screen (RETRO_DEVICE_POINTER): x and y from -0x7fff to 0x7fff over the whole
    /// picture. The position only counts while `pressed`.
    void SetPointer(std::int16_t x, std::int16_t y, bool pressed);
    /// What the core last asked of a port's rumble motors, 0 to 0xffff each.
    void Rumble(unsigned port, std::uint16_t* strong, std::uint16_t* weak) const;

    /// Emulates one frame: updates Frame() and calls the audio sink on this thread.
    void RunFrame();
    const VideoFrame& Frame() const;
    bool HardwareRendered() const;
    /// The core asked for a kind of GPU rendering that could not be given.
    bool HardwareRefused() const;
    /// The core's picture has row 0 at the bottom of its framebuffer.
    bool BottomLeftOrigin() const;
    /// A copy of the picture last shown, read back from the GPU when the core draws there.
    bool ReadPicture(unsigned* width, unsigned* height, std::vector<std::uint8_t>* rgba) const;
    std::uint64_t FrameCount() const;
    AvInfo GetAvInfo() const;
    /// True once after the core announced a new picture size, shape or timing.
    bool TakeAvChanged();

    void SetAudioSink(AudioSink sink);
    /// While muted the sink is not called (fast-forward, rewind).
    void SetAudioMuted(bool muted);

    std::size_t StateSize() const;
    bool SaveState(std::span<std::uint8_t> out) const;
    bool LoadState(std::span<const std::uint8_t> state);

    /// Battery-backed save RAM, empty when the game has none. Valid until the game unloads.
    std::span<std::uint8_t> SaveRam() const;
    std::span<std::uint8_t> Memory(unsigned id) const;
    /// The core's memory map (SET_MEMORY_MAPS), null when it declared none.
    const retro_memory_map* MemoryMap() const;
    /// True once after the core declared a memory map.
    bool TakeMemoryMoved();

    /// A value set here wins over the core's declared default; the core is told on its next
    /// frame. A value the core does not allow is ignored in favour of the default.
    void SetOption(const std::string& key, const std::string& value);
    std::string GetOption(const std::string& key) const;
    /// Every option the core declared so far, in its order. Ask after LoadGame.
    std::vector<OptionInfo> Options() const;

    struct Impl;

private:
    LibretroCore();
    std::unique_ptr<Impl> impl_;
};

} // namespace den::player
