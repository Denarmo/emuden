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
    R"json({"id":"virtualboy","name":"Virtual Boy","short_name":"VB","order":650,"content":{"extensions":["vb","vboy"],"folder_aliases":["vb","virtual boy"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Nintendo - Virtual Boy"}})json",
    R"json({"id":"pokemini","name":"Pokémon mini","short_name":"Poké mini","order":690,"content":{"extensions":["min"],"folder_aliases":["pokemon mini","pokémon mini"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Nintendo - Pokemon Mini"}})json",
    R"json({"id":"sg1000","name":"SG-1000","short_name":"SG-1000","order":1050,"content":{"extensions":["sg"],"folder_aliases":["sg-1000","sega sg-1000","sega sg1000"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Sega - SG-1000"}})json",
    R"json({"id":"32x","name":"Sega 32X","short_name":"32X","order":1320,"content":{"extensions":["32x"],"folder_aliases":["sega32x","sega 32x"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Sega - 32X"}})json",
    R"json({"id":"atari2600","name":"Atari 2600","short_name":"2600","order":2000,"content":{"extensions":["a26","bin"],"folder_only_extensions":["bin"],"folder_aliases":["2600","atari 2600","vcs","atari vcs"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Atari - 2600"}})json",
    R"json({"id":"atari5200","name":"Atari 5200","short_name":"5200","order":2050,"content":{"extensions":["a52","bin"],"folder_only_extensions":["bin"],"folder_aliases":["5200","atari 5200"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Atari - 5200"}})json",
    R"json({"id":"atari7800","name":"Atari 7800","short_name":"7800","order":2100,"content":{"extensions":["a78","bin"],"folder_only_extensions":["bin"],"folder_aliases":["7800","atari 7800"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Atari - 7800"}})json",
    R"json({"id":"lynx","name":"Atari Lynx","short_name":"Lynx","order":2200,"content":{"extensions":["lnx","o"],"folder_only_extensions":["o"],"folder_aliases":["atarilynx","atari lynx"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Atari - Lynx"}})json",
    R"json({"id":"jaguar","name":"Atari Jaguar","short_name":"Jaguar","order":2300,"content":{"extensions":["j64","jag","rom","abs","cof"],"folder_only_extensions":["rom","abs","cof"],"folder_aliases":["atarijaguar","atari jaguar"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Atari - Jaguar"}})json",
    R"json({"id":"pce","name":"PC Engine / TurboGrafx-16","short_name":"PCE","order":2400,"content":{"extensions":["pce","sgx"],"folder_aliases":["pcengine","pc engine","tg16","tg-16","turbografx","turbografx16","turbografx-16","supergrafx"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"NEC - PC Engine - TurboGrafx 16"}})json",
    R"json({"id":"ngp","name":"Neo Geo Pocket","short_name":"NGP","order":2510,"content":{"extensions":["ngp"],"folder_aliases":["neogeopocket","neo geo pocket"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"SNK - Neo Geo Pocket"}})json",
    R"json({"id":"ngpc","name":"Neo Geo Pocket Color","short_name":"NGPC","order":2520,"content":{"extensions":["ngc"],"folder_aliases":["neogeopocketcolor","neo geo pocket color"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"SNK - Neo Geo Pocket Color"}})json",
    R"json({"id":"wonderswan","name":"WonderSwan","short_name":"WS","order":2600,"content":{"extensions":["ws"],"folder_aliases":["ws","bandai wonderswan"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Bandai - WonderSwan"}})json",
    R"json({"id":"wsc","name":"WonderSwan Color","short_name":"WSC","order":2610,"content":{"extensions":["wsc"],"folder_aliases":["wonderswancolor","wonderswan color","bandai wonderswan color"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"Bandai - WonderSwan Color"}})json",
    R"json({"id":"vectrex","name":"Vectrex","short_name":"Vectrex","order":2900,"content":{"extensions":["vec","bin","gam"],"folder_only_extensions":["bin","gam"],"folder_aliases":["gce vectrex"],"zip":"unpack"},"artwork":{"libretro_thumbnails":"GCE - Vectrex"}})json",
    R"json({"id":"arcade","name":"Arcade","short_name":"Arcade","order":3000,"content":{"extensions":["zip","7z"],"folder_only_extensions":["zip","7z"],"folder_aliases":["fbneo","mame","fba","cps1","cps2","cps3"],"zip":"pass"},"artwork":{"libretro_thumbnails":"FBNeo - Arcade Games"}})json",
    nullptr,
};

} // namespace den::library
