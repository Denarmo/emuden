// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What den.achievements reaches (permission "achievements", capability
// "feature.achievements.1"): the app's RetroAchievements runtime. The plugin is its sign-in,
// settings and pages; the runtime itself runs in the player. The app implements it; tests use
// a fake. Calls marked blocking are made from a helper thread.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::plugins {

/// The official RetroAchievements plugin; only its sign-in is used by the runtime.
inline constexpr const char* ACHIEVEMENTS_PLUGIN = "emuden.retroachievements";
/// The secret (den.secrets of that plugin) holding {"user", "token"}; never the password.
inline constexpr const char* ACHIEVEMENTS_LOGIN_SECRET = "login";

struct AchievementOptions {
    bool hardcore = false;   // no state loading, rewind, cheats or slow motion
    bool sound = true;       // a chime on unlock
    bool screenshot = false; // a PNG of the game on unlock
};

struct AchievementUser {
    std::string name, display_name, avatar_url;
    std::uint32_t score = 0, score_softcore = 0, unread_messages = 0;
};

struct AchievementItem {
    std::uint32_t id = 0, points = 0;
    std::string title, description, badge_url;
    bool unlocked = false;
    std::string progress; // "3/10"
    float percent = 0, rarity = 0;
};

struct AchievementGame {
    std::string library_id, name;      // the library's
    std::string title, image_url;      // RetroAchievements'
    std::uint32_t id = 0;              // 0: RetroAchievements does not know this version
    std::uint32_t total = 0, unlocked = 0, points_total = 0, points_unlocked = 0;
    bool hardcore = false, mastered = false, playing = false;
    std::string note; // why there is nothing to show ("This console has no achievements")
    std::vector<AchievementItem> achievements;
};

class AchievementServices {
public:
    virtual ~AchievementServices() = default;
    /// Blocking. Signs the runtime in; the token comes back for the plugin's secret.
    virtual Result<std::string> SignIn(const std::string& user, const std::string& password) = 0;
    virtual void SignOut() = 0;
    virtual std::optional<AchievementUser> User() = 0;
    /// A sign-in with a kept token is still under way.
    virtual bool SigningIn() = 0;
    virtual AchievementOptions Options() = 0;
    virtual void SetOptions(const AchievementOptions& options) = 0;
    /// Blocking: hashes the game and asks the server (the running game's from the runtime).
    virtual Result<AchievementGame> Game(const std::string& library_id) = 0;
    /// The library id of the game running now, or empty.
    virtual std::string Playing() = 0;
};

} // namespace den::plugins
