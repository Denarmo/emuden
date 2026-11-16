// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the ROM player: one libretro core with one game, running inside the app's
// window. libretro.h (from upstream libretro) is in third_party/libretro. The same facade
// drives a game in an engine app (CreateEngineSession) through the Emulator Control
// Protocol, so the player screen and its menus serve both.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/engine/host.h"
#include "den/gfx/canvas.h"
#include "den/input/input.h"
#include "den/player/bfi.h"
#include "den/player/screens.h"

namespace den::store {
class Settings;
}
namespace den::net {
class Party;
}

struct retro_memory_map;

namespace den::player {

/// A game played with another device (den/net/party.h): how this session takes part.
struct TogetherRequest {
    /// The connection; null when the game is played on this device alone.
    std::shared_ptr<net::Party> party;
    /// Non-zero: the content is loaded twice through this subsystem of the core (TGB Dual's
    /// two linked Game Boys, RETRO_GAME_TYPE_GAMEBOY_LINK_2P).
    unsigned subsystem = 0;
    /// The memory this device's battery save is kept in (0: the usual save RAM), and the
    /// memory the other device's save is put in (0: none).
    unsigned save_memory = 0;
    unsigned peer_save_memory = 0;
    /// False: this device's battery save is not written (it plays the host's game).
    bool write_saves = true;
    /// Option values for this session only; never saved.
    std::vector<std::pair<std::string, std::string>> options;
};

struct OpenRequest {
    std::filesystem::path core;       // plugins::CoreInfo::library
    std::filesystem::path content;    // the ROM
    std::filesystem::path system_dir; // BIOS files
    std::string game_id;              // library entry id; names the save and state files
    std::string system;               // system id
    /// Option values from the plugin manifest, applied before the user's own.
    std::vector<std::pair<std::string, std::string>> option_hints;
    /// Continue from where the game was left (the state written by Close), when there is one
    /// and the "player.resume" setting allows it.
    bool resume = true;
    /// An engine app plays the game instead of a libretro core (`core` is then unused).
    std::optional<engine::Install> engine;
    /// Its own setup (the player's BIOS and game folders), sent before the game.
    std::optional<engine::EngineSetup> engine_setup;
    std::string title;
    bool fullscreen = true; // the engine's own window
    /// RetroAchievements hardcore: no state is loaded (the resume state neither) and no rewind.
    bool hardcore = false;
    TogetherRequest together;
};

enum class State { Closed, Running, Paused, Failed };

/// What the frame hook (Session::SetFrameHook) hears of.
enum class FrameEvent {
    Frame,       // the core emulated a frame forwards
    Reset,       // the game started again
    StateLoaded, // a state (a slot, the resume state, a rewind step) replaced the game's memory
    MemoryMoved, // the core announced a new memory map
};

/// Counters of the sound path, since the game opened (or ResetAudioStats).
struct AudioStats {
    double sample_rate = 0.0;          // the core's
    std::uint64_t frames_queued = 0;   // sample frames the core handed over
    std::uint64_t frames_dropped = 0;  // thrown away: the queue was too long, or there is no device
    int overruns = 0;                  // times the queue was too long and was dropped
    int underruns = 0;                 // times the device asked for more than was queued (a gap)
    std::uint64_t frames_starved = 0;  // sample frames the device missed in those gaps
    int refills = 0;                   // times the queue was started again after running dry
    double target_ms = 0.0;            // what the queue is kept at; raised when the game stalls
    double queue_ms = 0.0;             // queued now
    double queue_avg_ms = 0.0, queue_min_ms = 0.0, queue_max_ms = 0.0; // sampled once per frame
    double ratio = 1.0;                // playback rate set on the resampler (1.0 = the core's)
    double ratio_min = 1.0, ratio_max = 1.0;
};

/// What a finished capture of the device's output held.
struct AudioCapture {
    int sample_rate = 0;
    int channels = 0;
    std::uint64_t frames = 0;
    double peak = 0.0;            // 0..1
    double rms = 0.0;             // 0..1
    double longest_silence_ms = 0.0; // the longest run of zero samples
    double silence_ms = 0.0;         // all runs of zero samples over 5 ms, added up
    int silences = 0;                // how many such runs
};

/// What the screens show about a session. A copy; cheap to take every frame.
struct Snapshot {
    State state = State::Closed;
    std::string error; // user-facing, when Failed
    std::string core_name;
    std::string game_id;
    int frame_width = 0, frame_height = 0;
    float aspect = 4.0f / 3.0f;
    double core_fps = 60.0;     // what the core asks for
    double measured_fps = 0.0;  // what it gets
    std::uint64_t frames = 0;
    bool fast_forward = false;
    bool rewinding = false;
    bool can_save_states = false;
    bool can_rewind = false;
    bool hardware_rendered = false; // the core draws with the GPU
    bool resumed = false;           // this session continued from the state Close wrote
    double audio_queued_ms = 0.0;   // sound waiting to be played
    int audio_underruns = 0;        // times the sound ran dry (audible gaps) since Open
    std::string picture;            // the picture preset in use ("plain", "crt", ...), never "auto"
    AudioStats audio;
    /// The buttons the game was last given per port, as RetroPad bits (B, Y, Select, Start,
    /// Up, Down, Left, Right, A, X, L, R, L2, R2, L3, R3 from bit 0).
    std::array<std::uint16_t, 4> game_buttons{};
    std::size_t rewind_states = 0;  // states the rewind history holds
    bool audio_capturing = false;   // a capture asked for with StartAudioCapture still runs
    /// An engine app outside the app runs the game; the rows below say what it can do.
    bool external = false;
    bool can_pause = true;
    bool can_fast_forward = true;
    bool can_reset = true;
    bool can_options = true;
    bool can_volume = true;
    bool can_picture = true; // picture presets, screen size and button remapping (EmuDEN draws it)
    bool hardcore = false;   // states cannot be loaded and there is no rewind
    /// Played with another device: no fast-forward, rewind or loaded states.
    bool together = false;
    /// The game waits for the other device's buttons or state.
    bool waiting_for_peer = false;
    /// Black frame insertion, while it is asked for (SetBfi): the plan for the display, or
    /// why there is none.
    BfiPlan bfi;
    bool bfi_black = false;     // the refresh being drawn now is a black one
    bool bfi_steady = true;     // the frames keep up with the display
    double bfi_display_hz = 0;  // the refreshes the frames actually get per second
};

/// What the screen asks of black frame insertion (den/player/bfi.h).
struct BfiRequest {
    bool on = false;
    /// Planned, but no black frames for now: a menu or the start of the game is over it.
    bool hold = false;
    float refresh_hz = 0; // the display the game is on
    BfiStrength strength = BfiStrength::Full;
};

struct CoreOption {
    std::string key;
    std::string label;
    std::string value;
    std::string default_value;
    std::vector<std::string> values;
    bool for_game = false; // the value is this game's own, not the console's
};

/// Whose value SetOption writes: every game of the console (the core), or this game only.
enum class OptionScope { Console, Game };

/// A slider of a picture preset.
struct PictureParam {
    std::string id;
    std::string name;
    float min = 0, max = 1, step = 0.05f;
    float fallback = 0; // the preset's own value
    float value = 0;    // the value in use (Session::PictureParams)
    /// Names for the values of a slider of whole steps (min, min + step, ...); empty for
    /// a plain number.
    std::vector<std::string> labels;
};

/// A named set of a preset's slider values ("Lively colours"); the sliders it leaves out
/// take the preset's own values. The first look of a preset is the preset as it comes.
struct PictureLook {
    std::string name;
    std::vector<std::pair<std::string, float>> values;
};

/// A picture preset: how the game's picture is drawn (a chain of GLSL passes from
/// <assets>/shaders, see den/gfx/effect_chain.h). "plain" is always there and comes first.
struct PicturePreset {
    std::string id;    // "plain", "crt", "dotmatrix", ...
    std::string name;  // "Plain", "CRT", "Dot matrix (green)"
    std::string group; // "Clean", "Television", "Handheld"
    std::vector<PictureParam> params;
    std::vector<PictureLook> looks; // none, or two and more
};
/// The app's presets, then the imported ones (group "Imported").
const std::vector<PicturePreset>& PicturePresets();
/// Where imported RetroArch presets (.slangp, .glslp) are kept, in folders of their own.
std::filesystem::path ImportedShaderDir();
/// Reads the imported presets again, after an import or a removal.
void RefreshImportedPresets();
/// The id that means "the console's own default" wherever a preset id is taken.
constexpr const char* PICTURE_AUTO = "auto";
/// The preset Auto stands for on a console ("gb" gives "dotmatrix"); "plain" when none.
std::string AutoPicture(const std::string& system);

constexpr int STATE_SLOTS = 10;

/// Main thread only, with the app's GL context current. The app's player screen drives it:
/// Open, then per frame SetPad, Advance and Draw, and Close when the screen leaves.
///
/// Settings read at Open (all optional): player.resume (true), player.rewind (true),
/// player.fast_forward_speed (3), player.integer_scale (false), player.smooth (true),
/// player.pad_by_label (true), player.volume (1.0). A user's core option values are kept
/// under core_options.<core>.<key> for the console and game_options.<system>.<game>.<key>
/// for one game; the game's value wins. Remapped buttons are under
/// player.buttons.<system>.<button> (den/player/buttons.h).
class Session {
public:
    virtual ~Session() = default;

