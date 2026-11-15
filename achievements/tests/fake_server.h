// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A RetroAchievements server in memory: answers by the request's "r=" name, records what
// was asked. The answers are shaped as rcheevos' own tests have them.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "den/achievements/runtime.h"

namespace den::achievements::test {

struct FakeServer {
    std::map<std::string, HttpResponse> answers; // by API name ("login2")
    std::vector<HttpRequest> asked;

    static std::string Api(const HttpRequest& request) {
        const std::string& text = request.post_data.empty() ? request.url : request.post_data;
        const auto at = text.find("r=");
        if (at == std::string::npos) {
            return {};
        }
        const auto end = text.find('&', at);
        return text.substr(at + 2, end == std::string::npos ? std::string::npos : end - at - 2);
    }

    HttpFn Fn() {
        return [this](const HttpRequest& request) {
            asked.push_back(request);
            const auto found = answers.find(Api(request));
            return found == answers.end() ? HttpResponse{404, R"({"Success":false,"Error":"not here"})"}
                                          : found->second;
        };
    }

    int Count(const std::string& api) const {
        int n = 0;
        for (const HttpRequest& request : asked) {
            n += Api(request) == api ? 1 : 0;
        }
        return n;
    }
};

inline const char* LOGIN_OK =
    R"({"Success":true,"User":"Tester","Token":"ApiToken","Score":12345,"SoftcoreScore":123,"Messages":2,"Permissions":1,"AccountType":"Registered"})";

// Two achievements and a leaderboard on the NES's work RAM: 0x0001 = 3 and 0x0002 = 7
// unlocks the first; the leaderboard starts when 0x000C is 1.
inline const char* GAME_DATA =
    "{\"Success\":true,\"GameId\":1234,\"Title\":\"Sample Game\",\"ConsoleId\":7,"
    "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/112233.png\","
    "\"RichPresenceGameId\":1234,\"RichPresencePatch\":\"Display:\\r\\nLevel @Number(0xH0001)\",\"Sets\":[{"
    "\"AchievementSetId\":1111,\"GameId\":1234,\"Title\":null,\"Type\":\"core\","
    "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/112233.png\","
    "\"Achievements\":["
    "{\"ID\":5501,\"Title\":\"First steps\",\"Description\":\"Reach level 3\",\"Flags\":3,\"Points\":5,"
    "\"MemAddr\":\"0xH0001=3_0xH0002=7\",\"Author\":\"User1\",\"BadgeName\":\"00234\","
    "\"Created\":1367266583,\"Modified\":1376929305},"
    "{\"ID\":5502,\"Title\":\"Collector\",\"Description\":\"Collect 9\",\"Flags\":3,\"Points\":10,"
    "\"MemAddr\":\"0xH0001=2_0xH0003=9\",\"Author\":\"User1\",\"BadgeName\":\"00235\","
    "\"Created\":1376970283,\"Modified\":1376970283}"
    "],"
    "\"Leaderboards\":["
    "{\"ID\":4401,\"Title\":\"Fastest\",\"Description\":\"Finish fast\","
    "\"Mem\":\"STA:0xH000C=1::CAN:0xH000D=1::SUB:0xH000D=2::VAL:0x 000E\",\"Format\":\"SCORE\"}"
    "]}]}";

inline const char* NO_UNLOCKS = R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[],"ServerNow":1700000000})";
inline const char* NOT_FOUND = R"({"Success":false,"Error":"Unknown game","Code":"not_found","Status":404})";
inline const char* AWARD_OK =
    R"({"Success":true,"Score":12350,"SoftcoreScore":123,"AchievementID":5501,"AchievementsRemaining":1})";

} // namespace den::achievements::test
