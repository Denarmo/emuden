// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the library knows about a console: the content rules of its plugin manifest.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace den::library {

/// Where a ROM carries its own title.
struct HeaderTitle {
    std::uint64_t offset = 0;
    std::size_t length = 0;
};

/// Games made of an index file (cue sheet, playlist) and the files it names.
struct MultiFile {
    std::vector<std::string> index;
    std::vector<std::string> members;
    /// A member no index names is a game of its own (a PlayStation .bin without a cue sheet).
    bool loose_members = false;
    std::string track_naming; // "redump": "(Track 2)" files belong to track 1's game
};

/// Bytes at an offset of a file; `mask` (when given, as long as `bytes`) keeps only some bits.
struct MagicBytes {
    std::uint64_t offset = 0;
    std::vector<std::uint8_t> bytes, mask;
};

/// One way a console's file looks (content.magic): every part must be there.
struct Magic {
    std::vector<MagicBytes> parts;
    std::size_t Weight() const; // the bytes it compares: the more, the surer
};

/// One entry of a plugin manifest's systems[], as far as the library uses it.
struct SystemDef {
    std::string id;         // "gba"; also the name of the system's ROM folder
    std::string name;       // "Game Boy Advance"
    std::string short_name; // "GBA"
    int order = 0;
    std::vector<std::string> extensions; // lower case, without the dot
    std::vector<std::string> folder_aliases;
    /// Extensions that only mean this console inside its own folder (".md").
    std::vector<std::string> folder_only_extensions;
    std::vector<std::string> compound_extensions; // "p8.png"
    std::string zip = "unpack";                   // "unpack", "pass" or "no"
    std::string scanner;                          // "" for ROM files, "switch2"
    bool cover_is_rom = false;
    std::optional<MultiFile> multi_file;
    std::vector<HeaderTitle> header_title;
    std::vector<Magic> magic;
    int rcheevos_console = 0; // achievements.rcheevos_console; 0: none
    std::string libretro_thumbnails; // folder in the libretro thumbnail collection
    /// Cover addresses (https) with "{id}" for the game file's own text, a short id such as a
    /// store's app id; tried in order. For consoles whose games are files naming an id.
    std::vector<std::string> cover_by_id;
    /// The LaunchBox Games Database platform ("Nintendo Game Boy Advance"); when empty it is
    /// found from `libretro_thumbnails`.
    std::string launchbox;
};

/// The systems of a manifest: a whole plugin.json, its "systems" array, or one system object.
std::vector<SystemDef> ParseSystems(std::string_view json);

const std::vector<SystemDef>& BuiltinSystems();
/// Every ROM extension of the built-in consoles.
const std::vector<std::string>& BuiltinRomExtensions();

/// The consoles a scan works with.
class SystemTable {
public:
    /// The built-in consoles.
    SystemTable();

    /// Replaces the console of the same id (lists a manifest leaves empty keep their built-in
    /// value) or adds a new one.
    void Merge(std::vector<SystemDef> systems);
    /// The built-in consoles merged with these, dropping consoles an earlier call added.
    void Set(std::vector<SystemDef> systems);

    const std::vector<SystemDef>& All() const {
        return systems_;
    }
    const SystemDef* Find(std::string_view id) const;
    /// The system a ROM folder name stands for (id, alias or name, any case).
    const SystemDef* FromFolderName(std::string_view folder) const;
    /// The system an extension belongs to when only one system uses it.
    const SystemDef* FromUniqueExtension(std::string_view extension) const;
    /// The extension of a ROM file name, lower case without the dot; a compound one
    /// ("p8.png") when a console has it.
    std::string RomExtension(const std::string& lower_name) const;

private:
    std::vector<SystemDef> systems_;
};

} // namespace den::library