    /// Loads the core and the game and starts running. On failure the snapshot's state is
    /// Failed and the Status carries the same user-facing text.
    virtual Status Open(const OpenRequest& request) = 0;
    /// Writes the battery save and unloads the game and the core.
    virtual void Close() = 0;

    virtual void Pause() = 0;
    virtual void Resume() = 0;
    /// The pad for a port (0 = player one), by position. Call before Advance.
    virtual void SetPad(int port, const input::PadState& pad) = 0;
    /// A finger or the mouse on the picture (a touch screen console), in the units of the
    /// canvas last drawn to. Outside the picture it counts as lifted.
    virtual void SetPointer(float x, float y, bool pressed) = 0;
    /// Runs as many core frames as `seconds` of real time ask for (none while paused) and
    /// queues their audio.
    virtual void Advance(double seconds) = 0;
    /// Draws the latest frame into `area` of the frame the UI is drawing (letterboxed to the
    /// core's aspect ratio). The session may call canvas.Flush(), use its own GL, and must
    /// leave the canvas usable (canvas.RestoreState()).
    virtual void Draw(gfx::Canvas& canvas, const gfx::Rect& area) = 0;

    /// The console's screens when its picture holds two (den/player/screens.h); empty when
    /// the picture is one screen, which Draw and SetPointer are for. It can change while the
    /// game runs (a core option that lays the screens out differently).
    virtual std::vector<GameScreen> Screens() const {
        return {};
    }
    /// Black frame insertion from the next Advance. While a plan is active, Advance emulates
    /// one game frame per cycle of refreshes and Snapshot::bfi_black tells the refreshes the
    /// screen draws black (it draws them: Draw and DrawScreen show the game as always). Off
    /// while fast-forwarding, rewinding and playing with another device.
    virtual void SetBfi(const BfiRequest& /*request*/) {}
    /// The next preview keeps the latest picture as the frame of the previews from then on.
    virtual void CapturePreviewFrame() {}
    /// Draws one screen of Screens() into exactly `target` of the frame being drawn, through
    /// the picture preset. The canvas may be the second display's; the rules of Draw apply.
    virtual void DrawScreen(gfx::Canvas& /*canvas*/, int /*screen*/, const gfx::Rect& /*target*/) {}
    /// A finger on a screen of Screens(): `u` and `v` are 0..1 across it. Only a screen with
    /// `touch` reaches the game; anything else counts as lifted.
    virtual void SetScreenPointer(int /*screen*/, float /*u*/, float /*v*/, bool /*pressed*/) {}

