// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <ctime>
#include <format>
#include <sstream>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/app/games.h"
#include "den/app/strings.h"

namespace den::app {

namespace {

constexpr const char* HIDDEN_KEY = "library.hidden";
std::uint64_t g_hidden_revision = 0;

std::int64_t LocalDay(std::time_t time) {
    const std::tm* local = std::localtime(&time);
    return local ? static_cast<std::int64_t>(local->tm_year) * 366 + local->tm_yday : 0;
}

} // namespace

bool IsHidden(const std::string& id) {
    const std::vector<std::string> hidden = Svc().settings->GetList(HIDDEN_KEY);
    return std::find(hidden.begin(), hidden.end(), id) != hidden.end();
}

void SetHidden(const std::string& id, bool hidden) {
    std::vector<std::string> list = Svc().settings->GetList(HIDDEN_KEY);
    std::erase(list, id);
    if (hidden) {
        list.push_back(id);
    }
    Svc().settings->SetList(HIDDEN_KEY, list);
    ++g_hidden_revision;
}

int HiddenCount() {
    return static_cast<int>(Svc().settings->GetList(HIDDEN_KEY).size());
}

void ShowAllHidden() {
    Svc().settings->Remove(HIDDEN_KEY);
    ++g_hidden_revision;
}

std::vector<library::Entry> WithoutHidden(std::vector<library::Entry> games) {
    const std::vector<std::string> hidden = Svc().settings->GetList(HIDDEN_KEY);
    if (!hidden.empty()) {
        std::erase_if(games, [&](const library::Entry& game) {
            return std::find(hidden.begin(), hidden.end(), game.id) != hidden.end();
        });
    }
    return games;
}

std::uint64_t GamesRevision() {
    return Svc().library->Revision() * 1000003u + Svc().plugins->Revision() * 1009u + g_hidden_revision;
}

void TouchGames() {
    ++g_hidden_revision;
}

std::string SystemName(const std::string& system) {
    if (const auto console = Svc().plugins->FindConsole(system); console && !console->name.empty()) {
        return console->name;
    }
    for (const library::SystemInfo& info : Svc().library->Systems()) {
        if (info.id == system && !info.name.empty()) {
            return info.name;
        }
    }
    return system;
}

std::string SystemShortName(const std::string& system) {
    // The library's table has the real short names ("GBA"); a plugin without one repeats
    // its full name.
    const std::string full = SystemName(system);
    for (const library::SystemInfo& info : Svc().library->Systems()) {
        if (info.id == system && !info.short_name.empty() && info.short_name != info.name) {
            return info.short_name;
        }
    }
    if (const auto console = Svc().plugins->FindConsole(system); console && !console->short_name.empty()) {
        return console->short_name;
    }
    return full;
}

std::string PlayTimeText(std::int64_t seconds) {
    const std::int64_t minutes = seconds / 60;
    if (minutes < 1) {
        return Tr("time.under_minute");
    }
    if (minutes < 60) {
        return TrF("time.minutes", minutes);
    }
    return TrF("time.hours_minutes", minutes / 60, minutes % 60);
}

std::string LastPlayedText(std::int64_t unix_time) {
    if (unix_time <= 0) {
        return Tr("time.never");
    }
    const std::time_t time = static_cast<std::time_t>(unix_time);
    const std::int64_t days = LocalDay(std::time(nullptr)) - LocalDay(time);
    if (days <= 0) {
        return Tr("time.today");
    }
    if (days == 1) {
        return Tr("time.yesterday");
    }
    if (days < 30) {
        return TrF("time.days_ago", days);
    }
    const std::tm* local = std::localtime(&time);
    return local ? std::format("{:04}-{:02}-{:02}", local->tm_year + 1900, local->tm_mon + 1, local->tm_mday)
                 : std::string();
}

std::string SizeText(std::uint64_t bytes) {
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mib >= 1024.0) {
        return std::format("{:.2f} GiB", mib / 1024.0);
    }
    if (mib >= 1.0) {
        return std::format("{:.1f} MiB", mib);
    }
    return std::format("{:.0f} KiB", static_cast<double>(bytes) / 1024.0);
}

std::string RecordsLine(const library::Entry& game) {
    const std::string dot = "  \xC2\xB7  ";
    std::string line = SystemName(game.system);
    if (game.last_played <= 0 && game.play_seconds <= 0) {
        return line + dot + Tr("records.not_played");
    }
    if (game.play_seconds > 0) {
        line += dot + TrF("records.played", PlayTimeText(game.play_seconds));
    }
    if (game.last_played > 0) {
        line += dot + TrF("records.last_played", LastPlayedText(game.last_played));
    }
    return line;
}

std::string ClockText() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    return local ? std::format("{:02}:{:02}", local->tm_hour, local->tm_min) : std::string();
}

bool Matches(const library::Entry& game, const std::string& query) {
    const std::string haystack = ToLower(game.name) + " " + ToLower(SystemName(game.system)) + " " +
                                 ToLower(SystemShortName(game.system));
    std::istringstream words(ToLower(query));
    std::string word;
    while (words >> word) {
        if (haystack.find(word) == std::string::npos) {
            return false;
        }
    }
    return true;
}

} // namespace den::app
