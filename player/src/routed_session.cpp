// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The app's one session: a libretro core or an engine app, chosen by each Open.

#include "den/player/session.h"

namespace den::player {

namespace {

class RoutedSession final : public Session {
public:
    RoutedSession(std::unique_ptr<Session> core, std::unique_ptr<Session> engine)
        : core_(std::move(core)), engine_(std::move(engine)), active_(core_.get()) {}

    Status Open(const OpenRequest& request) override {
        Session* next = request.engine && engine_ ? engine_.get() : core_.get();
        if (next != active_) {
            active_->Close(); // one game at a time, as either session keeps it
        }
        active_ = next;
        return active_->Open(request);
    }
    void Close() override {
        active_->Close();
    }
    void Pause() override {
        active_->Pause();
    }
    void Resume() override {
        active_->Resume();
    }
    void SetPad(int port, const input::PadState& pad) override {
        active_->SetPad(port, pad);
    }
    void SetPointer(float x, float y, bool pressed) override {
        active_->SetPointer(x, y, pressed);
    }
    void Advance(double seconds) override {
        active_->Advance(seconds);
    }
    void Draw(gfx::Canvas& canvas, const gfx::Rect& area) override {
        active_->Draw(canvas, area);
    }
    std::vector<GameScreen> Screens() const override {
        return active_->Screens();
    }
    void SetBfi(const BfiRequest& request) override {
        active_->SetBfi(request);
    }
    void CapturePreviewFrame() override {
        active_->CapturePreviewFrame();
    }
    void DrawScreen(gfx::Canvas& canvas, int screen, const gfx::Rect& target) override {
        active_->DrawScreen(canvas, screen, target);
    }
    void SetScreenPointer(int screen, float u, float v, bool pressed) override {
        active_->SetScreenPointer(screen, u, v, pressed);
    }
    void SetAspectOverride(float aspect) override {
        active_->SetAspectOverride(aspect);
    }
    Status SaveState(int slot) override {
        return active_->SaveState(slot);
    }
    Status LoadState(int slot) override {
        return active_->LoadState(slot);
    }
    bool HasState(int slot) const override {
        return active_->HasState(slot);
    }
    std::filesystem::path StateThumbnail(int slot) const override {
        return active_->StateThumbnail(slot);
    }
    void SetFastForward(bool on) override {
        active_->SetFastForward(on);
    }
    void SetRewind(bool on) override {
        active_->SetRewind(on);
    }
    void Reset() override {
        active_->Reset();
    }
    void FlushSaves() override {
        active_->FlushSaves();
    }
    void SaveResumeState() override {
        active_->SaveResumeState();
    }
    std::vector<CoreOption> Options() const override {
        return active_->Options();
    }
    void SetOption(const std::string& key, const std::string& value) override {
        active_->SetOption(key, value);
    }
    void SetOption(const std::string& key, const std::string& value, OptionScope scope) override {
        active_->SetOption(key, value, scope);
    }
    void ClearGameOptions() override {
        active_->ClearGameOptions();
    }
    void ReloadButtons() override {
        active_->ReloadButtons();
    }
    int MicrophoneButton() const override {
        return active_->MicrophoneButton();
    }
    void SetHeldButtons(std::uint16_t buttons) override {
        active_->SetHeldButtons(buttons);
    }
    void SetPicture(const std::string& id) override {
        active_->SetPicture(id);
    }
    std::vector<PictureParam> PictureParams() const override {
        return active_->PictureParams();
    }
    void SetPictureParam(const std::string& param, float value) override {
        active_->SetPictureParam(param, value);
    }
    void ResetPictureParams() override {
        active_->ResetPictureParams();
    }
    gfx::Texture PicturePreview(gfx::Canvas& canvas, const std::string& id, int width, int height) override {
        return active_->PicturePreview(canvas, id, width, height);
    }
    void SetVolume(float volume) override {
        active_->SetVolume(volume);
    }
    void ResetAudioStats() override {
        active_->ResetAudioStats();
    }
    Status StartAudioCapture(double seconds) override {
        return active_->StartAudioCapture(seconds);
    }
    Result<AudioCapture> SaveAudioCapture(const std::filesystem::path& wav) override {
        return active_->SaveAudioCapture(wav);
    }
    Snapshot GetSnapshot() const override {
        return active_->GetSnapshot();
    }
    std::vector<Cheat> Cheats() const override {
        return active_->Cheats();
    }
    Status SetCheats(std::vector<Cheat> cheats) override {
        return active_->SetCheats(std::move(cheats));
    }
    bool TakeMenuRequest() override {
        return active_->TakeMenuRequest();
    }
    const retro_memory_map* MemoryMap() const override {
        return active_->MemoryMap();
    }
    std::span<std::uint8_t> MemoryRegion(unsigned id) const override {
        return active_->MemoryRegion(id);
    }
    void SetFrameHook(std::function<void(FrameEvent)> hook) override {
        // Either session may play the next game.
        core_->SetFrameHook(hook);
        if (engine_) {
            engine_->SetFrameHook(hook);
        }
    }
    void SetHardcore(bool on) override {
        active_->SetHardcore(on);
    }
    std::filesystem::path ContentPath() const override {
        return active_->ContentPath();
    }
    Status SaveScreenshot(const std::filesystem::path& png) override {
        return active_->SaveScreenshot(png);
    }

private:
    std::unique_ptr<Session> core_;
    std::unique_ptr<Session> engine_;
    Session* active_;
};

} // namespace

std::unique_ptr<Session> CreateRoutedSession(std::unique_ptr<Session> core, std::unique_ptr<Session> engine) {
    return std::make_unique<RoutedSession>(std::move(core), std::move(engine));
}

} // namespace den::player