    /// Forces the picture's width / height in Draw (the screen size choice); 0 returns to the
    /// core's own aspect ratio.
    virtual void SetAspectOverride(float /*aspect*/) {}

    virtual Status SaveState(int slot) = 0;
    virtual Status LoadState(int slot) = 0;
    virtual bool HasState(int slot) const = 0;
    /// The PNG taken when the slot was saved; an empty path when the slot has none.
    virtual std::filesystem::path StateThumbnail(int slot) const = 0;
    virtual void SetFastForward(bool on) = 0;
    /// While on, Advance steps backwards through the rewind buffer.
    virtual void SetRewind(bool on) = 0;
    virtual void Reset() = 0;
    /// Writes the battery save now when it changed (Close and a timer do too).
    virtual void FlushSaves() = 0;
    /// Writes the state the next start resumes from, as Close does, and goes on: for a
    /// moment after which the process may be ended without a word (the app is covered).
    virtual void SaveResumeState() {}

    virtual std::vector<CoreOption> Options() const = 0;
    virtual void SetOption(const std::string& key, const std::string& value) = 0;
    /// As SetOption, saved for the console or for this game only. Saving for the console
    /// drops the game's own value.
    virtual void SetOption(const std::string& key, const std::string& value, OptionScope scope) {
        (void)scope;
        SetOption(key, value);
    }
    /// Forgets this game's own option values; the console's apply again.
    virtual void ClearGameOptions() {}
    /// Reads the remapped buttons from the settings again (after the remap page changed them).
    virtual void ReloadButtons() {}
    /// The RetroPad button (bit number) the core names as its microphone, such as melonDS's
    /// "Make microphone noise"; -1 when it has none.
    virtual int MicrophoneButton() const {
        return -1;
    }
    /// RetroPad buttons of port 1 held down besides the pad's, for a shortcut (0 = none).
    virtual void SetHeldButtons(std::uint16_t buttons) {
        (void)buttons;
    }
    /// Chooses a picture preset by id for the running game: "auto" is the console's default,
    /// an unknown id is "plain". Its sliders start at the user's values
    /// (player.shader.<preset>.<param>). The choice itself is not saved here: the screen
    /// keeps it in the settings.
    virtual void SetPicture(const std::string& /*id*/) {}
    /// The sliders of the preset in use, with their values.
    virtual std::vector<PictureParam> PictureParams() const {
        return {};
    }
    /// Moves a slider of the preset in use, at once, and remembers the value.
    virtual void SetPictureParam(const std::string& /*param*/, float /*value*/) {}
    /// Back to the preset's own values.
    virtual void ResetPictureParams() {}
    /// The kept preview frame through preset `id` (its saved slider values) as it looks on
    /// the game's display: the middle of it at its real size, `width` x `height` pixels. The
    /// caller destroys the texture; none before a picture was shown. Compiles the preset,
    /// so it can take a moment. The frame is the latest picture when the first preview is
    /// made, and again after CapturePreviewFrame.
    virtual gfx::Texture PicturePreview(gfx::Canvas& /*canvas*/, const std::string& /*id*/, int /*width*/,
                                        int /*height*/) {
        return {};
    }
    /// The volume of the running game, 0..1, at once. Not saved here ("player.volume" is
    /// what the next game opens with).
    virtual void SetVolume(float /*volume*/) {}

