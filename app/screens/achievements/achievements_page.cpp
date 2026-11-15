// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Route "achievements": the running game's achievements, opened from the pause menu. Locked
// ones first, nearest to done first, then the unlocked ones, latest first. The plugin's own
// pages show any game of the library and the user's profile.

#include <ctime>
#include <format>

#include "achievements_glue.h"
#include "achievements_state.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

std::string When(std::int64_t unix_time) {
    if (unix_time <= 0) {
        return "Unlocked";
    }
    const std::time_t t = static_cast<std::time_t>(unix_time);
    const std::tm local = *std::localtime(&t);
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
    return std::string("Unlocked ") + text;
}

class AchievementsPage final : public ui::Page {
    void Build() override {
        AchievementsState& s = Ach();
        const auto progress = s.runtime ? s.runtime->Progress() : std::nullopt;
        ui::Node* content = BuildPlainFrame(*this, progress ? progress->title : std::string("Achievements"));
        ui::Node* list = content->Add(ui::MakeList(6));
        list->id = "achievements.list";
        list->pad_bottom = 24;
        if (!progress) {
            list->Emplace<ui::Label>("This game has no achievements loaded.");
            return;
        }
        std::vector<ui::RowSpec> rows;
        rows.push_back(ValueRow("achievements.summary",
                                std::format("{} of {} unlocked", progress->unlocked, progress->total), [p = *progress] {
                                    return std::format("{} of {} points  \xC2\xB7  {}", p.points_unlocked,
                                                       p.points_total, p.hardcore ? "Hardcore" : "Softcore");
                                }));
        for (const achievements::Achievement& a : progress->achievements) {
            if (a.unofficial) {
                continue;
            }
            std::string state = a.unsupported ? std::string("Not supported")
                                : a.unlocked  ? When(a.unlock_time)
                                : !a.progress.empty() ? a.progress
                                                      : std::string("Locked");
            ui::RowSpec row = ValueRow("achievement." + std::to_string(a.id),
                                       std::format("{}  ({} points)", a.title, a.points),
                                       [state] { return state; });
            row.description = a.description;
            rows.push_back(std::move(row));
        }
        AddRows(list, std::move(rows), 64);
    }
};

} // namespace

DEN_REGISTER_SCREEN("achievements", AchievementsPage);

} // namespace den::app
