// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The running game's cheats: read at Open, given to the core after the game loaded and again
// whenever the list changes (retro_cheat_reset, then retro_cheat_set for every enabled one).

#include "session_impl.h"

namespace den::player {

void PlayerSession::LoadCheats(const std::filesystem::path& content) {
    cheats_ = snapshot_.can_cheats ? LoadGameCheats(paths_.Cheats(), content) : std::vector<Cheat>();
    ApplyCheats();
}

std::string PlayerSession::CheatsOff() const {
    if (hardcore_) {
        return "Hardcore mode is on: cheats are off.";
    }
    if (together_.party) {
        return "Not while playing together: the two games would differ.";
    }
    return {};
}

void PlayerSession::ApplyCheats() {
    snapshot_.cheats_active = 0;
    if (!core_ || !snapshot_.can_cheats) {
        return;
    }
    core_->CheatReset();
    if (!CheatsOff().empty()) {
        return;
    }
    unsigned index = 0;
    for (const Cheat& cheat : cheats_) {
        if (cheat.enabled) {
            core_->CheatSet(index++, true, CleanCode(cheat.code));
        }
    }
    snapshot_.cheats_active = static_cast<int>(index);
}

Status PlayerSession::SetCheats(std::vector<Cheat> cheats) {
    if (!core_ || !snapshot_.can_cheats) {
        return Status::Fail("This game has no cheats.");
    }
    if (const std::string off = CheatsOff(); !off.empty()) {
        return Status::Fail(off);
    }
    if (const Status tidy = TidyCheats(cheats); !tidy) {
        return tidy;
    }
    const Status written = WriteCheats(paths_.Cheats(), cheats);
    cheats_ = std::move(cheats);
    ApplyCheats();
    return written;
}

} // namespace den::player
