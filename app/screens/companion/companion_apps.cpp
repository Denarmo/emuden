// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The device's apps on the second screen, from the quick settings' "Apps" button: a tap
// opens one on this display while the game goes on on the other (YouTube beside the game).
// The page closes with the choice, so the quick settings are there again when the app is
// left with the system's Back or Home gesture on this screen. Held, an app is pinned.
// Route "companion.apps" on the companion stage.

#include "den/app/strings.h"
#include "den/app/app_grid.h"
#include "den/app/apps_catalog.h"
#include "companion_page.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/app/second_screen.h"

namespace den::app {

namespace {

constexpr float BAR_HEIGHT = 52;

class CompanionApps final : public CompanionPage {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = AppsRevision();
        width_ = svc.canvas->Width();
        Root().SetPadding(24, 16);
        Root().gap = 10;

        ui::Node* bar = Root().Add(ui::MakeBox(ui::Axis::Row, 10));
        bar->align = ui::Align::Center;
        auto* back = bar->Emplace<ui::Button>("Back", [this] { GetStage().Pop(); });
        back->id = "companion.apps.back";
        back->width = 110;
        back->height = BAR_HEIGHT;
        auto* title = bar->Emplace<ui::Label>("Apps", svc.theme->text_heading, gfx::FONT_BOLD);
        title->pad_left = 8;
        auto* search = bar->Emplace<ui::Button>(query_.empty() ? "Search" : TrF("Search: {}", query_),
                                                [this] { AskQuery(); });
        search->id = "companion.apps.search";
        search->centered = false;
        search->grow = 1;
        search->height = BAR_HEIGHT;
        if (!query_.empty()) {
            auto* clear = bar->Emplace<ui::Button>("Clear", [this] { SetQuery({}); });
            clear->id = "companion.apps.clear";
            clear->width = 110;
            clear->height = BAR_HEIGHT;
        }

        AppGridOptions options;
        options.id_prefix = "companion.apps";
        options.query = query_;
        options.width = (width_ > 0 ? width_ : 800.0f) - 48;
        options.on_open = [this](const system::AppItem& app) {
            OpenApp(app.id, true, GetStage());
            // Back to the quick settings under the app.
            GetStage().Pop();
        };
        options.on_hold = [](const system::AppItem& app) { ToggleAppFavourite(app.id); };
        AddAppGrid(&Root(), options);
    }

    void Update(ui::Context& ctx) override {
        if (AppsRevision() != revision_ || ctx.canvas->Width() != width_) {
            Rebuild();
        }
    }

private:
    void AskQuery() {
        ui::TextRequest request;
        request.title = "Search apps";
        request.text = query_;
        request.placeholder = "Name of an app";
        request.allow_empty = true;
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
};

} // namespace

DEN_REGISTER_SCREEN("companion.apps", CompanionApps);

} // namespace den::app
