// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Play together": route "together", param id (a library entry); "from=game" when it was
// opened from the pause menu over the running game. Host shows this device to others on the
// network; the devices nearby that host a game that can play with this one are listed to join.

#include <format>

#include "den/app/strings.h"
#include "den/app/player.h"
#include "den/app/model_widgets.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/log.h"
#include "together.h"

namespace den::app {

namespace {

constexpr float TEXT_WIDTH = 760;

// A host this game can join, or why not.
std::string JoinText(const net::FoundHost& host, const net::GameInfo& mine) {
    if (host.beacon.busy) {
        return "Playing with someone";
    }
    net::GameInfo theirs;
    theirs.system = host.beacon.system;
    theirs.core = host.beacon.core;
    theirs.mode = host.beacon.mode;
    theirs.rom_hash = host.beacon.rom_hash;
    return net::Mismatch(theirs, mine).empty() ? "Join" : "Cannot join";
}

class TogetherScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        const auto found = svc.library->Find(Param("id"));
        ui::Node* content = BuildPlainFrame(*this, "Play together");
        content->pad_right = svc.theme->margin;
        if (!found) {
            content->Emplace<ui::Label>("That game is not in the library any more.");
            return;
        }
        game_ = *found;
        plan_ = PlanFor(game_);
        content->Emplace<ui::Label>(game_.name, svc.theme->text_heading, gfx::FONT_BOLD);
        auto* how = content->Emplace<Paragraph>(plan_.possible ? plan_.how : plan_.reason, TEXT_WIDTH);
        how->size = svc.theme->text_small;
        how->tone = ui::Tone::Dim;
        how->pad_bottom = 8;
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "together.list";
        if (plan_.possible) {
            AddRows(list, Rows());
        }
        signature_ = Signature();
    }

    void Update(ui::Context&) override {
        Together& together = Together::Get();
        if (!plan_.possible) {
            return;
        }
        if (!together.PartyFor()) {
            together.Browse(); // keeps the list fresh while this page is shown
        }
        // A lockstep game starts on both devices once they are together.
        if (auto party = together.PartyFor(game_.id); party && party->Joined() && !started_ &&
                                                     (plan_.mode == net::Mode::Lockstep || !party->IsHost())) {
            started_ = true;
            StartTogetherGame(game_.id, Param("from") == "game");
            return;
        }
        if (install_ && install_->Finished()) {
            if (install_->State() != JobState::Done) {
                GetStage().Toast(install_->Error().empty() ? "The core could not be installed." : install_->Error(),
                                 6.0f);
            }
            install_ = nullptr;
            Rebuild();
            return;
        }
        if (Signature() != signature_) {
            Rebuild();
        }
    }

    bool OnNav(input::Nav nav) override {
        if (nav != input::Nav::Back) {
            return false;
        }
        // Leaving the page ends a party whose game has not begun.
        if (auto party = Together::Get().PartyFor(game_.id); party && !started_ && !party->Joined()) {
            Together::Get().Leave();
        }
        GetStage().Pop();
        return true;
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Select"}, {input::Nav::Back, "Back"}};
    }

private:
    std::string Signature() const {
        Together& together = Together::Get();
        std::string text = together.RomHash(game_).empty() ? "hashing" : "ready";
        if (const auto core = TogetherCore(game_, plan_)) {
            text += core->Ready() ? "+core" : "-core";
        }
        if (auto party = together.PartyFor()) {
            text += std::format("|{}{}", static_cast<int>(party->GetState()), party->IsHost() ? "h" : "g");
            return text;
        }
        if (net::Browser* browser = together.Browse()) {
            for (const net::FoundHost& host : browser->Hosts(pal::NowSeconds())) {
                text += "|" + host.address.ToString() + host.beacon.title + (host.beacon.busy ? "b" : "");
            }
        }
        return text;
    }

    std::vector<ui::RowSpec> Rows() {
        Together& together = Together::Get();
        std::vector<ui::RowSpec> rows;
        const auto core = TogetherCore(game_, plan_);
        if (!core || !core->Ready()) {
            const std::string name = core && !core->name.empty() ? core->name : plan_.core_id;
            ui::RowSpec row = ValueRow(
                "together.install", TrF("Install {}", name), [this] { return JobText(install_, "Needed on both devices"); },
                [this] {
                    if (!install_) {
                        install_ = Svc().plugins->InstallCore(game_.system, plan_.core_id);
                    }
                });
            rows.push_back(std::move(row));
            return rows;
        }
        if (together.RomHash(game_).empty()) {
            rows.push_back(ValueRow("together.reading", "Reading the game file", [] { return std::string("..."); }));
            return rows;
        }
        if (auto party = together.PartyFor()) {
            PartyRows(*party, rows);
            return rows;
        }
        ui::RowSpec host = ActionRow("together.host", "Host", [this] { HostNow(); });
        host.description = TrF("Show this device to others nearby as \"{}\"", DeviceLabel());
        rows.push_back(std::move(host));
        net::GameInfo mine;
        mine.system = game_.system;
        mine.core = plan_.core_id;
        mine.mode = plan_.mode;
        mine.rom_hash = together.RomHash(game_);
        std::size_t listed = 0;
        if (net::Browser* browser = together.Browse()) {
            for (const net::FoundHost& found : browser->Hosts(pal::NowSeconds())) {
                if (found.beacon.system != game_.system) {
                    continue;
                }
                ui::RowSpec row = ValueRow(
                    "together.host." + found.address.ToString(), TrF("Join {}", found.beacon.device),
                    [found, mine] { return JoinText(found, mine); }, [this, found] { JoinNow(found); });
                row.description = found.beacon.title + (found.beacon.rom_hash == mine.rom_hash ? "" : "  \xC2\xB7  " + Tr("another game"));
                rows.push_back(std::move(row));
                ++listed;
            }
        }
        if (listed == 0) {
            ui::RowSpec none = ValueRow("together.none", "Looking for devices nearby",
                                        [] { return std::string("..."); });
            none.description = "Host on the other device with the same game; both on the same Wi-Fi";
            rows.push_back(std::move(none));
        }
        return rows;
    }

