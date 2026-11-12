// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// System: version, folders, engines.

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/system/info.h"

namespace den::app {

namespace {

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

DEN_REGISTER_SETTINGS(100, "System", SystemRows);

} // namespace den::app
