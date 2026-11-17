// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Language codes: the app's ("en", "es", "pt-BR": a lower-case language, an optional upper-case
// region), what the system reports, and what a core is told.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace den {

/// "es_ES.UTF-8", "es-419", "ES" as "es", "es-ES", "es-419", "es". Empty when the text is no
/// language code.
std::string NormalizeLanguage(std::string_view text);

/// The language part of a code: "pt-BR" is "pt".
std::string LanguageBase(std::string_view code);

/// The code of `available` that serves the system's languages (best first): the first
/// preference that an available code equals, else shares its language part with. English
/// and empty entries are skipped (English is built in). Empty when none does.
std::string MatchLanguage(const std::vector<std::string>& preferred,
                          const std::vector<std::string>& available);

/// libretro's RETRO_LANGUAGE_* number for an app language code (English for an unknown one).
unsigned RetroLanguage(std::string_view code);

} // namespace den
