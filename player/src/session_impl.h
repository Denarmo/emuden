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
#include "hw_host.h"
#include "pad_map.h"
#include "den/gfx/retro_chain.h"
#include "picture_presets.h"
#include "rewind_buffer.h"

namespace den::player {

/// The key of the renderer entry among a session's core options; never passed to the core.
inline constexpr const char* RENDERER_OPTION = "emuden_renderer";

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
    void SaveResumeState() override;
    std::vector<CoreOption> Options() const override;
    void SetOption(const std::string& key, const std::string& value) override;
    void SetOption(const std::string& key, const std::string& value, OptionScope scope) override;
    void ClearGameOptions() override;
    void ReloadButtons() override;
    int MicrophoneButton() const override;
    void SetHeldButtons(std::uint16_t buttons) override {
        held_buttons_ = buttons;
    }
    void SetPicture(const std::string& id) override;
    std::vector<PictureParam> PictureParams() const override;
    void SetPictureParam(const std::string& param, float value) override;
    void ResetPictureParams() override;
    gfx::Texture PicturePreview(gfx::Canvas& canvas, const std::string& id, int width, int height) override;
    void CapturePreviewFrame() override;
    void SetVolume(float volume) override {
        audio_.SetVolume(volume);
    }
    void ResetAudioStats() override {
        audio_.ResetStats();
    }
    Status StartAudioCapture(double seconds) override;
    Result<AudioCapture> SaveAudioCapture(const std::filesystem::path& wav) override;
    Snapshot GetSnapshot() const override;
    // session_cheats.cpp
    std::vector<Cheat> Cheats() const override {
        return cheats_;
    }
    Status SetCheats(std::vector<Cheat> cheats) override;
    // session_achievements.cpp
    const retro_memory_map* MemoryMap() const override;
    std::span<std::uint8_t> MemoryRegion(unsigned id) const override;
    void SetFrameHook(std::function<void(FrameEvent)> hook) override {
        frame_hook_ = std::move(hook);
    }
    void SetHardcore(bool on) override;
    std::filesystem::path ContentPath() const override {
        return core_ ? content_path_ : std::filesystem::path();
    }
    Status SaveScreenshot(const std::filesystem::path& png) override;
    void SetTogether(TogetherRequest request) override;
    // session_pacing.cpp
    void SetBfi(const BfiRequest& request) override;

private:
    void Notify(FrameEvent event) {
        if (frame_hook_) {
            frame_hook_(event);
        }
    }
    // session_net.cpp: playing with another device.
    bool LoadContent(const std::filesystem::path& content, std::string* error);
    void StartTogether();
    void StopTogether();
    void PumpTogether();
    void StartLink();
    void DeliverPackets();
    bool InLockstep() const;
    void LeftLockstep();
    void TickLockstep();
    void AdvanceLockstep(double frames);
    void SendStateToPeer(std::uint64_t frame);
    void TakeStateFromPeer();
    bool StoreLocalInput(int port, const RetroPad& mapped);

    // session_cheats.cpp
    void LoadCheats(const std::filesystem::path& content);
    void ApplyCheats();
    std::string CheatsOff() const;

    Status Fail(std::string message);
    Status Start(const OpenRequest& request);
    void ApplyStoredOptions();
    std::string OptionSettingKey(const std::string& key) const;
    std::string GameOptionKey(const std::string& key) const;
    // The renderer choice, kept like a core option (session_renderer.cpp).
    std::string RendererValue() const;
    void AddRendererOption(std::vector<CoreOption>& options) const;
    void SetRenderer(const std::string& value, OptionScope scope);
    void ClearGameRenderer();
    void ReadAvInfo();
    void RunForward(int frames, bool sound_on_all);
    bool StepRewind();
    void CaptureRewind();
    void SetupRewind();
    void LoadBatterySave();
    /// Runs a few more frames, unseen, until another GBA core could take over the state.
    void SettleForOtherCores();
    Status WriteStateFile(const std::filesystem::path& file);
    Status ReadStateFile(const std::filesystem::path& file);
    void WriteThumbnail(const std::filesystem::path& file);
    void MeasureFps(double seconds);
    void PlanBfiFrame();
    bool AdvanceBfi(double seconds);
    gfx::Rect PictureRect(const gfx::Canvas& canvas, const gfx::Rect& area) const;
    bool PreparePicture(gfx::Canvas& canvas, PictureSource& source);
    void DrawRegion(gfx::Canvas& canvas, const PictureSource& source, const PictureRegion& region,
                    int slot);
    bool ReadyChain(gfx::Device& device);
    std::vector<float> SavedValues(const PresetData& preset) const;
    bool KeepPreviewFrame(gfx::Canvas& canvas);
    void ReleasePreviewFrame(gfx::Device& device);
    void ReleasePicture();

    Config config_;
    // Destroyed after the core: the core frees what it drew with while it unloads.
    std::unique_ptr<HwHost> hw_;
    std::unique_ptr<LibretroCore> core_;
    AudioOut audio_;
    std::optional<RewindBuffer> rewind_;
    GamePaths paths_;
    std::filesystem::path content_cache_;
    Snapshot snapshot_;
    std::string system_;
    std::vector<ScreenShape> screen_shapes_;
    AvInfo av_;

    // Achievements.
    std::function<void(FrameEvent)> frame_hook_;
    bool hardcore_ = false;
    Allowed allowed_;
    std::filesystem::path content_path_;

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
    BfiRequest bfi_;
    BfiCadence bfi_cadence_;
    bool bfi_ran_ = false; // the last Advance paced the game for black frames

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
    gfx::Rect shown_part_{0, 0, 1, 1}; // what DrawRegion drew last, for PicturePreview
    float shown_w_ = 0, shown_h_ = 0;  // ... and its size in pixels
    gfx::Texture preview_frame_;        // the frame of the previews, its first row the last
    float preview_w_ = 0, preview_h_ = 0; // the size it was shown at
    bool preview_stale_ = true;          // the next preview keeps a new frame
    float aspect_override_ = 0.0f;
    // The picture preset: its passes are compiled when it is first drawn.
    std::string picture_ = "plain";
    const PresetData* preset_ = nullptr;   // null for "plain"
    std::vector<float> picture_values_;    // per PresetData::uniforms
    gfx::EffectChain chain_;
    gfx::RetroChain retro_; // an imported preset's, in place of chain_
    bool chain_stale_ = true;
    std::uint64_t picture_frame_ = 0;
    int frame_owner_ = -1;          // the screen whose DrawScreen counts the frames
    bool frame_other_seen_ = false;

    // Input: the button map per pad family, with the user's remap.
    std::array<ButtonMap, 4> button_maps_{};
    std::uint16_t held_buttons_ = 0; // port 1, for a shortcut
    std::string game_key_;

    std::vector<Cheat> cheats_;

    // Playing with another device.
    TogetherRequest together_;
    bool link_started_ = false;     // the core's netpacket session runs
    bool peer_connected_ = false;   // ... and the other device is in it
    int lockstep_owed_ = 0;         // frames due that waited for the other device
    bool lockstep_ran_ = false;
    double last_resync_ = -100;
    double last_link_pump_ = 0;
    int quick_resyncs_ = 0;
    double waiting_since_ = -1;
    std::array<std::int16_t, 4> local_axes_{};
    std::uint16_t local_buttons_ = 0;
};

} // namespace den::player
