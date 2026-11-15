// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// RetroAchievements while a game plays: rcheevos' rc_client signed in with the user's token,
// the game identified by its hash, its achievements checked against the core's memory every
// emulated frame, and what happened (unlocks, progress, leaderboard attempts) as events for
// the screens. Main thread only; the server's answers are handed over in Pump.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "den/base/result.h"

struct retro_memory_map;

namespace den::achievements {

struct HttpRequest {
    std::string url;
    std::string post_data;    // empty: GET
    std::string content_type; // of post_data
};

struct HttpResponse {
    int status = 0; // 0: no answer (no network)
    std::string body;
};

/// Blocking; called on a helper thread.
using HttpFn = std::function<HttpResponse(const HttpRequest&)>;

struct Config {
    HttpFn http;
    /// Runs work off the main thread; default: a thread of its own. Tests run it inline.
    std::function<void(std::function<void()>)> spawn;
    std::string client_name = "EmuDEN"; // the User-Agent starts with "<client_name>/<version>"
    std::string client_version = "0.1";
    /// The server's address; empty for retroachievements.org.
    std::string host;
};

struct Credentials {
    std::string user, token;
};

struct User {
    std::string name, display_name;
    std::string avatar_url;
    std::uint32_t score = 0, score_softcore = 0;
    std::uint32_t unread_messages = 0;
};

struct Achievement {
    std::uint32_t id = 0;
    std::string title, description;
    std::string badge_url;        // the unlocked picture
    std::string badge_locked_url; // grey
    std::uint32_t points = 0;
    bool unlocked = false;        // in the mode in effect
    bool unsupported = false;     // this runtime cannot check it
    bool unofficial = false;
    std::int64_t unlock_time = 0; // Unix seconds, 0 when locked
    std::string progress;         // "12/50", or empty
    float percent = 0;            // 0..100 of progress
    float rarity = 0;             // % of players who have it
};

struct GameProgress {
    std::uint32_t id = 0;         // 0: the server does not know the game
    std::string title, hash;
    std::string image_url;
    std::uint32_t total = 0, unlocked = 0;
    std::uint32_t points_total = 0, points_unlocked = 0;
    bool hardcore = false;
    bool mastered = false;
    std::vector<Achievement> achievements; // locked first, then unlocked
};

enum class EventKind {
    Unlocked,             // achievement
    GameCompleted,        // every achievement of the game (title, text)
    ProgressShow,         // achievement (progress, percent)
    ProgressHide,
    ChallengeShow,        // achievement: a challenge that is live now
    ChallengeHide,        // achievement id
    LeaderboardStarted,   // title, text
    LeaderboardFailed,    // title
    LeaderboardSubmitted, // title, value: the score
    LeaderboardRank,      // title, value: "rank of entries"
    TrackerShow,          // id, value
    TrackerUpdate,        // id, value
    TrackerHide,          // id
    ServerError,          // text
    Disconnected,         // unlocks wait for the network
    Reconnected,
    ResetWanted,          // hardcore was turned on: the game must start again
};

struct Event {
    EventKind kind = EventKind::Unlocked;
    std::uint32_t id = 0;
    std::string title, text, value;
    std::string badge_url;
    std::uint32_t points = 0;
    float percent = 0;
    bool hardcore = false;
};

/// The game's memory as the core shows it. `map` (SET_MEMORY_MAPS) may be null; `region`
/// is retro_get_memory_data with its size. Both must stay valid while the game is loaded.
struct GameMemory {
    const retro_memory_map* map = nullptr;
    std::function<std::span<std::uint8_t>(unsigned id)> region;
};

enum class LoadState { None, Loading, Loaded, Unknown, Failed };

class Runtime {
public:
    explicit Runtime(Config config);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /// Hands over the server's answers and runs what waits on them. Every app frame.
    void Pump();

    void LoginWithToken(const Credentials& credentials, std::function<void(Status)> done);
    /// The password goes to the server once and is not kept; the token comes back.
    void LoginWithPassword(const std::string& user, const std::string& password,
                           std::function<void(Result<Credentials>)> done);
    void Logout();
    bool SignedIn() const;
    std::optional<User> UserInfo() const;

    void SetHardcore(bool on);
    bool Hardcore() const;
    void SetUnofficial(bool on);

    /// Starts a game by its RetroAchievements hash (HashFile). `done` says whether the server
    /// knows the game; memory is read from `memory` from then on.
    void LoadGame(std::uint32_t console, const std::string& hash, GameMemory memory,
                  std::function<void(Status)> done = {});
    void UnloadGame();
    LoadState GameState() const;
    /// The loaded game with its achievements and the user's unlocks.
    std::optional<GameProgress> Progress() const;
    /// What the game shows of the player now ("Lap 2 of 3, 1st place"), or empty.
    std::string RichPresence() const;
    /// The core's memory may have moved (a core announced a new map): read it again.
    void RefreshMemory();

    /// After every emulated frame.
    void DoFrame();
    /// Once per app frame while the game is paused (keeps the session alive).
    void Idle();
    /// The game was reset, or a state was loaded: what was being counted starts again.
    void Reset();
    void StateLoaded();

    std::vector<Event> TakeEvents();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace den::achievements
