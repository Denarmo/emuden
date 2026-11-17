// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The quick settings' actions (ids in quick_spec.h), as they are now: what each is called,
// what it holds, whether it is on, and what it does. The layouts arrange them.

#include <algorithm>
#include <format>

#include "companion_play.h"
#include "den/app/app.h"
#include "den/app/apps_catalog.h"
#include "den/app/beside.h"
#include "den/app/pages.h"
#include "den/app/quick_actions.h"
#include "den/app/volume_rows.h"

namespace den::app {

namespace {

constexpr const char* FPS_NAMES[] = {"FPS: Off", "FPS: On", "FPS: Graph"};

QuickAction Make(std::string id, std::string node_id, std::string label, QuickAction::Kind kind,
                 std::function<void()> run) {
    QuickAction action;
    action.id = std::move(id);
    action.node_id = std::move(node_id);
    action.label = std::move(label);
    action.kind = kind;
    action.run = std::move(run);
    return action;
}

std::function<void()> PlayerPage(const char* name) {
    return [name] {
        if (GamePlayer* now = CurrentPlayer()) {
            now->OpenPage(name);
        }
    };
}

} // namespace

std::vector<QuickAction> CompanionPlay::MakeActions(const player::Snapshot& snapshot) {
    using Kind = QuickAction::Kind;
    Services& svc = Svc();
    std::vector<QuickAction> out;
    const bool paused = snapshot.state == player::State::Paused;
    out.push_back(Make("pause", "companion.menu", paused ? "Resume" : "Pause", Kind::Control,
                       [] { Svc().input->Inject(input::Event::MakeNav(input::Nav::Menu)); }));
    out.back().selected = false;
    if (snapshot.can_rewind) {
        out.push_back(Make("rewind", "companion.rewind", "Rewind", Kind::Control, [this] {
            rewind_ = !Svc().player->GetSnapshot().rewinding;
            fast_ = false;
            Svc().player->SetFastForward(false);
            Svc().player->SetRewind(rewind_);
        }));
        out.back().selected = snapshot.rewinding;
    }
    if (snapshot.can_fast_forward) {
        out.push_back(Make("fast_forward", "companion.fast", "Fast-forward", Kind::Control, [this] {
            fast_ = !Svc().player->GetSnapshot().fast_forward;
            rewind_ = false;
            Svc().player->SetRewind(false);
            Svc().player->SetFastForward(fast_);
        }));
        out.back().selected = snapshot.fast_forward;
    }
    out.push_back(Make("exit", "companion.exit", "Exit", Kind::Control, [] {
        // The player screen saves the game and books its play time.
        if (GamePlayer* screen = CurrentPlayer()) {
            screen->Exit();
        }
    }));

    // The sections, each one step in, with what it holds now at the right.
    GamePlayer* screen = CurrentPlayer();
    if (snapshot.can_save_states) {
        int used = 0;
        for (int slot = 0; slot < player::STATE_SLOTS; ++slot) {
            used += svc.player->HasState(slot) ? 1 : 0;
        }
        QuickAction& states = out.emplace_back(
            Make("states", "companion.section.states", "States", Kind::Section, [this] { Open(QuickView::States); }));
        states.value = used ? std::format("{} saved", used) : std::string("None saved");
        states.rows.push_back(ValueRow(states.node_id, states.label, [value = states.value] { return value; },
                                       states.run));
    }
    std::string picture;
    for (const player::PicturePreset& preset : player::PicturePresets()) {
        picture = preset.id == snapshot.picture ? preset.name : picture;
    }
    std::string size;
    if (screen) {
        const auto& names = ScreenSizeNames();
        const auto index = static_cast<std::size_t>(std::max(0, screen->ScreenSizeIndex()));
        size = index < names.size() ? names[index] : std::string();
    }
    // An engine app draws the game and reads the pads itself.
    if (snapshot.can_picture) {
        QuickAction& section = out.emplace_back(Make("screen", "companion.section.screen", "Screen and picture",
                                                     Kind::Section, [this] { Open(QuickView::Screen); }));
        section.value = size.empty() || picture.empty() ? size + picture : size + " · " + picture;
        section.rows.push_back(ValueRow(section.node_id, section.label, [value = section.value] { return value; },
                                        section.run));
    }
    if (screen && snapshot.can_picture) {
        out.push_back(Make("swap", "companion.quick.swap", "Swap screens", Kind::Control, [] {
            if (GamePlayer* now = CurrentPlayer()) {
                now->SwapDisplays();
            }
        }));
        // Off, the frame rate, the frame rate with a graph of the frame times.
        const int fps = screen->FpsMode();
        QuickAction& counter = out.emplace_back(Make("fps", "companion.quick.fps", FPS_NAMES[fps], Kind::Control, [fps] {
            if (GamePlayer* now = CurrentPlayer()) {
                now->SetFpsMode((fps + 1) % GamePlayer::FPS_MODES);
            }
        }));
        counter.selected = fps != 0;
    }
    if (screen) {
        QuickAction& bfi = out.emplace_back(Make("bfi", "companion.bfi", BFI_LABEL, Kind::Toggle, [this] {
            if (GamePlayer* now = CurrentPlayer()) {
                now->SetBfi(!now->Bfi().On());
            }
            Rebuild();
        }));
        bfi.selected = screen->Bfi().On();
        bfi.refusal = screen->Bfi().Refusal(snapshot);
        bfi.enabled = bfi.refusal.empty() || bfi.selected;
        bfi.value = bfi.enabled ? std::string(bfi.selected ? "On" : "Off") : std::string("Not available");
        bfi.rows.push_back(BfiRow(*screen, snapshot));
        if (!snapshot.external) {
            QuickAction& controls = out.emplace_back(Make("controls", "companion.section.controls", "Controls and core",
                                                          Kind::Section, [this] { Open(QuickView::Controls); }));
            controls.rows.push_back(ActionRow(controls.node_id, controls.label, controls.run));
            out.push_back(Make("options", "companion.quick.options", "Core options", Kind::Section, PlayerPage("options")));
            out.push_back(Make("buttons", "companion.quick.buttons", "Buttons", Kind::Section, PlayerPage("buttons")));
        }
    }
    // What other features added (cheats).
    for (const QuickActionHook& hook : QuickActionHooks()) {
        const bool built_in = std::any_of(out.begin(), out.end(), [&](const QuickAction& a) { return a.id == hook.id; });
        if (built_in || !KnownQuickAction(hook.id) || !hook.run || (hook.available && !hook.available())) {
            continue;
        }
        QuickAction& added = out.emplace_back(Make(hook.id, "companion.section." + hook.id, hook.label, Kind::Section, hook.run));
        added.value = hook.value ? hook.value() : std::string();
        added.rows.push_back(ValueRow(added.node_id, added.label, [value = added.value] { return value; }, added.run));
    }
    if (screen && AppsAvailable()) {
        QuickAction& apps = out.emplace_back(Make("apps", "companion.section.apps", "Apps", Kind::Section, [this] {
            // The list has the apps opened last at its top.
            GetStage().Push("companion.apps");
        }));
        apps.value = other_volume_ ? Beside().App().label : std::string();
        apps.rows.push_back(ValueRow(apps.node_id, apps.label, [value = apps.value] { return value; }, apps.run));
    }
    QuickAction& volume = out.emplace_back(Make("volume", "companion.section.volume", "Volume", Kind::Sliders,
                                                [this] { Open(QuickView::Volume); }));
    volume.rows = VolumeRows("companion", other_volume_);
    return out;
}

// Black frame insertion, as in the pause menu: a toggle, or why it cannot be had here.
ui::RowSpec CompanionPlay::BfiRow(const GamePlayer& screen, const player::Snapshot& snapshot) {
    const std::string refused = screen.Bfi().Refusal(snapshot);
    if (!refused.empty() && !screen.Bfi().On()) {
        ui::RowSpec row = ValueRow("companion.bfi", BFI_LABEL, [] { return std::string("Not available"); });
        row.description = refused;
        return row;
    }
    ui::RowSpec row = ToggleRow(
        "companion.bfi", BFI_LABEL,
        [] {
            GamePlayer* now = CurrentPlayer();
            return now && now->Bfi().On();
        },
        [this](bool on) {
            if (GamePlayer* now = CurrentPlayer()) {
                now->SetBfi(on);
            }
            Rebuild();
        });
    row.description = screen.Bfi().Status(snapshot);
    row.live_description = [] {
        GamePlayer* now = CurrentPlayer();
        return now ? now->Bfi().Status(Svc().player->GetSnapshot()) : std::string();
    };
    return row;
}

} // namespace den::app
