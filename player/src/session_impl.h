// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The real den::player::Session. It runs the core on the calling (main) thread: Advance
// emulates the frames that are due, Draw shows the latest picture.

#pragma once

#include <array>
#include <memory>
#include <optional>

#include "audio_out.h"
#include "core/core.h"
#include "den/player/session.h"
#include "game_files.h"
#include "hw_context.h"
#include "pad_map.h"
#include "picture_presets.h"
#include "rewind_buffer.h"

namespace den::player {

class PlayerSession final : public Session {
public:
    explicit PlayerSession(const Config& config);
    ~PlayerSession() override;

    Status Open(const OpenRequest& request) override;
    void Close() override;
    void Pause() override;
    void Resume() override;
    void SetPad(int port, const input::PadState& pad) override;
    void SetPointer(float x, float y, bool pressed) override;
    void SetAspectOverride(float aspect) override {
        aspect_override_ = aspect;
    }
    std::vector<GameScreen> Screens() const override;
    void DrawScreen(gfx::Canvas& canvas, int screen, const gfx::Rect& target) override;
    void SetScreenPointer(int screen, float u, float v, bool pressed) override;
    void Advance(double seconds) override;
    void Draw(gfx::Canvas& canvas, const gfx::Rect& area) override;
    Status SaveState(int slot) override;
    Status LoadState(int slot) override;
    bool HasState(int slot) const override;
    std::filesystem::path StateThumbnail(int slot) const override;
    void SetFastForward(bool on) override;
    void SetRewind(bool on) override;
    void Reset() override;
    void FlushSaves() override;
    std::vector<CoreOption> Options() const override;
    void SetOption(const std::string& key, const std::string& value) override;
    void SetOption(const std::string& key, const std::string& value, OptionScope scope) override;
    void ClearGameOptions() override;
    void ReloadButtons() override;
    void SetPicture(const std::string& id) override;
    std::vector<PictureParam> PictureParams() const override;
    void SetPictureParam(const std::string& param, float value) override;
    void ResetPictureParams() override;
    void SetVolume(float volume) override {
        audio_.SetVolume(volume);
    }
    void ResetAudioStats() override {
        audio_.ResetStats();
    }
    Status StartAudioCapture(double seconds) override;
    Result<AudioCapture> SaveAudioCapture(const std::filesystem::path& wav) override;
    Snapshot GetSnapshot() const override;

private:
    Status Fail(std::string message);
    Status Start(const OpenRequest& request);
    void ApplyStoredOptions();
    std::string OptionSettingKey(const std::string& key) const;
    std::string GameOptionKey(const std::string& key) const;
    void ReadAvInfo();
    void RunForward(int frames, bool sound_on_all);
    bool StepRewind();
    void CaptureRewind();
    void SetupRewind();
    void LoadBatterySave();
    Status WriteStateFile(const std::filesystem::path& file);
    Status ReadStateFile(const std::filesystem::path& file);
    void WriteThumbnail(const std::filesystem::path& file);
    void MeasureFps(double seconds);
    gfx::Rect PictureRect(const gfx::Canvas& canvas, const gfx::Rect& area) const;
    bool PreparePicture(gfx::Canvas& canvas, PictureSource& source);
    void DrawRegion(gfx::Canvas& canvas, const PictureSource& source, const PictureRegion& region,
                    int slot);
    bool ReadyChain(gfx::Device& device);
    void ReleasePicture();

    Config config_;
    // Destroyed after the core: the core frees what it drew with while it unloads.
    std::unique_ptr<HwContext> hw_;
    std::unique_ptr<LibretroCore> core_;
    AudioOut audio_;
    std::optional<RewindBuffer> rewind_;
    GamePaths paths_;
    std::filesystem::path content_cache_;
    Snapshot snapshot_;
    std::string system_;
    AvInfo av_;

    // Settings as read at Open.
    bool resume_enabled_ = true;
    bool pad_by_label_ = true;
    bool integer_scale_ = false;
    bool smooth_ = true;
    int fast_forward_speed_ = 3;

    // Pacing.
    double due_ = 0.0;       // core frames owed
    double step_ = 1.0;      // smoothed core frames per display frame
    double fps_window_ = 0.0;
    int fps_frames_ = 0;
    double save_timer_ = 0.0;
    bool pending_resume_ = false;

    // Rewind.
    unsigned rewind_interval_ = 2;
    unsigned frames_since_capture_ = 0;
    std::vector<std::uint8_t> state_scratch_;
    std::vector<std::uint8_t> saved_ram_;

    // Video.
    gfx::Texture texture_;
    std::uint64_t uploaded_serial_ = 0;
    int texture_filter_ = -1;
    unsigned filtered_texture_ = 0;
    gfx::Rect picture_rect_;
    float aspect_override_ = 0.0f;
    // The picture preset: its passes are compiled when it is first drawn.
    std::string picture_ = "plain";
    const PresetData* preset_ = nullptr;   // null for "plain"
    std::vector<float> picture_values_;    // per PresetData::uniforms
    gfx::EffectChain chain_;
    bool chain_stale_ = true;
    std::uint64_t picture_frame_ = 0;
    int frame_owner_ = -1;          // the screen whose DrawScreen counts the frames
    bool frame_other_seen_ = false;

    // Input: the button map per pad family, with the user's remap.
    std::array<ButtonMap, 4> button_maps_{};
    std::string game_key_;
};

} // namespace den::player
