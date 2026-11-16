// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/app/player_hooks.h"

namespace den::app {

namespace {

std::vector<PlayerHooks>& Hooks() {
    static std::vector<PlayerHooks> hooks;
    return hooks;
}

} // namespace

const std::vector<PlayerHooks>& AllPlayerHooks() {
    return Hooks();
}

void AddPlayerHooks(PlayerHooks hooks) {
    Hooks().push_back(std::move(hooks));
}

} // namespace den::app
