// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The in-game layer of the ROM player: the game drawn full window by den::player, and the
// pause menu over it. Route "player", parameter "id" (a library entry); "resume=0" starts
// the game from the beginning instead of where it was left.
//
// With a second display (app/screens/companion/second_screen.h) the pause menu opens there
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
#include "dual_screen.h"
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

    // For the quick settings of the second display (companion.play).
    /// The screen whose game is running; null when there is none.
    static PlayerScreen* Current();
    /// Opens one page of the pause menu by itself ("options", "buttons"); Back closes it.
    void OpenPage(const std::string& page);
    /// The game goes to the other display and the quick settings come to this one. A game
    /// with two screens swaps which display has its top screen.
    void SwapDisplays();
    int ScreenSizeIndex() const {
        return static_cast<int>(size_);
    }
    void SetScreenSize(int index);
    /// The frame rate overlay: 0 off, 1 the numbers, 2 the numbers and a graph of the frame times.
    static constexpr int FPS_MODES = 3;
    int FpsMode() const {
        return fps_mode_;
    }
    void SetFpsMode(int mode);
    /// The game is on the second display and the quick settings in the main window.
    bool Swapped() const {
        return swapped_;
    }
    /// Pauses with the menu open, as the menu button does; nothing when it is open already.
    void Pause() {
        OpenMenu();
    }
    /// Saves and leaves the game, as the pause menu's "Exit to home".
    void Exit() {
        Leave();
    }
    /// The display the game is on is covered by another app (true): it pauses without a
    /// menu and goes on by itself when the display is back (false). A game that is paused
    /// in its menu stays so.
    void Hold(bool hold);
    /// The game is set to play on the second display (whether or not that is there now).
    bool WantsSecond() const;

private:
    enum class Phase { New, Preparing, Running, Left };
    enum class View { Closed, Main, Save, Load, Options, Buttons, Players, Picture, PictureAdjust, Screens };

    // player_screen.cpp: the session.
    void Open();
    void Begin(const std::filesystem::path& content);
    void Fail(const std::string& message);
    void Leave();
    void OpenMenu(View view = View::Main);
    void CloseMenu();
    void Show(View view, std::string focus = {});
    void FeedPads();
    void ApplySpeed();
    bool WrapFocus(input::Nav nav);

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

    // pause_menu.cpp: the menu's pages as rows.
    void BuildMenu();
    void BuildMain(ui::Node* list);
    void BuildSlots(ui::Node* list, bool save);
    void BuildOptions(ui::Node* list);
    void BuildButtons(ui::Node* list);
    void BuildPlayers(ui::Node* list);
    // picture_pages.cpp: the picture preset and its sliders.
    void BuildPicture(ui::Node* list);
    void BuildPictureAdjust(ui::Node* list);
    void LoadPicture();
    void ChoosePicture(const std::string& id);
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

    DualScreens dual_;               // a console with two screens
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
    double chord_since_ = -1;
    bool key_fast_ = false, key_rewind_ = false;
    bool stick_fast_ = false, stick_rewind_ = false;
    bool fast_toggle_ = false;
    bool fast_sent_ = false, rewind_sent_ = false;
    bool chord_fast_ = false, chord_rewind_ = false;

    // The pause menu's pages.
    std::string picture_ = "auto";   // the kept choice: a preset id of den::player, or "auto"
    int picture_scope_ = 1;          // "Apply to": a PictureScope
    bool options_for_game_ = false;  // "Apply to": this game, or every game of the console
    int capture_ = -1;               // the game button waiting for a pad button; -1 = none
    bool capture_armed_ = false;     // every button was released since the capture started
    double capture_since_ = 0;
    bool swallow_nav_ = false;       // until the captured button is released
};

} // namespace den::app
