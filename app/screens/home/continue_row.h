// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Home's Continue (docs/sync.md, section 3.4): the games last played on any of the user's
// devices, each with the picture of the moment it was left, the device and how long ago.
// Choosing one starts it where it was left: its check before it starts brings the newer
// resume state down. A game only another device has offers its download from the online
// game library. Shared by the standard Home and the interface layouts.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "den/library/library.h"
#include "den/sync/continue.h"
#include "den/sync/engine.h"
#include "den/ui/widgets.h"

namespace den::app {

struct ContinueCard {
    sync::ContinueItem item;
    library::Entry game; // this device's entry; for a game it does not have, the name only
    std::optional<sync::RemoteGame> online; // only another device has it: the library's copy
};

/// The cards, newest first; hidden games are left out.
std::vector<ContinueCard> ContinueCards();
/// Changes whenever what the cards show changed (plays, pictures, the online library, a
/// download): the screens rebuild then. Forgets a picture the texture cache holds when its
/// file changed.
std::string ContinueSignature(const std::vector<ContinueCard>& cards);
/// "On Legion Go S · 12 min ago", "On this device · Yesterday", "Downloading 40%".
std::string ContinueLine(const ContinueCard& card);
/// Starts the game where it was left, or offers its download.
void ChooseContinue(const ContinueCard& card);
/// True once for each newest session the cards show (another device's game just came in, or
/// a game just left here): Home then puts the selection on it.
bool ContinueNews(const std::vector<ContinueCard>& cards);
/// Home was shown: a sync starts when the last one is over a minute old, so a game just left
/// on another device shows up.
void RefreshContinue();
/// The id a card's entry gets: the game's id, or "online:<system>/<stem>".
std::string ContinueId(const ContinueCard& card);

/// A Continue card: the picture of the moment (else the cover), the name and ContinueLine.
class ContinueTile final : public ui::Tile {
public:
    explicit ContinueTile(const ContinueCard& card);
    std::string line;
    bool screenshot = false; // the picture is the game's, not a box: it fills the card
    std::string cover;       // drawn small in the corner over a screenshot

protected:
    void DrawContent(ui::Context& ctx) override;
};

/// The picture of the moment a game was left, fitted in the node; nothing without one.
class MomentPicture final : public ui::Node {
public:
    MomentPicture(float w, float h);
    std::string path;

protected:
    void DrawContent(ui::Context& ctx) override;
};

constexpr float CONTINUE_W = 320, CONTINUE_H = 192;

} // namespace den::app