    void PartyRows(net::Party& party, std::vector<ui::RowSpec>& rows) {
        ui::RowSpec state = ValueRow("together.state", party.IsHost() ? "Hosting" : "Joining",
                                     [] { return Together::Get().StatusText(); });
        if (party.IsHost()) {
            state.description = TrF("Shown to others as \"{}\" (port {})", DeviceLabel(), party.Port());
        }
        rows.push_back(std::move(state));
        if (party.Joined() || (party.IsHost() && plan_.mode == net::Mode::Link)) {
            rows.push_back(ActionRow("together.play", "Play", [this] {
                started_ = true;
                StartTogetherGame(game_.id, Param("from") == "game");
            }));
        }
        rows.push_back(ActionRow("together.end", party.IsHost() ? "Stop hosting" : "Leave", [this] {
            Together::Get().Leave();
            Rebuild();
        }));
    }

    // From the pause menu, a game that runs with another core starts again with the party's.
    void AskRestart(std::function<void()> then) {
        GamePlayer* player = CurrentPlayer();
        if (Param("from") != "game" || !player || (player->CoreId() == plan_.core_id && plan_.subsystem == 0)) {
            return then();
        }
        const auto core = TogetherCore(game_, plan_);
        GetStage().Confirm(TrF("Start again with {}?", core ? core->name : plan_.core_id),
                           TrF("{} closes and starts again from its last in-game save. Save in the game first.",
                               game_.name),
                           "Start again", std::move(then));
    }

    void HostNow() {
        AskRestart([this] { HostConfirmed(); });
    }

    void HostConfirmed() {
        if (const Status hosted = Together::Get().Host(game_, plan_); !hosted) {
            GetStage().Toast(hosted.error, 6.0f);
            return;
        }
        if (plan_.mode == net::Mode::Link) {
            // A link game is played while waiting: the other device links when it joins.
            started_ = true;
            StartTogetherGame(game_.id, Param("from") == "game");
            return;
        }
        Rebuild();
    }

    void JoinNow(const net::FoundHost& host) {
        net::GameInfo mine;
        mine.system = game_.system;
        mine.core = plan_.core_id;
        mine.mode = plan_.mode;
        mine.rom_hash = Together::Get().RomHash(game_);
        if (const std::string reason = JoinText(host, mine); reason != "Join") {
            net::GameInfo theirs;
            theirs.system = host.beacon.system;
            theirs.core = host.beacon.core;
            theirs.mode = host.beacon.mode;
            theirs.rom_hash = host.beacon.rom_hash;
            const std::string why = net::Mismatch(theirs, mine);
            GetStage().Toast(why.empty() ? reason : why, 6.0f);
            return;
        }
        AskRestart([this, host] {
            if (const Status joined = Together::Get().Join(game_, plan_, host); !joined) {
                GetStage().Toast(joined.error, 6.0f);
            }
            Rebuild();
        });
    }

    library::Entry game_;
    TogetherPlan plan_;
    JobPtr install_;
    std::string signature_;
    bool started_ = false;
};

} // namespace

void StartTogetherGame(const std::string& game_id, bool over_player) {
    ui::Stage& stage = *Svc().stage;
    stage.Pop(); // this page
    GamePlayer* player = CurrentPlayer();
    const TogetherPlan& plan = Together::Get().Plan();
    if (over_player && player && player->CoreId() == plan.core_id && plan.subsystem == 0) {
        // The game runs with the party's core already: it goes on, together from now.
        if (const auto game = Svc().library->Find(game_id)) {
            Svc().player->SetTogether(Together::Get().RequestFor(*game));
        }
        return;
    }
    if (over_player && player) {
        // The running game closes (its saves are written) and opens again for the party.
        Together::Get().SetRestarting(true);
        player->Exit();
    }
    log::Info("together", "start {} for the party", game_id);
    stage.Push("player", {{"id", game_id}, {"together", "1"}});
}

DEN_REGISTER_SCREEN("together", TogetherScreen);

} // namespace den::app
