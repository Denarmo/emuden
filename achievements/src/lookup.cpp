// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Blocking lookups for pages outside a game, through rcheevos' request builders and
// response parsers: the game id of a hash, and a game's achievements with the user's unlocks.

#include <algorithm>
#include <set>

#include "den/achievements/games.h"
#include "rc_api_runtime.h"
#include "rc_api_user.h"
#include "rc_error.h"

namespace den::achievements {

namespace {

constexpr const char* NO_ANSWER = "RetroAchievements could not be reached.";

struct Sent {
    HttpResponse response;
    rc_api_server_response_t server{};
};

// Sends a built request and frees it.
Sent Send(const HttpFn& http, rc_api_request_t& request) {
    HttpRequest out;
    out.url = request.url ? request.url : "";
    out.post_data = request.post_data ? request.post_data : "";
    out.content_type = request.content_type ? request.content_type : "";
    rc_api_destroy_request(&request);
    Sent sent;
    sent.response = http(out);
    sent.server.body = sent.response.body.c_str();
    sent.server.body_length = sent.response.body.size();
    sent.server.http_status_code =
        sent.response.status == 0 ? RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR : sent.response.status;
    return sent;
}

std::string Why(const rc_api_response_t& response, int result) {
    if (response.error_message && *response.error_message) {
        return response.error_message;
    }
    return result == RC_OK ? NO_ANSWER : std::string("RetroAchievements: ") + rc_error_str(result);
}

rc_api_host_t HostOf(const std::string& host) {
    rc_api_host_t out{};
    out.host = host.empty() ? nullptr : host.c_str();
    return out;
}

Result<std::set<std::uint32_t>> Unlocks(const HttpFn& http, const rc_api_host_t& host, const Credentials& user,
                                        std::uint32_t game, bool hardcore) {
    rc_api_fetch_user_unlocks_request_t params{};
    params.username = user.user.c_str();
    params.api_token = user.token.c_str();
    params.game_id = game;
    params.hardcore = hardcore ? 1 : 0;
    rc_api_request_t request{};
    if (rc_api_init_fetch_user_unlocks_request_hosted(&request, &params, &host) != RC_OK) {
        return Status::Fail("The request could not be made.");
    }
    const Sent sent = Send(http, request);
    rc_api_fetch_user_unlocks_response_t response{};
    const int result = rc_api_process_fetch_user_unlocks_server_response(&response, &sent.server);
    if (result != RC_OK || !response.response.succeeded) {
        const std::string why = Why(response.response, result);
        rc_api_destroy_fetch_user_unlocks_response(&response);
        return Status::Fail(why);
    }
    std::set<std::uint32_t> ids(response.achievement_ids, response.achievement_ids + response.num_achievement_ids);
    rc_api_destroy_fetch_user_unlocks_response(&response);
    return ids;
}

} // namespace

Result<std::uint32_t> ResolveHash(const HttpFn& http, const std::string& host_name, const std::string& hash) {
    const rc_api_host_t host = HostOf(host_name);
    rc_api_resolve_hash_request_t params{};
    params.game_hash = hash.c_str();
    rc_api_request_t request{};
    if (rc_api_init_resolve_hash_request_hosted(&request, &params, &host) != RC_OK) {
        return Status::Fail("The request could not be made.");
    }
    const Sent sent = Send(http, request);
    rc_api_resolve_hash_response_t response{};
    const int result = rc_api_process_resolve_hash_server_response(&response, &sent.server);
    if (result != RC_OK || !response.response.succeeded) {
        const std::string why = Why(response.response, result);
        rc_api_destroy_resolve_hash_response(&response);
        return Status::Fail(why);
    }
    const std::uint32_t id = response.game_id;
    rc_api_destroy_resolve_hash_response(&response);
    return id;
}

Result<GameProgress> FetchProgress(const HttpFn& http, const std::string& host_name, const Credentials& user,
                                   const std::string& hash, bool hardcore) {
    const rc_api_host_t host = HostOf(host_name);
    rc_api_fetch_game_sets_request_t params{};
    params.username = user.user.c_str();
    params.api_token = user.token.c_str();
    params.game_hash = hash.c_str();
    rc_api_request_t request{};
    if (rc_api_init_fetch_game_sets_request_hosted(&request, &params, &host) != RC_OK) {
        return Status::Fail("The request could not be made.");
    }
    const Sent sent = Send(http, request);
    rc_api_fetch_game_sets_response_t response{};
    const int result = rc_api_process_fetch_game_sets_server_response(&response, &sent.server);
    if (result != RC_OK || !response.response.succeeded) {
        const std::string why = Why(response.response, result);
        rc_api_destroy_fetch_game_sets_response(&response);
        if (result == RC_NOT_FOUND || why.find("Unknown game") != std::string::npos) {
            GameProgress unknown;
            unknown.hash = hash;
            return unknown;
        }
        return Status::Fail(why);
    }
    GameProgress game;
    game.id = response.id;
    game.title = response.title ? response.title : "";
    game.image_url = response.image_url ? response.image_url : "";
    game.hash = hash;
    game.hardcore = hardcore;
    for (std::uint32_t s = 0; s < response.num_sets; ++s) {
        const rc_api_achievement_set_definition_t& set = response.sets[s];
        for (std::uint32_t i = 0; i < set.num_achievements; ++i) {
            const rc_api_achievement_definition_t& def = set.achievements[i];
            if (def.category != RC_ACHIEVEMENT_CATEGORY_CORE) {
                continue;
            }
            Achievement a;
            a.id = def.id;
            a.title = def.title ? def.title : "";
            a.description = def.description ? def.description : "";
            a.badge_url = def.badge_url ? def.badge_url : "";
            a.badge_locked_url = def.badge_locked_url ? def.badge_locked_url : "";
            a.points = def.points;
            a.rarity = hardcore ? def.rarity_hardcore : def.rarity;
            game.achievements.push_back(std::move(a));
        }
    }
    const std::uint32_t game_id = response.session_game_id ? response.session_game_id : response.id;
    rc_api_destroy_fetch_game_sets_response(&response);

    if (!game.achievements.empty()) {
        Result<std::set<std::uint32_t>> unlocked = Unlocks(http, host, user, game_id, hardcore);
        if (!unlocked) {
            return unlocked.status();
        }
        for (Achievement& a : game.achievements) {
            a.unlocked = unlocked->contains(a.id);
        }
    }
    std::stable_sort(game.achievements.begin(), game.achievements.end(),
                     [](const Achievement& a, const Achievement& b) { return !a.unlocked && b.unlocked; });
    for (const Achievement& a : game.achievements) {
        ++game.total;
        game.points_total += a.points;
        if (a.unlocked) {
            ++game.unlocked;
            game.points_unlocked += a.points;
        }
    }
    game.mastered = game.total > 0 && game.unlocked == game.total;
    return game;
}

} // namespace den::achievements
