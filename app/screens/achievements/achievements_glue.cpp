// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The runtime's life in the app: created at start, signed in with the plugin's kept token
// while the plugin is installed, and following the game the player runs: identified by its
// hash on a job, then checked after every emulated frame through the session's frame hook.

#include <format>
#include <future>

#include "../plugins/feature_api.h"
#include "achievements_glue.h"
#include "achievements_state.h"
#include "den/achievements/games.h"
#include "den/app/registry.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/system/info.h"

namespace den::app {

namespace {

constexpr const char* KEY_HARDCORE = "achievements.hardcore";
constexpr const char* KEY_SOUND = "achievements.sound";
constexpr const char* KEY_SCREENSHOT = "achievements.screenshot";

bool PluginInstalled() {
    plugins::FeaturePlugin plugin;
    return FindFeaturePlugin(plugins::ACHIEVEMENTS_PLUGIN, plugin) && plugin.installed;
}

void SignInWithKeptToken() {
    AchievementsState& s = Ach();
    const auto kept = pal::GetSecrets().Get(LoginSecretName());
    const auto value = kept ? ParseJson(*kept) : std::nullopt;
    if (!value) {
        return;
    }
    const achievements::Credentials login{value->String("user"), value->String("token")};
    if (login.user.empty() || login.token.empty()) {
        return;
    }
    {
        std::lock_guard lock(s.mutex);
        s.signing_in = true;
    }
    s.runtime->LoginWithToken(login, [login](Status status) {
        AchievementsState& state = Ach();
        std::lock_guard lock(state.mutex);
        state.signing_in = false;
        if (status) {
            state.login = login;
        } else {
            log::Warn("achievements", "the kept sign-in was refused: {}", status.error);
        }
    });
}

void Unfollow() {
    AchievementsState& s = Ach();
    if (s.hooked) {
        Svc().player->SetFrameHook({});
    }
    if (!s.game_id.empty()) {
        s.runtime->UnloadGame();
    }
    s.game_id.clear();
    s.hooked = false;
    s.loaded_told = false;
    s.popups.clear();
    s.progress.reset();
    s.challenges.clear();
    s.trackers.clear();
    std::lock_guard lock(s.mutex);
    s.playing_id.clear();
}

void Loaded(const std::string& id, const Status& status) {
    AchievementsState& s = Ach();
    if (s.game_id != id) {
        return;
    }
    if (!status) {
        log::Info("achievements", "{}: {}", s.game.name, status.error);
        // A game without achievements keeps nothing from hardcore.
        Svc().player->SetHardcore(false);
        return;
    }
    const auto progress = s.runtime->Progress();
    if (progress && !s.loaded_told) {
        s.loaded_told = true;
        const std::string mode = s.runtime->Hardcore() ? "Hardcore" : "Softcore";
        Svc().stage->Toast(progress->total == 0
                               ? progress->title + ": no achievements yet"
                               : std::format("{}: {} of {} achievements ({})", progress->title, progress->unlocked,
                                             progress->total, mode),
                           4.0f);
    }
}

void Follow(const player::Snapshot& snapshot) {
    AchievementsState& s = Ach();
    s.game_id = snapshot.game_id;
    const auto entry = Svc().library->Find(snapshot.game_id);
    s.console = entry ? RaConsole(entry->system) : 0;
    if (!entry || s.console == 0 || snapshot.external) {
        return; // nothing to check: no achievements for the console, or an engine app's memory
    }
    s.game = *entry;
    {
        std::lock_guard lock(s.mutex);
        s.playing_id = s.game_id;
    }
    player::Session& session = *Svc().player;
    session.SetFrameHook([](player::FrameEvent event) {
        achievements::Runtime& runtime = *Ach().runtime;
        switch (event) {
        case player::FrameEvent::Frame:
            runtime.DoFrame();
            break;
        case player::FrameEvent::Reset:
            runtime.Reset();
            break;
        case player::FrameEvent::StateLoaded:
            runtime.StateLoaded();
            break;
        case player::FrameEvent::MemoryMoved:
            runtime.RefreshMemory();
            break;
        }
    });
    s.hooked = true;
    const std::filesystem::path content = session.ContentPath();
    const std::uint32_t console = s.console;
    const std::string id = s.game_id;
    auto hash = std::make_shared<std::string>();
    Svc().jobs->Run(
        "Identifying the game for RetroAchievements",
        [content, console, hash](Job&) -> Status {
            auto made = achievements::HashFile(console, content);
            if (!made) {
                return made.status();
            }
            *hash = made.value();
            return {};
        },
        [id, console, hash](Job& job) {
            AchievementsState& state = Ach();
            if (state.game_id != id) {
                return;
            }
            if (job.State() != JobState::Done) {
                log::Warn("achievements", "{}: {}", state.game.name, job.Error());
                Svc().player->SetHardcore(false);
                return;
            }
            log::Info("achievements", "{} has hash {}", state.game.name, *hash);
            achievements::GameMemory memory;
            memory.map = Svc().player->MemoryMap();
            memory.region = [](unsigned region) { return Svc().player->MemoryRegion(region); };
            state.runtime->LoadGame(console, *hash, std::move(memory),
                                    [id](Status status) { Loaded(id, status); });
        });
}

void Startup() {
    AchievementsState& s = Ach();
    achievements::Config config;
    config.client_version = system::Version();
    config.host = ServerHost();
    config.http = achievements::PlatformHttp(achievements::UserAgent(config.client_name, config.client_version));
    s.runtime = std::make_unique<achievements::Runtime>(std::move(config));
    store::Settings& settings = *Svc().settings;
    std::lock_guard lock(s.mutex);
    s.options.hardcore = settings.GetBool(KEY_HARDCORE, false);
    s.options.sound = settings.GetBool(KEY_SOUND, true);
    s.options.screenshot = settings.GetBool(KEY_SCREENSHOT, false);
    s.runtime->SetHardcore(s.options.hardcore);
}

void EveryFrame() {
    AchievementsState& s = Ach();
    if (!s.runtime) {
        return;
    }
    std::deque<std::function<void()>> work;
    {
        std::lock_guard lock(s.mutex);
        work.swap(s.posted);
    }
    for (auto& fn : work) {
        fn();
    }
    const double now = pal::NowSeconds();
    if (now >= s.next_plugin_check) {
        // The plugin is the switch: without it the runtime is signed out.
        s.next_plugin_check = now + 2.0;
        const bool installed = PluginInstalled();
        if (!s.started && installed) {
            s.started = true;
            SignInWithKeptToken();
        } else if (s.started && !installed) {
            s.started = false;
            Unfollow();
            s.runtime->Logout();
            std::lock_guard lock(s.mutex);
            s.login = {};
        }
    }
    s.runtime->Pump();

    const player::Snapshot snapshot = Svc().player->GetSnapshot();
    const bool running = snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
    if (!s.game_id.empty() && (!running || snapshot.game_id != s.game_id)) {
        Unfollow();
    }
    if (running && s.game_id.empty() && s.runtime->SignedIn()) {
        Follow(snapshot);
    }
    if (running && snapshot.hardcore && s.game_id.empty() && !s.runtime->SignedIn()) {
        std::lock_guard lock(s.mutex);
        if (!s.signing_in) {
            Svc().player->SetHardcore(false); // the kept sign-in was refused: nothing to keep strict for
        }
    }
    if (running && snapshot.state == player::State::Paused) {
        s.runtime->Idle();
    }
    for (const achievements::Event& event : s.runtime->TakeEvents()) {
        if (event.kind == achievements::EventKind::ResetWanted) {
            Svc().player->SetHardcore(true);
            Svc().player->Reset();
        }
        ShowEvent(event);
    }
}

} // namespace

AchievementsState& Ach() {
    static AchievementsState state;
    return state;
}

void PostToMain(std::function<void()> work) {
    AchievementsState& s = Ach();
    std::lock_guard lock(s.mutex);
    s.posted.push_back(std::move(work));
}

void RunOnMain(const std::function<void()>& work) {
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> wait = done->get_future();
    PostToMain([&work, done] {
        work();
        done->set_value();
    });
    wait.wait();
}

std::uint32_t RaConsole(const std::string& system) {
    // The console's manifest says (achievements.rcheevos_console; 0 when its capabilities
    // say no achievements).
    const auto console = Svc().plugins ? Svc().plugins->FindConsole(system) : std::nullopt;
    return console && console->rcheevos_console > 0 ? static_cast<std::uint32_t>(console->rcheevos_console) : 0;
}

std::string ServerHost() {
    return pal::GetEnv("EMUDEN_RA_HOST");
}

std::string LoginSecretName() {
    return std::string("plugin/") + plugins::ACHIEVEMENTS_PLUGIN + "/" + plugins::ACHIEVEMENTS_LOGIN_SECRET;
}

void SaveOptions(const plugins::AchievementOptions& options) {
    store::Settings& settings = *Svc().settings;
    settings.SetBool(KEY_HARDCORE, options.hardcore);
    settings.SetBool(KEY_SOUND, options.sound);
    settings.SetBool(KEY_SCREENSHOT, options.screenshot);
}

void ApplyHardcore(bool on) {
    AchievementsState& s = Ach();
    if (s.runtime->Hardcore() == on) {
        return;
    }
    // Turning it on while a game runs makes rcheevos ask for a reset (ResetWanted).
    s.runtime->SetHardcore(on);
    if (!on) {
        Svc().player->SetHardcore(false);
    }
}

bool AchievementsHardcore(const library::Entry& game) {
    AchievementsState& s = Ach();
    if (!s.runtime || RaConsole(game.system) == 0) {
        return false;
    }
    std::lock_guard lock(s.mutex);
    return s.options.hardcore && (!s.login.user.empty() || s.signing_in);
}

bool AchievementsForRunningGame() {
    AchievementsState& s = Ach();
    return s.runtime && !s.game_id.empty() && s.runtime->GameState() == achievements::LoadState::Loaded;
}

DEN_REGISTER_STARTUP(Startup);
DEN_REGISTER_FRAME(EveryFrame);

} // namespace den::app
