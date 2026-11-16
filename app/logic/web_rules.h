// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the in-app browser takes from a site (web_browser.h): the extensions, the ROM images
// nobody gets, the links that may leave the page and how downloads are named.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace den::app::web {

/// "Any": a host list or an extension list with this takes everything.
inline constexpr const char* ANY = "*";

/// Patch and archive formats: bps, ips, ups, xdelta, vcdiff, aps, ppf, zip, 7z.
const std::vector<std::string>& DefaultExtensions();
/// The extension of a file name in lower case without the dot.
std::string ExtensionOf(const std::string& file_name);
/// Cartridge, disc and card images (gb, gba, nds, iso, chd, ...).
bool IsRomImage(const std::string& extension);
enum class Verdict { Take, NotWanted, RomImage };
Verdict JudgeDownload(const std::string& file_name, const std::vector<std::string>& wanted);
/// What the user is told about a download that was not taken.
std::string RefusalText(const std::string& file_name, Verdict verdict);
/// Whether the user may be asked to follow a link that leaves the page's hosts: an https
/// address (http on this machine) with a host.
bool CanLeaveTo(const std::string& url);
/// "a, B ,c" -> {"a", "b", "c"}.
std::vector<std::string> SplitList(const std::string& text);
/// An opener's extensions in lower case, without empty ones and ROM images.
void CleanExtensions(std::vector<std::string>& extensions);
/// A file of `dir` that does not exist yet: "hack.bps", then "hack (2).bps", ...
std::filesystem::path FreeFile(const std::filesystem::path& dir, const std::string& name);

} // namespace den::app::web
