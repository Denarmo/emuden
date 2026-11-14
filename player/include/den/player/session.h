// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the ROM player: one libretro core with one game, running inside the app's
// window. libretro.h (from upstream libretro) is in third_party/libretro.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/gfx/canvas.h"
#include "den/input/input.h"
#include "den/player/screens.h"

namespace den::store {
class Settings;
}

namespace den::player {

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
};

enum class State { Closed, Running, Paused, Failed };

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
};

struct Config {
    std::filesystem::path data_dir; // saves, states and core options live under it
    Jobs* jobs = nullptr;
    gfx::Device* device = nullptr;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Session> CreateSession(const Config& config);

} // namespace den::player
