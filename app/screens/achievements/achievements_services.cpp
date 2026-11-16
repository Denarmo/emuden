// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.achievements over the app's runtime. The plugin calls from a helper thread; what the
// runtime does runs on the main thread (PostToMain) while the helper waits.

#include <future>

#include "den/app/achievements_glue.h"
#include "achievements_state.h"
#include "den/achievements/games.h"
#include "den/base/files.h"
#include "den/library/content.h"
#include "den/system/info.h"

namespace den::app {

namespace {

plugins::AchievementItem ToItem(const achievements::Achievement& a) {
    plugins::AchievementItem item;
    item.id = a.id;
    item.points = a.points;
    item.title = a.title;
    item.description = a.description;
    item.badge_url = a.unlocked || a.badge_locked_url.empty() ? a.badge_url : a.badge_locked_url;
    item.unlocked = a.unlocked;
    item.progress = a.progress;
    item.percent = a.percent;
    item.rarity = a.rarity;
    return item;
}

plugins::AchievementGame ToGame(const achievements::GameProgress& from, const library::Entry& entry) {
    plugins::AchievementGame game;
    game.library_id = entry.id;
    game.name = entry.name;
    game.title = from.title;
    game.image_url = from.image_url;
    game.id = from.id;
    game.total = from.total;
    game.unlocked = from.unlocked;
    game.points_total = from.points_total;
    game.points_unlocked = from.points_unlocked;
    game.hardcore = from.hardcore;
    game.mastered = from.mastered;
    if (from.id == 0) {
        game.note = "RetroAchievements does not know this version of the game. Achievements are made for "
                    "particular dumps: another version of the game may have them.";
    } else if (from.total == 0) {
        game.note = "This game has no achievements yet.";
    }
    for (const achievements::Achievement& a : from.achievements) {
        if (!a.unsupported && !a.unofficial) {
            game.achievements.push_back(ToItem(a));
        }
    }
    return game;
}

class RuntimeServices final : public plugins::AchievementServices {
public:
    Result<std::string> SignIn(const std::string& user, const std::string& password) override {
        auto answer = std::make_shared<std::promise<Result<std::string>>>();
        std::future<Result<std::string>> wait = answer->get_future();
        {
            std::lock_guard lock(Ach().mutex);
            Ach().signing_in = true;
        }
        PostToMain([user, password, answer] {
            Ach().runtime->LoginWithPassword(user, password, [answer](Result<achievements::Credentials> got) {
                AchievementsState& s = Ach();
                std::lock_guard lock(s.mutex);
                s.signing_in = false;
                if (!got) {
                    answer->set_value(got.status());
                    return;
                }
                s.login = got.value();
                answer->set_value(got.value().token);
            });
        });
        if (wait.wait_for(std::chrono::seconds(90)) != std::future_status::ready) {
            return Status::Fail("RetroAchievements did not answer.");
        }
        return wait.get();
    }

    void SignOut() override {
        {
            std::lock_guard lock(Ach().mutex);
            Ach().login = {};
        }
        RunOnMain([] {
            Ach().runtime->Logout();
            Svc().player->SetHardcore(false);
        });
    }

    std::optional<plugins::AchievementUser> User() override {
        std::optional<achievements::User> user;
        RunOnMain([&] { user = Ach().runtime->UserInfo(); });
        if (!user) {
            return std::nullopt;
        }
        return plugins::AchievementUser{user->name,  user->display_name,   user->avatar_url,
                                        user->score, user->score_softcore, user->unread_messages};
    }

    bool SigningIn() override {
        std::lock_guard lock(Ach().mutex);
        return Ach().signing_in;
    }

    plugins::AchievementOptions Options() override {
        std::lock_guard lock(Ach().mutex);
        return Ach().options;
    }

    void SetOptions(const plugins::AchievementOptions& options) override {
        {
            std::lock_guard lock(Ach().mutex);
            Ach().options = options;
        }
        RunOnMain([options] {
            SaveOptions(options);
            ApplyHardcore(options.hardcore);
        });
    }

    std::string Playing() override {
        std::lock_guard lock(Ach().mutex);
        return Ach().playing_id;
    }

    Result<plugins::AchievementGame> Game(const std::string& id) override {
        std::optional<library::Entry> entry;
        std::optional<achievements::GameProgress> live;
        std::filesystem::path data_dir;
        std::uint32_t console = 0;
        RunOnMain([&] {
            entry = Svc().library->Find(id);
            console = entry ? RaConsole(entry->system) : 0;
            data_dir = Svc().data_dir;
            if (Ach().game_id == id) {
                live = Ach().runtime->Progress();
            }
        });
        if (!entry) {
            return Status::Fail("There is no such game.");
        }
        if (live) {
            plugins::AchievementGame game = ToGame(*live, *entry);
            game.playing = true;
            return game;
        }
        if (console == 0) {
            plugins::AchievementGame game;
            game.library_id = entry->id;
            game.name = entry->name;
            game.note = "RetroAchievements has no achievements for this console.";
            return game;
        }
        achievements::Credentials login;
        bool hardcore = false;
        {
            std::lock_guard lock(Ach().mutex);
            login = Ach().login;
            hardcore = Ach().options.hardcore;
        }
        if (login.user.empty()) {
            return Status::Fail("Sign in to RetroAchievements first.");
        }
        std::filesystem::path file = PathFromUtf8(entry->path);
        if (!entry->archive_member.empty()) {
            auto unpacked = library::PrepareRom(*entry, data_dir / "cache" / "roms");
            if (!unpacked) {
                return unpacked.status();
            }
            file = unpacked.take();
        }
        auto hash = achievements::HashFile(console, file);
        if (!hash) {
            return hash.status();
        }
        const auto http = achievements::PlatformHttp(achievements::UserAgent("EmuDEN", system::Version()));
        auto progress = achievements::FetchProgress(http, ServerHost(), login, hash.value(), hardcore);
        if (!progress) {
            return progress.status();
        }
        return ToGame(progress.value(), *entry);
    }
};

} // namespace

std::shared_ptr<plugins::AchievementServices> AchievementServicesSvc() {
    static const std::shared_ptr<plugins::AchievementServices> services = std::make_shared<RuntimeServices>();
    return services;
}

} // namespace den::app
