// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The consoles the library knows without any plugin installed: the systems[] of the official
// plugin manifests (id, names, content rules, thumbnail collection), one per line. Plugins
// replace or extend it through Library::SetSystems.

#include "den/library/systems.h"

namespace den::library {

extern const char* const BUILTIN_SYSTEMS_JSON[];
const char* const BUILTIN_SYSTEMS_JSON[] = {
    R"json({"id":"switch2","name":"Nintendo Switch 2","short_name":"Switch 2","order":0,"content":{"extensions":["nup","nsp","xci"],"scanner":"switch2","folder_aliases":["switch 2","ns2","nx2","nintendo switch 2","nintendoswitch2"],"zip":"no"}})json",
    R"json({"id":"nes","name":"NES","short_name":"NES","order":200,"content":{"extensions":["nes","fds","unf","unif"],"folder_aliases":["famicom","fc","nintendo"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Nintendo - Nintendo Entertainment System"}})json",
    R"json({"id":"snes","name":"Super Nintendo","short_name":"SNES","order":300,"content":{"extensions":["sfc","smc","fig","swc","bs"],"folder_aliases":["sfc","superfamicom","supernintendo"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Nintendo - Super Nintendo Entertainment System"}})json",
    R"json({"id":"gb","name":"Game Boy","short_name":"GB","order":400,"content":{"extensions":["gb"],"folder_aliases":["gameboy"],"zip":"unpack","header_title":[{"offset":308,"length":11,"encoding":"ascii"}]},"artwork":{"libretro_thumbnails":"Nintendo - Game Boy"}})json",
    R"json({"id":"gbc","name":"Game Boy Color","short_name":"GBC","order":500,"content":{"extensions":["gbc"],"folder_aliases":["gameboycolor"],"zip":"unpack","header_title":[{"offset":308,"length":11,"encoding":"ascii"}]},"artwork":{"libretro_thumbnails":"Nintendo - Game Boy Color"}})json",
    R"json({"id":"gba","name":"Game Boy Advance","short_name":"GBA","order":600,"content":{"extensions":["gba"],"folder_aliases":["gameboyadvance"],"zip":"unpack","header_title":[{"offset":160,"length":12,"encoding":"ascii"}]},"artwork":{"libretro_thumbnails":"Nintendo - Game Boy Advance"}})json",
    R"json({"id":"n64","name":"Nintendo 64","short_name":"N64","order":700,"content":{"extensions":["n64","z64","v64"],"folder_aliases":["nintendo64"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Nintendo - Nintendo 64"}})json",
    R"json({"id":"nds","name":"Nintendo DS","short_name":"NDS","order":800,"content":{"extensions":["nds"],"folder_aliases":["ds","nintendods"],"zip":"unpack","header_title":[{"offset":0,"length":12,"encoding":"ascii"}]},"artwork":{"libretro_thumbnails":"Nintendo - Nintendo DS"}})json",
    R"json({"id":"gamecube","name":"GameCube","short_name":"GC","order":900,"content":{"extensions":["gcm","rvz","gcz","iso"],"folder_aliases":["gc","ngc"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Nintendo - GameCube"}})json",
    R"json({"id":"wii","name":"Wii","short_name":"Wii","order":1000,"content":{"extensions":["wbfs","rvz","wad","iso"],"folder_aliases":[],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Nintendo - Wii"}})json",
    R"json({"id":"sms","name":"Master System","short_name":"SMS","order":1100,"content":{"extensions":["sms"],"folder_aliases":["mastersystem"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Sega - Master System - Mark III"}})json",
    R"json({"id":"gg","name":"Game Gear","short_name":"GG","order":1200,"content":{"extensions":["gg"],"folder_aliases":["gamegear"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Sega - Game Gear"}})json",
    R"json({"id":"genesis","name":"Genesis / Mega Drive","short_name":"MD","order":1300,"content":{"extensions":["md","gen","smd","bin"],"folder_aliases":["megadrive","md","segagenesis"],"folder_only_extensions":["md"],"zip":"unpack","header_title":[{"offset":336,"length":48,"encoding":"ascii"}]},"artwork":{"libretro_thumbnails":"Sega - Mega Drive - Genesis"}})json",
    R"json({"id":"saturn","name":"Saturn","short_name":"SAT","order":1400,"content":{"extensions":["cue","chd","m3u","iso"],"folder_aliases":["ss","segasaturn"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Sega - Saturn"}})json",
    R"json({"id":"dreamcast","name":"Dreamcast","short_name":"DC","order":1500,"content":{"extensions":["gdi","cdi","chd","cue","m3u"],"folder_aliases":["dc"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Sega - Dreamcast"}})json",
    R"json({"id":"psx","name":"PlayStation","short_name":"PS1","order":1600,"content":{"extensions":["cue","chd","pbp","m3u","iso","img"],"folder_aliases":["ps1","playstation","ps","psone"],"zip":"unpack","multi_file":{"index":["m3u","cue"],"members":["bin"],"loose_members":true,"track_naming":"redump"}},"artwork":{"libretro_thumbnails":"Sony - PlayStation"}})json",
    R"json({"id":"ps2","name":"PlayStation 2","short_name":"PS2","order":1700,"content":{"extensions":["iso","chd","cso"],"folder_aliases":["playstation2"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Sony - PlayStation 2"}})json",
    R"json({"id":"psp","name":"PSP","short_name":"PSP","order":1800,"content":{"extensions":["iso","cso","pbp"],"folder_aliases":["playstationportable"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Sony - PlayStation Portable"}})json",
    R"json({"id":"pico8","name":"PICO-8","short_name":"PICO-8","order":1900,"content":{"extensions":["p8"],"compound_extensions":["p8.png"],"cover_is_rom":true,"folder_aliases":["pico-8","p8"],"zip":"unpack"}})json",
    nullptr,
};

} // namespace den::library
