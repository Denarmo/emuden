// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the runtime's events show over the game: a card for an unlock, a mastery and a
// leaderboard attempt (top right, one after another), the progress of an achievement (bottom
// right, briefly), the badges of live challenges and the leaderboards' trackers (bottom left).
// An unlock also plays the chime and saves a screenshot when the user chose them.

#include <algorithm>
#include <chrono>
#include <mutex>
#include <ctime>
#include <format>
#include <set>
#include <thread>

#include "achievements_glue.h"
#include "achievements_state.h"
#include "den/achievements/chime.h"
#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/log.h"

namespace den::app {

namespace {

using achievements::Event;
using achievements::EventKind;

constexpr float CARD_W = 380, CARD_H = 84, BADGE = 64;

std::string SafeName(std::string text) {
    for (char& c : text) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|' || static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    return text.size() > 80 ? text.substr(0, 80) : text;
}

void Screenshot(const Event& event) {
    const AchievementsState& s = Ach();
    const std::time_t now = std::time(nullptr);
    const std::tm local = *std::localtime(&now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    const std::filesystem::path file = Svc().data_dir / "screenshots" / "achievements" / s.game.system /
                                       PathFromUtf8(SafeName(s.game.name + " - " + event.title) + " " + stamp + ".png");
    if (const Status saved = Svc().player->SaveScreenshot(file); !saved) {
        log::Warn("achievements", "no screenshot: {}", saved.error);
    }
}

void Push(const Event& event, std::string heading, double seconds = 5) {
    AchievementsState& s = Ach();
    if (s.popups.size() >= 6) {
        s.popups.pop_back();
    }
    s.popups.push_back(Popup{event, std::move(heading), 0, seconds});
    BadgeFile(event.badge_url); // fetched while it waits
}

void DrawCard(ui::Context& ctx, const gfx::Rect& box, const Popup& popup, float alpha) {
    gfx::Canvas& canvas = *ctx.canvas;
    const auto a = static_cast<std::uint8_t>(255 * alpha);
    canvas.FillRect(box, gfx::Color{20, 20, 24, static_cast<std::uint8_t>(225 * alpha)}, 12);
    float x = box.x + 10;
    const std::string badge = BadgeFile(popup.event.badge_url);
    if (!badge.empty()) {
        if (const gfx::Texture texture = ctx.textures->Get(badge, 128); texture.id != 0) {
            canvas.DrawImage(gfx::Rect{x, box.y + (box.h - BADGE) * 0.5f, BADGE, BADGE}, texture,
                             gfx::Color{255, 255, 255, a}, 6);
        }
    }
    x += BADGE + 12;
    const float w = box.Right() - x - 10;
    const float small = ctx.theme->text_small, body = ctx.theme->text_body;
    const gfx::Color dim{200, 200, 210, a}, ink{255, 255, 255, a};
    std::string heading = popup.heading;
    if (popup.event.points > 0 && popup.event.kind == EventKind::Unlocked) {
        heading += std::format("  \xC2\xB7  {} points", popup.event.points);
    }
    canvas.DrawText(ctx.Text().Elide(heading, small, w, gfx::FONT_BOLD), x, box.y + 10, small, dim, gfx::FONT_BOLD);
    canvas.DrawText(ctx.Text().Elide(popup.event.title, body, w, gfx::FONT_BOLD), x, box.y + 30, body, ink,
                    gfx::FONT_BOLD);
    const std::string line = popup.event.text.empty() ? popup.event.value : popup.event.text;
    canvas.DrawText(ctx.Text().Elide(line, small, w), x, box.y + 56, small, dim);
}

} // namespace

std::string BadgeFile(const std::string& url) {
    static std::mutex mutex;
    static std::set<std::string> fetching;
    if (url.rfind("https://", 0) != 0) {
        return {};
    }
    const std::filesystem::path file =
        Svc().data_dir / "cache" / "achievements" / (Md5Hex(url.data(), url.size()) + ".png");
    if (IsRegularFile(file)) {
        return PathToUtf8(file);
    }
    std::lock_guard lock(mutex);
    if (fetching.insert(url).second) {
        std::thread([url, file] {
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            pal::HttpRequest request;
            request.url = url;
            request.to_file = file;
            const pal::HttpResponse answer = pal::HttpGet(request);
            if (!answer.ok()) {
                std::filesystem::remove(file, ec);
            }
            std::lock_guard done(mutex);
            fetching.erase(url);
        }).detach();
    }
    return {};
}

void ShowEvent(const Event& event) {
    AchievementsState& s = Ach();
    plugins::AchievementOptions options;
    {
        std::lock_guard lock(s.mutex);
        options = s.options;
    }
    const double now = pal::NowSeconds();
    switch (event.kind) {
    case EventKind::Unlocked:
        Push(event, event.hardcore ? "Achievement unlocked (hardcore)" : "Achievement unlocked");
        if (options.sound) {
            achievements::PlayChime(static_cast<float>(Svc().settings->GetNumber("player.volume", 1.0)));
        }
        if (options.screenshot) {
            Screenshot(event);
        }
        break;
    case EventKind::GameCompleted:
        Push(event, event.hardcore ? "Mastered" : "Completed", 8);
        break;
    case EventKind::ProgressShow:
        s.progress = event;
        s.progress_until = now + 3.0;
        BadgeFile(event.badge_url);
        break;
    case EventKind::ProgressHide:
        s.progress.reset();
        break;
    case EventKind::ChallengeShow:
        s.challenges[event.id] = event;
        BadgeFile(event.badge_url);
        break;
    case EventKind::ChallengeHide:
        s.challenges.erase(event.id);
        break;
    case EventKind::LeaderboardStarted:
        Push(event, "Leaderboard attempt started", 3);
        break;
    case EventKind::LeaderboardFailed:
        Push(event, "Leaderboard attempt failed", 3);
        break;
    case EventKind::LeaderboardSubmitted: {
        Event shown = event;
        shown.text = "Submitted: " + event.value;
        Push(shown, "Leaderboard", 4);
        break;
    }
    case EventKind::LeaderboardRank: {
        Event shown = event;
        shown.text = "Rank " + event.value + (event.text.empty() ? "" : "  \xC2\xB7  best " + event.text);
        Push(shown, "Leaderboard", 5);
        break;
    }
    case EventKind::TrackerShow:
    case EventKind::TrackerUpdate:
        s.trackers[event.id] = event.value;
        break;
    case EventKind::TrackerHide:
        s.trackers.erase(event.id);
        break;
    case EventKind::ServerError:
        Svc().stage->Toast("RetroAchievements: " + event.text, 5.0f);
        break;
    case EventKind::Disconnected:
        Svc().stage->Toast("RetroAchievements cannot be reached; unlocks are sent when it is back", 5.0f);
        break;
    case EventKind::Reconnected:
        Svc().stage->Toast("RetroAchievements is back: the waiting unlocks were sent");
        break;
    case EventKind::ResetWanted:
        Svc().stage->Toast("Hardcore is on: the game starts again", 4.0f);
        break;
    }
}

void DrawAchievementOverlay(ui::Context& ctx, const gfx::Rect& area) {
    AchievementsState& s = Ach();
    if (s.game_id.empty()) {
        return;
    }
    gfx::Canvas& canvas = *ctx.canvas;
    const double now = pal::NowSeconds();
    // Cards, the oldest first; one at a time on a small window.
    while (!s.popups.empty() && s.popups.front().until > 0 && now >= s.popups.front().until) {
        s.popups.pop_front();
    }
    if (!s.popups.empty()) {
        Popup& popup = s.popups.front();
        if (popup.until == 0) {
            popup.until = now + popup.seconds;
        }
        const double left = popup.until - now, shown = popup.seconds - left;
        const float alpha = static_cast<float>(std::clamp(std::min(left, shown) / 0.25, 0.0, 1.0));
        const float w = std::min(CARD_W, area.w - 24);
        DrawCard(ctx, gfx::Rect{area.Right() - w - 12, area.y + 12, w, CARD_H}, popup, alpha);
    }
    const float small = ctx.theme->text_small;
    const gfx::Color plate{0, 0, 0, 150}, ink{255, 255, 255, 255};
    if (s.progress && now < s.progress_until) {
        const Event& p = *s.progress;
        const std::string text = p.title + "  " + p.value;
        const float w = std::min(area.w * 0.5f, ctx.Text().Measure(text, small, gfx::FONT_BOLD) + 60);
        const gfx::Rect box{area.Right() - w - 12, area.Bottom() - 48, w, 36};
        canvas.FillRect(box, plate, 8);
        const std::string badge = BadgeFile(p.badge_url);
        if (!badge.empty()) {
            if (const gfx::Texture texture = ctx.textures->Get(badge, 64); texture.id != 0) {
                canvas.DrawImage(gfx::Rect{box.x + 4, box.y + 4, 28, 28}, texture, ink, 4);
            }
        }
        canvas.DrawText(ctx.Text().Elide(text, small, w - 48, gfx::FONT_BOLD), box.x + 40, box.y + 10, small, ink,
                        gfx::FONT_BOLD);
    }
    float x = area.x + 12;
    const float y = area.Bottom() - 48;
    for (const auto& [id, challenge] : s.challenges) {
        const std::string badge = BadgeFile(challenge.badge_url);
        canvas.FillRect(gfx::Rect{x, y, 36, 36}, plate, 6);
        if (!badge.empty()) {
            if (const gfx::Texture texture = ctx.textures->Get(badge, 64); texture.id != 0) {
                canvas.DrawImage(gfx::Rect{x + 2, y + 2, 32, 32}, texture, ink, 4);
            }
        }
        x += 42;
    }
    for (const auto& [id, value] : s.trackers) {
        const float w = ctx.Text().Measure(value, small, gfx::FONT_BOLD) + 20;
        canvas.FillRect(gfx::Rect{x, y, w, 36}, plate, 8);
        canvas.DrawText(value, x + 10, y + 10, small, ink, gfx::FONT_BOLD);
        x += w + 6;
    }
}

} // namespace den::app
