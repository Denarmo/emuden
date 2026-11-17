// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The quick settings page ("companion.play"): its main view comes from the chosen layout
// (companion_quick.h), its sections are the same for every layout. The page's parts are in
// companion_play.cpp (views, updates, colours), companion_actions.cpp (the actions) and
// companion_sections.cpp (the sections).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "companion_page.h"
#include "den/app/player.h"
#include "companion_quick.h"
#include "den/ui/theme.h"

namespace den::app {

/// The quick settings layout chosen in Settings, or the standard one (companion_quick_choice.cpp).
std::shared_ptr<const QuickSpec> ChosenQuickLayout();

class CompanionPlay final : public CompanionPage {
public:
    void Build() override;
    void Update(ui::Context& ctx) override;
    void Draw(ui::Context& ctx, const gfx::Rect& area) override;

private:
    void Open(QuickView view);
    // A section's title with the way back to the main view.
    void Heading(ui::Node& root, const std::string& title, QuickView back_to = QuickView::Main);
    void BesideRow(ui::Node& parent);

    // companion_actions.cpp
    std::vector<QuickAction> MakeActions(const player::Snapshot& snapshot);
    // The spec's actions, in its order, of those there now.
    std::vector<QuickAction> Arranged(const player::Snapshot& snapshot);
    ui::RowSpec BfiRow(const GamePlayer& screen, const player::Snapshot& snapshot);

    // companion_sections.cpp
    void BuildStates(ui::Node& root, int columns = 0); // 0: four a row
    void BuildScreen(ui::Node& root, const player::Snapshot& snapshot);
    void BuildPicture(ui::Node& root);
    void BuildControls(ui::Node& root);
    void BuildVolume(ui::Node& root);
    void Save(int slot);
    void Load(int slot);

    // What the page shows; it is built again when this changes.
    std::string Key(const player::Snapshot& snapshot) const;
    // The shape of the area the main view was built for, where the layout depends on it.
    int Shape(float width, float height) const;

    std::string key_, game_id_, system_;
    QuickView view_ = QuickView::Main;
    std::shared_ptr<const QuickSpec> spec_;
    ui::Theme theme_;
    bool save_mode_ = false;
    bool reveal_picture_ = false; // the Screen view was just opened
    int reveal_index_ = -1;       // the carousel card to bring into view
    bool fast_ = false, rewind_ = false;
    bool other_volume_ = false;
    float width_ = 0, height_ = 0; // the area last drawn
    int shape_ = -1;               // Shape() the page was built for
};

} // namespace den::app
