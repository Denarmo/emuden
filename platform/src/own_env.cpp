// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <SDL3/SDL.h>

#include "own_env.h"

namespace den::pal {

namespace {

std::vector<OwnEnvChange>& Changes() {
    static std::vector<OwnEnvChange> changes;
    return changes;
}

} // namespace

void SetOwnEnv(const char* name, const std::string& value) {
    auto& changes = Changes();
    const bool noted = std::any_of(changes.begin(), changes.end(),
                                   [&](const OwnEnvChange& change) { return change.name == name; });
    if (!noted) {
        // The process's own environment, not SDL's copy of it.
        const char* before = SDL_getenv_unsafe(name);
        changes.push_back({name, before != nullptr, before ? before : ""});
    }
    SDL_setenv_unsafe(name, value.c_str(), 1);
}

const std::vector<OwnEnvChange>& OwnEnvChanges() {
    return Changes();
}

} // namespace den::pal
