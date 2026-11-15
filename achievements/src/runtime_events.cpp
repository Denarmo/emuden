// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What rc_client tells while the game runs, turned into events; the game's memory as rcheevos
// reads it; the loaded game's achievements for the pages.

#include <algorithm>
#include <format>

#include "runtime_impl.h"

namespace den::achievements {

namespace {

Runtime::Impl& Of(const rc_client_t* client) {
    return *static_cast<Runtime::Impl*>(rc_client_get_userdata(client));
}

std::string Text(const char* text) {
    return text ? text : "";
}

Event FromAchievement(EventKind kind, const rc_client_achievement_t& a) {
    Event event;
    event.kind = kind;
    event.id = a.id;
    event.title = Text(a.title);
    event.text = Text(a.description);
    event.badge_url = Text(a.badge_url);
    event.points = a.points;
    event.value = a.measured_progress;
    event.percent = a.measured_percent;
    return event;
}

} // namespace

Achievement ToAchievement(const rc_client_achievement_t& from) {
    Achievement a;
    a.id = from.id;
    a.title = Text(from.title);
    a.description = Text(from.description);
    a.badge_url = Text(from.badge_url);
    a.badge_locked_url = Text(from.badge_locked_url);
    a.points = from.points;
    a.unlocked = from.state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
    a.unsupported = from.state == RC_CLIENT_ACHIEVEMENT_STATE_DISABLED;
    a.unofficial = from.category == RC_CLIENT_ACHIEVEMENT_CATEGORY_UNOFFICIAL;
    a.unlock_time = a.unlocked ? static_cast<std::int64_t>(from.unlock_time) : 0;
    a.progress = from.measured_progress;
    a.percent = from.measured_percent;
    a.rarity = from.rarity;
    return a;
}

void RC_CCONV HandleEvent(const rc_client_event_t* event, rc_client_t* client) {
    Runtime::Impl& self = Of(client);
    const bool hardcore = rc_client_get_hardcore_enabled(client) != 0;
    Event out;
    switch (event->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        out = FromAchievement(EventKind::Unlocked, *event->achievement);
        break;
    case RC_CLIENT_EVENT_GAME_COMPLETED: {
        out.kind = EventKind::GameCompleted;
        const rc_client_game_t* game = rc_client_get_game_info(client);
        rc_client_user_game_summary_t summary{};
        rc_client_get_user_game_summary(client, &summary);
        out.id = game ? game->id : 0;
        out.title = game ? Text(game->title) : std::string();
        out.badge_url = game ? Text(game->badge_url) : std::string();
        out.points = summary.points_unlocked;
        out.text = std::format("{} achievements, {} points", summary.num_unlocked_achievements,
                               summary.points_unlocked);
        break;
    }
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
        out = FromAchievement(EventKind::ProgressShow, *event->achievement);
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        out.kind = EventKind::ProgressHide;
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
        out = FromAchievement(EventKind::ChallengeShow, *event->achievement);
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
        out = FromAchievement(EventKind::ChallengeHide, *event->achievement);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        out.kind = event->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED  ? EventKind::LeaderboardStarted
                   : event->type == RC_CLIENT_EVENT_LEADERBOARD_FAILED ? EventKind::LeaderboardFailed
                                                                       : EventKind::LeaderboardSubmitted;
        out.id = event->leaderboard->id;
        out.title = Text(event->leaderboard->title);
        out.text = Text(event->leaderboard->description);
        out.value = Text(event->leaderboard->tracker_value);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD: {
        const rc_client_leaderboard_scoreboard_t& board = *event->leaderboard_scoreboard;
        out.kind = EventKind::LeaderboardRank;
        out.id = board.leaderboard_id;
        out.title = event->leaderboard ? Text(event->leaderboard->title) : std::string();
        out.value = std::format("{} of {}", board.new_rank, board.num_entries);
        out.text = board.best_score;
        break;
    }
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
        out.kind = event->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW   ? EventKind::TrackerShow
                   : event->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE ? EventKind::TrackerHide
                                                                             : EventKind::TrackerUpdate;
        out.id = event->leaderboard_tracker->id;
        out.value = event->leaderboard_tracker->display;
        break;
    case RC_CLIENT_EVENT_RESET:
        out.kind = EventKind::ResetWanted;
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        out.kind = EventKind::ServerError;
        out.text = event->server_error ? Text(event->server_error->error_message) : std::string();
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        out.kind = EventKind::Disconnected;
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        out.kind = EventKind::Reconnected;
        break;
    default:
        return;
    }
    out.hardcore = hardcore;
    self.events.push_back(std::move(out));
}

uint32_t RC_CCONV ReadMemory(uint32_t address, uint8_t* buffer, uint32_t bytes, rc_client_t* client) {
    const Runtime::Impl& self = Of(client);
    if (!self.regions_ready) {
        return 0;
    }
    return rc_libretro_memory_read(&self.regions, address, buffer, bytes);
}

std::optional<GameProgress> Runtime::Progress() const {
    rc_client_t* client = impl_->client;
    if (impl_->load != LoadState::Loaded || !rc_client_is_game_loaded(client)) {
        return std::nullopt;
    }
    const rc_client_game_t* info = rc_client_get_game_info(client);
    GameProgress game;
    game.id = info->id;
    game.title = Text(info->title);
    game.hash = Text(info->hash);
    game.image_url = Text(info->badge_url);
    game.hardcore = rc_client_get_hardcore_enabled(client) != 0;
    rc_client_achievement_list_t* list = rc_client_create_achievement_list(
        client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (list) {
        for (std::uint32_t b = 0; b < list->num_buckets; ++b) {
            const rc_client_achievement_bucket_t& bucket = list->buckets[b];
            for (std::uint32_t i = 0; i < bucket.num_achievements; ++i) {
                game.achievements.push_back(ToAchievement(*bucket.achievements[i]));
            }
        }
        rc_client_destroy_achievement_list(list);
    }
    // Locked first (nearest to done first), then unlocked (latest first), then unsupported.
    const auto rank = [](const Achievement& a) { return a.unsupported ? 2 : a.unlocked ? 1 : 0; };
    std::stable_sort(game.achievements.begin(), game.achievements.end(), [&](const Achievement& a, const Achievement& b) {
        if (rank(a) != rank(b)) {
            return rank(a) < rank(b);
        }
        if (rank(a) == 0) {
            return a.percent > b.percent;
        }
        return a.unlock_time > b.unlock_time;
    });
    rc_client_user_game_summary_t summary{};
    rc_client_get_user_game_summary(client, &summary);
    game.total = summary.num_core_achievements;
    game.unlocked = summary.num_unlocked_achievements;
    game.points_total = summary.points_core;
    game.points_unlocked = summary.points_unlocked;
    game.mastered = game.total > 0 && game.unlocked == game.total;
    return game;
}

std::string Runtime::RichPresence() const {
    if (impl_->load != LoadState::Loaded || !rc_client_has_rich_presence(impl_->client)) {
        return {};
    }
    char text[256] = {};
    rc_client_get_rich_presence_message(impl_->client, text, sizeof(text));
    return text;
}

} // namespace den::achievements
