// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The in-game layer of the ROM player: the game drawn full window by den::player, and the
// pause menu over it. Route "player", parameter "id" (a library entry); "resume=0" starts
// the game from the beginning instead of where it was left.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "den/app/app.h"
#include "player_view.h"

namespace den::app {

class PlayerScreen final : public ui::Page {
public:
    ~PlayerScreen() override;

    void Build() override;
    void OnHide() override;
    void Update(ui::Context& ctx) override;
    bool OnEvent(const input::Event& event) override;
    bool OnNav(input::Nav nav) override;
    void Draw(ui::Context& ctx, const gfx::Rect& area) override;
    bool Immersive() const override {
        return true;
    }
    std::vector<ui::Hint> Hints() const override {
        return {};
    }

private:
    enum class Phase { New, Preparing, Running, Left };
    enum class View { Closed, Main, Save, Load, Options, Buttons, Players };

    // player_screen.cpp: the session.
    void Open();
    void Begin(const std::filesystem::path& content);
    void Fail(const std::string& message);
    void Leave();
    void OpenMenu();
    void CloseMenu();
    void Show(View view, std::string focus = {});
    void FeedPads();
    void ApplySpeed();
    void DrawOverlays(ui::Context& ctx, const gfx::Rect& area, const player::Snapshot& snapshot);
    bool WrapFocus(input::Nav nav);

    // pause_menu.cpp: the menu's pages as rows.
    void BuildMenu();
    void BuildMain(ui::Node* list);
    void BuildSlots(ui::Node* list, bool save);
    void BuildOptions(ui::Node* list);
    void BuildButtons(ui::Node* list);
    void BuildPlayers(ui::Node* list);
    void SetPicture(int index);
    void StartCapture(int game_button);
    void TickCapture();
    std::uint32_t HeldButtons() const;
    input::PadFamily PadFamilyShown() const;
    void SaveSlot(int slot);
    void LoadSlot(int slot);

    Phase phase_ = Phase::New;
    View view_ = View::Closed;
    library::Entry game_;
    plugins::Console console_;
    plugins::CoreInfo core_;
    JobPtr prepare_;
    std::shared_ptr<std::filesystem::path> prepared_;
    std::int64_t started_unix_ = 0;
    double started_ = 0;

    ScreenSize size_ = ScreenSize::Fit;
    float aspect_sent_ = 0;
    bool show_fps_ = false;
    std::string focus_after_build_;
    std::vector<player::CoreOption> options_;

    // Input while the game has the pad.
    bool wait_release_ = false;
    bool resume_told_ = false;
    bool pointer_down_ = false;
    bool guide_was_down_ = false;
    int pads_seen_ = 0;
    double chord_since_ = -1;
    bool key_fast_ = false, key_rewind_ = false;
    bool stick_fast_ = false, stick_rewind_ = false;
    bool fast_toggle_ = false;
    bool fast_sent_ = false, rewind_sent_ = false;
    bool chord_fast_ = false, chord_rewind_ = false;

    // The pause menu's pages.
    int picture_ = 0;                // index into player::PicturePresets()
    bool options_for_game_ = false;  // "Apply to": this game, or every game of the console
    int capture_ = -1;               // the game button waiting for a pad button; -1 = none
    bool capture_armed_ = false;     // every button was released since the capture started
    double capture_since_ = 0;
    bool swallow_nav_ = false;       // until the captured button is released
};

} // namespace den::app
