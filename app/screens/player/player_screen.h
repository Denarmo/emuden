// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The in-game layer of the ROM player: the game drawn full window by den::player, and the
// pause menu over it. Route "player", parameter "id" (a library entry); "resume=0" starts
// the game from the beginning instead of where it was left.
//
// With a second display (app/include/den/app/second_screen.h) the pause menu opens there
// and the game stays as it is ("player.overlay_on_second_screen"), and the game and the
// quick settings can change places ("player.swap_displays").

#pragma once

#include <array>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "den/app/app.h"
#include "den/app/player.h"
#include "den/app/player_hooks.h"
#include "den/player/buttons.h"
#include "den/app/black_frames.h"
#include "dual_screen.h"
#include "touch_pad.h"
#include "den/app/player_view.h"

namespace den::app {

class PlayerScreen final : public ui::Page, public GamePlayer {
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

    /// The screen whose game is running; null when there is none.
    static PlayerScreen* Current();

    // GamePlayer: for the quick settings of the second display, the apps beside, sync.
    bool InStartCheck() const override {
        return InSaveCheck();
    }
    void OpenPage(const std::string& page) override;
    void SwapDisplays() override;
    int ScreenSizeIndex() const override {
        return static_cast<int>(size_);
    }
    void SetScreenSize(int index) override;
    int FpsMode() const override {
        return fps_mode_;
    }
    void SetFpsMode(int mode) override;
    bool CanWidescreen() const override;
    bool Widescreen() const override {
        return widescreen_;
    }
    bool WidescreenRestarts() const override {
        return core_.widescreen_restart;
    }
    void SetWidescreen(bool on) override;
    bool Swapped() const override {
        return swapped_;
    }
    void Pause() override {
        OpenMenu();
    }
    void Exit() override {
        Leave();
    }
    void Hold(bool hold) override;
    bool WantsSecond() const override;
    bool External() const override;
    const BlackFrames& Bfi() const override {
        return bfi_;
    }
    void SetBfi(bool on) override;
    const std::string& CoreId() const override {
        return core_.id;
    }

private:
    enum class Phase { New, Gated, Preparing, Running, Left };
    enum class View { Closed, Main, Save, Load, Options, Buttons, Players, Picture, PictureAdjust, Screens, ButtonPick };

    // player_screen.cpp: the session.
    void Open();
    void Proceed(); // after the gates: unpacks the game, or starts it
    void Begin(const std::filesystem::path& content, bool resume = true);
    void Fail(const std::string& message);
    void Leave();
    void Restart();
    void ReadWidescreen();
    void OpenMenu(View view = View::Main);
    void CloseMenu();
    void Show(View view, std::string focus = {});
    void FeedPads();
    void ApplySpeed();
    bool WrapFocus(input::Nav nav);

    // What other features hold the game for before it starts (den/app/player_hooks.h: sync's
    // check for newer saves).
    bool StartGate();
    void TickGate();
    void AbandonGate(); // the screen goes: the gate writes nothing more
    bool InSaveCheck() const {
        return phase_ == Phase::Gated;
    }
    /// The resume state was written by another core (another device plays this console with
    /// another one): this launch uses that core when it is installed here (player_start.cpp).
    void MatchResumeCore();

    // player_displays.cpp: what is drawn where, on one display or two.
    void PlaceDisplays();
    void ApplyOverlay();
    void StopDisplays();
    void DrawGameLayer(ui::Context& ctx, const gfx::Rect& area, bool menu_here);
    void DrawMenu(ui::Context& ctx, const gfx::Rect& area);
    void DrawSecond(ui::Context& ctx, const gfx::Rect& area);
    void SecondPointer(const input::Event& event);
    void DrawOverlays(ui::Context& ctx, const gfx::Rect& area, const player::Snapshot& snapshot);
    void DrawFrameGraph(gfx::Canvas& canvas, const gfx::Rect& box, float text_size) const;
    bool TabPressed(const input::Event& event, const gfx::Rect& area, float scale);
    void HandlePlayTouch(const input::Event& event, float menu_corner);

