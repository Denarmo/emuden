// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den_achievements_probe <system> <rom>: the RetroAchievements hash of a game and the game id
// the server gives it, without signing in.

#include <cstdio>

#include "den/achievements/games.h"
#include "den/base/files.h"

using namespace den;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: den_achievements_probe <system> <rom>\n");
        return 2;
    }
    const std::uint32_t console = achievements::ConsoleFor(argv[1]);
    const auto hash = achievements::HashFile(console, PathFromUtf8(argv[2]));
    if (!hash) {
        std::fprintf(stderr, "%s\n", hash.error().c_str());
        return 1;
    }
    std::printf("console %u hash %s\n", console, hash.value().c_str());
    const auto http = achievements::PlatformHttp(achievements::UserAgent("EmuDEN", "0.1"));
    const auto id = achievements::ResolveHash(http, {}, hash.value());
    if (!id) {
        std::fprintf(stderr, "%s\n", id.error().c_str());
        return 1;
    }
    std::printf("game id %u\n", id.value());
    return 0;
}
