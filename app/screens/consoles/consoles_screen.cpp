// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Consoles: every console the plugins describe, with its state. A row opens the console.

#include <format>

#include "console_state.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

JobPtr g_refresh;

int GameCount(const std::string& system) {
    for (const library::SystemInfo& info : Svc().library->Systems()) {
        if (info.id == system) {
            return info.game_count;
        }
    }
    return 0;
}

class ConsolesScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = svc.plugins->Revision();
        ui::Node* content = BuildRailFrame(*this, "Consoles");
        content->pad_right = svc.theme->margin;
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "consoles.list";
        list->pad_bottom = 24;

        std::vector<ui::RowSpec> rows;
        for (const plugins::Console& console : svc.plugins->Consoles()) {
            ui::RowSpec row = ValueRow(
                "console." + console.id, console.name,
                [id = console.id] {
                    const auto found = Svc().plugins->FindConsole(id);
                    return found ? StateText(*found) : std::string();
                },
                [id = console.id] { Svc().stage->Push("console", {{"system", id}}); });
            const int games = GameCount(console.id);
            std::string detail = games == 1 ? "1 game" : std::format("{} games", games);
            if (console.runner == plugins::Runner::Engine) {
                detail += "  ·  runs in its own engine";
            } else if (const auto core = svc.plugins->CoreFor(console.id)) {
                detail += "  ·  " + (core->name.empty() ? core->id : core->name);
            }
            row.description = std::move(detail);
            rows.push_back(std::move(row));
        }
        if (rows.empty()) {
            rows.push_back(ValueRow("consoles.none", "No consoles yet",
                                    [] { return std::string("Check for updates to fetch them"); }));
        }
        ui::RowSpec refresh = ValueRow(
            "consoles.refresh", "Check for updates", [] { return JobText(g_refresh, ""); }, [] {
                if (g_refresh && !g_refresh->Finished()) {
                    return;
                }
                g_refresh = Svc().plugins->Refresh();
                TrackConsoleJob("#refresh", "Consoles", "are up to date", g_refresh);
            });
        refresh.description = "Fetches the list of consoles and cores again";
        rows.push_back(std::move(refresh));
        AddRows(list, std::move(rows));
    }

    void Update(ui::Context&) override {
        if (Svc().plugins->Revision() != revision_) {
            Rebuild();
        }
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Open"}, {input::Nav::Back, "Back"}};
    }

    bool OnNav(input::Nav nav) override {
        if (nav != input::Nav::Back) {
            return false;
        }
        if (GetStage().Depth() > 1) {
            GetStage().Pop();
        } else {
            GetStage().Replace("home");
        }
        return true;
    }

private:
    std::uint64_t revision_ = 0;
};

std::vector<ui::RowSpec> ConsolesRows() {
    ui::RowSpec row = ValueRow(
        "settings.consoles", "Consoles",
        [] { return std::format("{} of {} ready", ReadyCount(), Svc().plugins->Consoles().size()); },
        [] { Svc().stage->Push("consoles"); });
    row.description = "Install, repair or remove what plays each console's games";
    return {row};
}

} // namespace

DEN_REGISTER_SCREEN("consoles", ConsolesScreen);
DEN_REGISTER_RAIL(50, "Consoles", "consoles");
DEN_REGISTER_SETTINGS(50, "Consoles", ConsolesRows);

} // namespace den::app
