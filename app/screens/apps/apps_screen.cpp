// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Apps section of the main menu: the device's other apps as a grid, to open one. It is
// in the rail only where the platform can list apps (Android). Route "apps"; with
// "from=game" (the pause menu, on a device with one display) it has no rail and Back
// returns to the game.

#include "den/app/app_grid.h"
#include "den/app/apps_catalog.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

constexpr float RAIL_W = 232;

class AppsScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = AppsRevision();
        width_ = CanvasWidth();
        in_game_ = Param("from") == "game";
        ui::Node* content = in_game_ ? BuildPlainFrame(*this, "Apps") : BuildRailFrame(*this, "Apps");
        content->gap = 12;

        ui::Node* bar = content->Add(ui::MakeBox(ui::Axis::Row, 10));
        bar->align = ui::Align::Center;
        bar->chrome = true; // the first focus is an app, not the search
        bar->pad_left = 10;
        bar->pad_right = svc.theme->margin;
        auto* search = bar->Emplace<ui::Button>(query_.empty() ? "Search apps" : "Search: " + query_,
                                                [this] { AskQuery(); });
        search->id = "apps.search";
        search->centered = false;
        search->grow = 1;
        if (!query_.empty()) {
            auto* clear = bar->Emplace<ui::Button>("Clear", [this] { SetQuery({}); });
            clear->id = "apps.clear";
            clear->width = 120;
        }

        AppGridOptions options;
        options.id_prefix = "apps";
        options.query = query_;
        options.width = width_ - (in_game_ ? svc.theme->margin * 2 : RAIL_W + svc.theme->margin * 2);
        options.on_open = [this](const system::AppItem& app) {
            OpenApp(app.id, in_game_, *Svc().stage);
        };
        options.on_hold = [](const system::AppItem& app) { ToggleAppFavourite(app.id); };
        AddAppGrid(content, options);
    }

    void Update(ui::Context&) override {
        if (AppsRevision() != revision_ || CanvasWidth() != width_) {
            Rebuild();
        }
    }

    bool OnNav(input::Nav nav) override {
        switch (nav) {
        case input::Nav::Option:
            if (const std::string app = Focus() ? AppInNodeId(Focus()->id) : std::string(); !app.empty()) {
                ToggleAppFavourite(app);
            }
            return true;
        case input::Nav::Extra:
            AskQuery();
            return true;
        case input::Nav::Back:
            if (!query_.empty()) {
                SetQuery({});
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    std::vector<ui::Hint> Hints() const override {
        const bool on_app = Focus() && !AppInNodeId(Focus()->id).empty();
        std::vector<ui::Hint> hints{{input::Nav::Confirm, on_app ? "Open" : "Select"}};
        if (on_app) {
            hints.push_back({input::Nav::Option, Apps().Favourite(AppInNodeId(Focus()->id)) ? "Unpin" : "Pin"});
        }
        hints.push_back({input::Nav::Extra, "Search"});
        hints.push_back({input::Nav::Back, "Back"});
        return hints;
    }

private:
    static float CanvasWidth() {
        const float width = Svc().canvas->Width();
        return width > 0 ? width : 1280.0f;
    }

    void AskQuery() {
        ui::TextRequest request;
        request.title = "Search apps";
        request.text = query_;
        request.placeholder = "Name of an app";
        request.max_length = 32;
        request.allow_empty = true;
        // The grid follows the typing.
        request.on_change = [this](const std::string& text) { SetQuery(text); };
        GetStage().AskText(std::move(request), [this](const std::string& text) { SetQuery(text); });
    }

    void SetQuery(const std::string& query) {
        if (query != query_) {
            query_ = query;
            Rebuild(false);
        }
    }

    std::string query_;
    std::uint64_t revision_ = 0;
    float width_ = 0;
    bool in_game_ = false;
};

} // namespace

DEN_REGISTER_SCREEN("apps", AppsScreen);
// In the rail only where there are apps to list: never on the desktop.
DEN_REGISTRAR([](Registry& r) {
    if (AppsAvailable()) {
        r.AddRail({30, "Apps", "apps"});
    }
});

} // namespace den::app
