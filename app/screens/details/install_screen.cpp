// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "core needed" page: downloads what a console needs with a progress row, then starts
// the game that asked for it. Route "install", params: system, id (optional game to start),
// restart ("1" starts it from the beginning).

#include <format>

#include "../home/game_widgets.h"
#include "../home/games.h"
#include "../home/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "launch.h"

namespace den::app {

namespace {

class InstallScreen final : public ui::Page {
public:
    ~InstallScreen() override {
        if (job_ && !job_->Finished()) {
            job_->Cancel();
        }
    }

    void Build() override {
        Services& svc = Svc();
        const std::string system = Param("system");
        const auto core = svc.plugins->CoreFor(system);
        const std::string what = core && !core->name.empty() ? core->name : SystemName(system);
        ui::Node* content = BuildPlainFrame(*this, TrF("install.title", what));
        content->align = ui::Align::Center;

        ui::Node* panel = content->Add(ui::MakeBox(ui::Axis::Column, 14));
        panel->width = 640;
        panel->pad_top = 120;
        panel->Emplace<ui::Label>(TrF("install.for", SystemName(system)))->tone = ui::Tone::Dim;
        status_ = panel->Emplace<ui::Label>(Tr("install.starting"));
        bar_ = panel->Emplace<ProgressBar>();
        bar_->visible = !failed_;
        ui::Node* buttons = panel->Add(ui::MakeBox(ui::Axis::Row, 12));
        buttons->pad_top = 16;
        if (failed_) {
            status_->text = error_;
            auto* retry = buttons->Emplace<ui::Button>(Tr("install.retry"), [this] {
                failed_ = false;
                job_.reset();
                Rebuild();
            });
            retry->id = "install.retry";
            retry->width = 200;
        }
        auto* cancel = buttons->Emplace<ui::Button>(failed_ ? Tr("common.back") : Tr("common.cancel"),
                                                    [this] { Leave(); });
        cancel->id = "install.cancel";
        cancel->width = 200;

        if (!job_ && !failed_) {
            job_ = svc.plugins->Install(system);
        }
    }

    void Update(ui::Context&) override {
        if (!job_ || failed_) {
            return;
        }
        if (!job_->Finished()) {
            const float progress = job_->Progress();
            bar_->fraction = progress;
            const std::string message = job_->Message();
            status_->text = message.empty() ? Tr("install.downloading") : message;
            if (progress >= 0) {
                status_->text += std::format("  {}%", static_cast<int>(progress * 100.0f + 0.5f));
            }
            return;
        }
        if (job_->State() == JobState::Done) {
            const std::string id = Param("id");
            const bool restart = Param("restart") == "1";
            job_.reset();
            GetStage().Pop();
            if (!id.empty()) {
                StartGame(id, restart);
            } else {
                GetStage().Toast(Tr("install.done"));
            }
            return;
        }
        failed_ = true;
        error_ = job_->State() == JobState::Cancelled ? Tr("install.cancelled") : job_->Error();
        if (error_.empty()) {
            error_ = Tr("install.failed");
        }
        Rebuild();
    }

    bool OnNav(input::Nav nav) override {
        if (nav == input::Nav::Back) {
            Leave();
            return true;
        }
        return false;
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, Tr("hint.select")}, {input::Nav::Back, Tr("common.cancel")}};
    }

private:
    void Leave() {
        if (job_ && !job_->Finished()) {
            job_->Cancel();
        }
        GetStage().Pop();
    }

    JobPtr job_;
    bool failed_ = false;
    std::string error_;
    ui::Label* status_ = nullptr;
    ProgressBar* bar_ = nullptr;
};

} // namespace

DEN_REGISTER_SCREEN("install", InstallScreen);

} // namespace den::app
