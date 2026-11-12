// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/system/info.h"

namespace den::app {

namespace {

std::vector<ui::RowSpec> AppearanceRows() {
    std::vector<ui::RowSpec> rows;

    ui::RowSpec theme;
    theme.kind = ui::RowSpec::Kind::Choice;
    theme.id = "ui.theme";
    theme.label = "Theme";
    theme.options = ui::ThemeNames();
    theme.get_index = [] {
        const std::vector<std::string> names = ui::ThemeNames();
        const auto it = std::find(names.begin(), names.end(), Svc().theme->name);
        return it == names.end() ? 0 : static_cast<int>(it - names.begin());
    };
    theme.set_index = [](int index) { SetTheme(ui::ThemeNames()[static_cast<std::size_t>(index)]); };
    rows.push_back(std::move(theme));

    if (pal::GetCaps().windowed) {
        ui::RowSpec fullscreen;
        fullscreen.kind = ui::RowSpec::Kind::Toggle;
        fullscreen.id = "window.fullscreen";
        fullscreen.label = "Full screen";
        fullscreen.description = "F11 switches too";
        fullscreen.get_bool = [] { return Svc().window->Fullscreen(); };
        fullscreen.set_bool = [](bool on) { SetFullscreen(on); };
        rows.push_back(std::move(fullscreen));
    }
    return rows;
}

std::vector<ui::RowSpec> AboutRows() {
    ui::RowSpec version;
    version.kind = ui::RowSpec::Kind::Value;
    version.id = "about.version";
    version.label = "Version";
    version.value = [] { return system::Describe(); };

    ui::RowSpec quit;
    quit.kind = ui::RowSpec::Kind::Action;
    quit.id = "about.quit";
    quit.label = "Close EmuDEN";
    quit.action = [] {
        Svc().stage->Confirm("Close EmuDEN?", "The app closes and returns to the system.", "Close",
                             [] { Svc().quit = true; });
    };
    return {version, quit};
}

} // namespace

DEN_REGISTER_SETTINGS(10, "Appearance", AppearanceRows);
DEN_REGISTER_SETTINGS(100, "About", AboutRows);

} // namespace den::app
