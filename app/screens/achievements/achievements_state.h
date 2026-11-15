// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The state shared by the files of app/screens/achievements. Main thread, except Post and
// the credentials, which helper threads (den.achievements) use.

#pragma once

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "den/achievements/runtime.h"
#include "den/library/library.h"
#include "den/plugins/feature_achievements.h"

namespace den::app {

struct Popup {
    achievements::Event event;
    std::string heading; // "Achievement unlocked"
    double until = 0;    // filled in when it is first drawn
    double seconds = 5;
};

struct AchievementsState {
    std::unique_ptr<achievements::Runtime> runtime;

    // From any thread.
    std::mutex mutex;
    achievements::Credentials login; // empty user: signed out
    bool signing_in = false;
    plugins::AchievementOptions options;
    std::string playing_id;
    std::deque<std::function<void()>> posted;

    // The game the runtime follows.
    std::string game_id;   // library id; empty when none
    library::Entry game;
    std::uint32_t console = 0;
    bool hooked = false;
    bool loaded_told = false;
    double next_plugin_check = 0;
    bool started = false;

    // What is drawn over the game.
    std::deque<Popup> popups;
    std::optional<achievements::Event> progress;
    double progress_until = 0;
    std::map<std::uint32_t, achievements::Event> challenges;
    std::map<std::uint32_t, std::string> trackers;
};

AchievementsState& Ach();

/// Runs `work` on the main thread, in the next frame.
void PostToMain(std::function<void()> work);
/// Runs `work` on the main thread and waits for it (helper threads only).
void RunOnMain(const std::function<void()>& work);

/// achievements_overlay.cpp: what an event shows, sounds and saves.
void ShowEvent(const achievements::Event& event);
/// A local copy of a badge picture, empty while it is fetched.
std::string BadgeFile(const std::string& url);

/// achievements_glue.cpp
std::string LoginSecretName();
/// The RetroAchievements console of a system: the console plugin's "achievements" when it
/// names one, else EmuDEN's own table; 0 for none. Main thread.
std::uint32_t RaConsole(const std::string& system);
/// The server: retroachievements.org, or EMUDEN_RA_HOST (a test server, "http://127.0.0.1:port").
std::string ServerHost();
void SaveOptions(const plugins::AchievementOptions& options);
void ApplyHardcore(bool on);

} // namespace den::app