    /// Starts the audio counters again (the totals and the queue's average, minimum, maximum).
    virtual void ResetAudioStats() {}
    /// Records what the sound device is given (after the resampler, the volume and the mix:
    /// gaps included) for `seconds`. Snapshot::audio_capturing tells when it is complete.
    virtual Status StartAudioCapture(double /*seconds*/) {
        return Status::Fail("This player cannot record its sound.");
    }
    /// Writes the finished capture as a 16-bit WAV file and says what it held.
    virtual Result<AudioCapture> SaveAudioCapture(const std::filesystem::path& /*wav*/) {
        return Status::Fail("This player cannot record its sound.");
    }

    virtual Snapshot GetSnapshot() const = 0;

    /// True once after an engine paused and handed its menu over (the player opens it).
    virtual bool TakeMenuRequest() {
        return false;
    }

    // For an achievements runtime (den::achievements).
    /// The memory map the core declared (SET_MEMORY_MAPS), null when it declared none or an
    /// engine app runs the game. Valid until the game closes or a MemoryMoved event.
    virtual const retro_memory_map* MemoryMap() const {
        return nullptr;
    }
    /// retro_get_memory_data with its size (RETRO_MEMORY_SYSTEM_RAM, ...); empty when none.
    virtual std::span<std::uint8_t> MemoryRegion(unsigned id) const {
        (void)id;
        return {};
    }
    /// Called on the main thread from Advance, Reset and LoadState; empty to stop.
    virtual void SetFrameHook(std::function<void(FrameEvent)> hook) {
        (void)hook;
    }
    /// Hardcore for the running game: LoadState fails and rewind is off. Turning it on does
    /// not undo a state loaded before.
    virtual void SetHardcore(bool on) {
        (void)on;
    }
    /// The file the core was given (an unpacked ROM rather than its zip); empty when none.
    virtual std::filesystem::path ContentPath() const {
        return {};
    }
    /// The game's latest picture as a PNG.
    virtual Status SaveScreenshot(const std::filesystem::path& png) {
        (void)png;
        return Status::Fail("This player cannot take a screenshot.");
    }
    /// Plays the running game with another device from now on, or alone again with no party.
    /// A request with a subsystem needs the game opened again instead.
    virtual void SetTogether(TogetherRequest /*request*/) {}
};

struct Config {
    std::filesystem::path data_dir; // saves, states and core options live under it
    Jobs* jobs = nullptr;
    gfx::Device* device = nullptr;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Session> CreateSession(const Config& config);

/// The battery save file of a game under the player's data folder (it may not exist).
std::filesystem::path BatterySaveFile(const std::filesystem::path& data_dir, const std::string& system,
                                      const std::string& game_id);

/// A session over the engine host: the game runs in an engine app, controlled through the
/// protocol. Option values are kept as "key=value" lists under engine_options.<core> for the
/// console and engine_game_options.<system>.<game> for one game, and sent with every launch.
std::unique_ptr<Session> CreateEngineSession(const Config& config, engine::Host& host);

/// One session for the app: Open goes to `engine` for a request with an engine and to
/// `core` otherwise, and everything else to whichever was opened last.
std::unique_ptr<Session> CreateRoutedSession(std::unique_ptr<Session> core, std::unique_ptr<Session> engine);

} // namespace den::player
