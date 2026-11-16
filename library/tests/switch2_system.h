// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The console of the Switch 2 plugin (installed by hand from a file), as its manifest's
// systems[] has it. The library knows it only once the plugin is installed.

#pragma once

inline constexpr const char* SWITCH2_SYSTEM = R"json({"id":"switch2","name":"Nintendo Switch 2",
    "short_name":"Switch 2","order":0,"content":{"extensions":["nup","nsp","xci"],"scanner":"switch2",
    "folder_aliases":["switch 2","ns2","nx2","nintendo switch 2","nintendoswitch2"],"zip":"no"},
    "artwork":{"launchbox":"Nintendo Switch 2"}})json";