    // pause_menu.cpp: the menu's pages as rows.
    void BuildMenu();
    void BuildMain(ui::Node* list);
    void BuildSlots(ui::Node* list, bool save);
    void BuildOptions(ui::Node* list);
    void BuildButtons(ui::Node* list);
    void BuildPlayers(ui::Node* list);
    ui::RowSpec BfiRow(const player::Snapshot& snapshot);
    // picture_pages.cpp: the picture preset and its sliders.
    void BuildPicture(ui::Node* list);
    void BuildPictureAdjust(ui::Node* list);
    void LoadPicture();
    void ChoosePicture(const std::string& id);
    input::PadFamily PadFamilyShown() const;
    // player_shortcuts.cpp: what each pad button does, game buttons and shortcuts.
    void BuildButtonPick(ui::Node* list);
    std::vector<player::Shortcut> ShortcutsHere() const;
    std::string ControlText(input::PadButton control) const;
    void Assign(input::PadButton control, int game_button, player::Shortcut shortcut);
    void LoadShortcutMap();
    void FeedShortcuts(input::PadState* pads, int count);
    void RunShortcut(player::Shortcut shortcut);
    void SaveSlot(int slot);
    void LoadSlot(int slot);

    Phase phase_ = Phase::New;
    View view_ = View::Closed;
    library::Entry game_;
    plugins::Console console_;
    plugins::CoreInfo core_;
    JobPtr prepare_;
    std::unique_ptr<PlayerGate> gate_;
    std::shared_ptr<std::filesystem::path> prepared_;
    std::filesystem::path content_; // what the core was given
    bool widescreen_ = false;
    std::int64_t started_unix_ = 0;
    double started_ = 0;

    DualScreens dual_;               // a console with two screens
    TouchPad touch_;                 // console-authentic on-screen touch controller
    BlackFrames bfi_;
    ScreenSize size_ = ScreenSize::Fit;
    float aspect_sent_ = 0;
    int fps_mode_ = 0;
    std::array<float, 120> frame_ms_{}; // the last frames' times, oldest at frame_at_
    std::size_t frame_at_ = 0;
    std::uint64_t graph_frames_ = 0; // the game's frame count when the last one was timed
    double graph_time_ = 0;
    gfx::Rect area_;                 // the window, as last drawn
    gfx::Rect menu_area_;            // the display the menu is on, as last drawn
    MenuLayout menu_;                // what the menu was built for
    bool built_apart_ = false;

    // Two displays.
    bool swapped_ = false;           // the game is on the second display, the quick settings here
    bool menu_apart_ = false;        // an open menu has a display to itself, the game the other
    bool menu_second_ = false;       // an open menu is drawn on the second display
    bool direct_ = false;            // the menu's page was opened from the quick settings
    bool second_down_ = false;       // a finger is on the game on the second display
    bool held_ = false;              // paused because the game's display is covered
    gfx::Rect second_area_;          // the second display, as last drawn
    std::string focus_after_build_;
    std::vector<player::CoreOption> options_;

    // Input while the game has the pad.
    bool wait_release_ = false;
    bool resume_told_ = false;
    bool pointer_down_ = false;
    bool guide_was_down_ = false;
    int pads_seen_ = 0;
    double select_since_ = -1;
    bool key_fast_ = false, key_rewind_ = false;
    bool stick_fast_ = false, stick_rewind_ = false;
    bool fast_toggle_ = false;
    bool fast_sent_ = false, rewind_sent_ = false;
    bool chord_fast_ = false, chord_rewind_ = false; // Select with a shoulder, last frame
    bool rewind_toggle_ = false;

    // The pause menu's pages.
    std::string picture_ = "auto";   // the kept choice: a preset id of den::player, or "auto"
    int picture_scope_ = 1;          // "Apply to": a PictureScope
    bool options_for_game_ = false;  // "Apply to": this game, or every game of the console
    input::PadButton pick_ = input::PadButton::Count; // the control whose choices are shown
    player::ShortcutMap shortcuts_{};
    std::uint32_t shortcut_down_ = 0; // the shortcut buttons held last frame
};

} // namespace den::app
