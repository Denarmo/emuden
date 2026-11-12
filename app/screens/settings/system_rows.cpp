// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Controllers (what is connected, and its family) and System (version, folders, engine).

#include <format>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/system/info.h"

namespace den::app {

namespace {

std::vector<ui::RowSpec> ControllerRows() {
    std::vector<ui::RowSpec> rows;
    const input::System& input = *Svc().input;
    for (int i = 0; i < input.PadCount(); ++i) {
        const input::PadState pad = input.Pad(i);
        ui::RowSpec row = ValueRow(std::format("pad.{}", i), pad.name.empty() ? "Controller" : pad.name,
                                   [family = pad.family] { return std::string(input::FamilyName(family)); });
        row.description = std::format("Player {}", i + 1);
        rows.push_back(std::move(row));
    }
    if (rows.empty()) {
        rows.push_back(ValueRow("pad.none", "No controller connected",
                                [] { return std::string("Connect one at any time"); }));
    }
    ui::RowSpec keyboard = ValueRow("pad.keyboard", "Keyboard", [] { return std::string("Player 1"); });
    keyboard.description = "Arrows, Z X A S, Q W, Enter, right Shift";
    rows.push_back(std::move(keyboard));
    ui::RowSpec active = ValueRow("pad.active", "Button hints", [] {
        return std::string(input::FamilyName(Svc().input->ActiveFamily()));
    });
    active.description = "Follow the device used last";
    rows.push_back(std::move(active));
    return rows;
}

std::vector<ui::RowSpec> SystemRows() {
    std::vector<ui::RowSpec> rows;
    rows.push_back(ValueRow("system.version", "Version", [] { return system::Describe(); }));
    ui::RowSpec data = ValueRow("system.data", "Data folder", [] { return PathToUtf8(Svc().data_dir); });
    data.description = pal::IsPortable() ? "Portable: the \"user\" folder next to the program"
                                         : "Saves, states, covers and settings";
    rows.push_back(std::move(data));

    for (const plugins::Console& console : Svc().plugins->Consoles()) {
        if (console.runner != plugins::Runner::Engine) {
            continue;
        }
        ui::RowSpec engine = ValueRow(
            "system.engine." + console.id, console.name + " engine",
            [id = console.id] {
                const auto install = Svc().engine->Find(id);
                return install ? PathToUtf8(install->executable) : std::string("Not found");
            },
            [id = console.id] { ChooseEnginePath(id); });
        engine.description = "Choose the engine's program";
        rows.push_back(std::move(engine));
    }

    rows.push_back(ActionRow("system.quit", "Close EmuDEN", [] {
        Svc().stage->Confirm("Close EmuDEN?", "The app closes and returns to the system.", "Close",
                             [] { Svc().quit = true; });
    }));
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(40, "Controllers", ControllerRows);
DEN_REGISTER_SETTINGS(100, "System", SystemRows);

} // namespace den::app
