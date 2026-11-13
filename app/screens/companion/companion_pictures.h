// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The picture presets as a carousel on the second screen: a card per preset with the game's
// latest picture through it, at the size the game's display shows it, and its name.

#pragma once

#include <functional>
#include <string>

#include "den/ui/node.h"

namespace den::app {

/// A preset's card. Its preview is made the first time the card is drawn, one card a frame
/// at most, and kept for the game being played.
class PictureCard final : public ui::Node {
public:
    PictureCard(std::string preset, std::string name, bool in_use, std::function<void()> action);

protected:
    void DrawContent(ui::Context& ctx) override;

private:
    std::string preset_, name_;
    bool in_use_ = false;
};

/// Previews of `game` are kept; any other game's are let go (on the next card drawn).
void KeepPicturePreviews(const std::string& game);
/// Makes every preview again from a new frame of the game.
void ResetPicturePreviews();
/// Makes the preset's preview again, after its sliders moved.
void ForgetPicturePreview(const std::string& preset);

} // namespace den::app
