// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Which language packs there are and which one the app should use: pure choices the language
// screens (settings and the first-run offer) make from the list of packs.

#pragma once

#include <string>
#include <vector>

namespace den::app {

/// One language pack as the plugin list shows it.
struct LanguageOffer {
    std::string plugin;    // plugin id
    std::string code;      // "es"
    std::string name;      // its own name, "Español"
    bool installed = false;
};

/// One offer per code, the installed one before one only offered, in order of name.
std::vector<LanguageOffer> LanguageOffers(std::vector<LanguageOffer> all);

/// Whether the first-run offer is to be made: no language chosen yet, none asked about, and a
/// pack for one of the system's languages that is not installed. `chosen` is the setting
/// "ui.language" ("" when never set), `asked` is "ui.language_asked".
const LanguageOffer* FirstRunOffer(const std::vector<LanguageOffer>& offers,
                                   const std::vector<std::string>& system_languages, const std::string& chosen,
                                   bool asked);

} // namespace den::app
