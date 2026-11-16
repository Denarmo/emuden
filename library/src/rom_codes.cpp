// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Game codes and titles in ROM headers, and the games hacks are made from most
// (den/library/rom_codes.h).

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <vector>

#include "den/base/files.h"
#include "den/library/rom_codes.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<std::uint8_t>;

struct Known {
    std::string_view system;
    std::string_view key; // the game code, or the header title for KNOWN_TITLES
    std::string_view game;
};

// The games ROM hacks are made from most, by their game code (No-Intro names).
constexpr Known KNOWN_CODES[] = {
    {"gba", "BPRE", "Pokemon - FireRed Version (USA, Europe)"},
    {"gba", "BPGE", "Pokemon - LeafGreen Version (USA, Europe)"},
    {"gba", "BPEE", "Pokemon - Emerald Version (USA, Europe)"},
    {"gba", "AXVE", "Pokemon - Ruby Version (USA, Europe)"},
    {"gba", "AXPE", "Pokemon - Sapphire Version (USA, Europe)"},
    {"gba", "AMKE", "Mario Kart - Super Circuit (USA)"},
    {"gba", "AA2E", "Super Mario Advance 2 - Super Mario World (USA, Australia)"},
    {"gba", "AX4E", "Super Mario Advance 4 - Super Mario Bros. 3 (USA)"},
    {"gba", "AZLE", "Legend of Zelda, The - A Link to the Past & Four Swords (USA)"},
    {"gba", "BZME", "Legend of Zelda, The - The Minish Cap (USA)"},
    {"gba", "AE7E", "Fire Emblem (USA, Australia)"},
    {"gba", "BE8E", "Fire Emblem - The Sacred Stones (USA, Australia)"},
    {"gba", "AMTE", "Metroid Fusion (USA)"},
    {"gba", "BMXE", "Metroid - Zero Mission (USA)"},
    {"gbc", "AAUE", "Pokemon - Gold Version (USA, Europe)"},
    {"gbc", "AAXE", "Pokemon - Silver Version (USA, Europe)"},
    {"gbc", "BYTE", "Pokemon - Crystal Version (USA, Europe)"},
    {"nds", "ADAE", "Pokemon - Diamond Version (USA)"},
    {"nds", "APAE", "Pokemon - Pearl Version (USA)"},
    {"nds", "CPUE", "Pokemon - Platinum Version (USA)"},
    {"nds", "IPKE", "Pokemon - HeartGold Version (USA)"},
    {"nds", "IPGE", "Pokemon - SoulSilver Version (USA)"},
    {"nds", "IRBO", "Pokemon - Black Version (USA, Europe)"},
    {"nds", "IRAO", "Pokemon - White Version (USA, Europe)"},
    {"nds", "IREO", "Pokemon - Black Version 2 (USA, Europe)"},
    {"nds", "IRDO", "Pokemon - White Version 2 (USA, Europe)"},
    {"nds", "AMCE", "Mario Kart DS (USA, Australia)"},
    {"nds", "A2DE", "New Super Mario Bros. (USA)"},
    {"n64", "NSME", "Super Mario 64 (USA)"},
    {"n64", "NZLE", "Legend of Zelda, The - Ocarina of Time (USA)"},
    {"n64", "NZSE", "Legend of Zelda, The - Majora's Mask (USA)"},
    {"n64", "NKTE", "Mario Kart 64 (USA)"},
    {"n64", "NMQE", "Paper Mario (USA)"},
    {"psx", "SCUS-94163", "Final Fantasy VII (USA) (Disc 1)"},
    {"psx", "SLUS-00892", "Final Fantasy VIII (USA) (Disc 1)"},
    {"psx", "SCUS-94900", "Crash Bandicoot (USA)"},
    {"psx", "SCUS-94228", "Spyro the Dragon (USA)"},
    {"psx", "SLUS-00594", "Metal Gear Solid (USA) (Disc 1)"},
    {"psx", "SLUS-00707", "Silent Hill (USA)"},
    {"psx", "SCUS-94194", "Gran Turismo (USA)"},
    {"ps2", "SLUS-20312", "Final Fantasy X (USA)"},
    {"ps2", "SLUS-20370", "Kingdom Hearts (USA)"},
    {"ps2", "SLUS-20062", "Grand Theft Auto III (USA)"},
    {"ps2", "SLUS-20946", "Grand Theft Auto - San Andreas (USA)"},
};

// The same for consoles whose headers have no code: their header titles.
constexpr Known KNOWN_TITLES[] = {
    {"gb", "POKEMON RED", "Pokemon - Red Version (USA, Europe) (SGB Enhanced)"},
    {"gb", "POKEMON BLUE", "Pokemon - Blue Version (USA, Europe) (SGB Enhanced)"},
    {"gb", "POKEMON YELLOW", "Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (CGB+SGB Enhanced)"},
    {"gbc", "PM_CRYSTAL", "Pokemon - Crystal Version (USA, Europe)"},
    {"gbc", "POKEMON_GLD", "Pokemon - Gold Version (USA, Europe)"},
    {"gbc", "POKEMON_SLV", "Pokemon - Silver Version (USA, Europe)"},
    {"snes", "SUPER MARIOWORLD", "Super Mario World (USA)"},
    {"snes", "THE LEGEND OF ZELDA", "Legend of Zelda, The - A Link to the Past (USA)"},
    {"snes", "Super Metroid", "Super Metroid (Japan, USA) (En,Ja)"},
    {"snes", "EARTH BOUND", "EarthBound (USA)"},
    {"snes", "SUPER MARIO KART", "Super Mario Kart (USA)"},
    {"snes", "YOSHI'S ISLAND", "Super Mario World 2 - Yoshi's Island (USA)"},
};

Bytes ReadAt(std::ifstream& in, std::uint64_t offset, std::size_t size) {
    Bytes out(size);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    out.resize(static_cast<std::size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    return out;
}

// Printable ASCII up to the first NUL, trimmed.
std::string Text(const Bytes& bytes, std::size_t at, std::size_t size) {
    std::string text;
    for (std::size_t i = at; i < at + size && i < bytes.size(); ++i) {
        if (bytes[i] == 0) {
            break;
        }
        text.push_back(bytes[i] >= 0x20 && bytes[i] < 0x7F ? static_cast<char>(bytes[i]) : ' ');
    }
    return Trim(text);
}

// A code is four letters or digits; anything else is a title's bytes or padding.
std::string Code(const Bytes& bytes, std::size_t at) {
    const std::string code = Text(bytes, at, 4);
    const bool ok = code.size() == 4 && std::all_of(code.begin(), code.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    });
    return ok ? code : std::string();
}

RomCode ReadN64(std::ifstream& in) {
    Bytes head = ReadAt(in, 0, 0x40);
    if (head.size() < 0x40) {
        return {};
    }
    if (head[0] == 0x37 && head[1] == 0x80) { // .v64: bytes swapped in pairs
        for (std::size_t i = 0; i + 1 < head.size(); i += 2) {
            std::swap(head[i], head[i + 1]);
        }
    } else if (head[0] == 0x40 && head[3] == 0x80) { // .n64: words little-endian
        for (std::size_t i = 0; i + 3 < head.size(); i += 4) {
            std::reverse(head.begin() + static_cast<std::ptrdiff_t>(i), head.begin() + static_cast<std::ptrdiff_t>(i + 4));
        }
    }
    if (head[0] != 0x80 || head[1] != 0x37) {
        return {};
    }
    return {Text(head, 0x20, 20), Code(head, 0x3B)};
}

RomCode ReadSnes(std::ifstream& in, std::uint64_t size) {
    const std::uint64_t skip = size % 1024 == 512 ? 512 : 0;
    for (const std::uint64_t at : {std::uint64_t{0x7FC0}, std::uint64_t{0xFFC0}}) {
        const Bytes head = ReadAt(in, skip + at, 0x20);
        if (head.size() < 0x20) {
            continue;
        }
        const unsigned complement = head[0x1C] | (head[0x1D] << 8), checksum = head[0x1E] | (head[0x1F] << 8);
        if ((complement ^ checksum) == 0xFFFF) {
            return {Text(head, 0, 21), {}};
        }
    }
    return {};
}

std::string Upper(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

} // namespace

RomCode ReadRomCode(const fs::path& rom, std::string_view system) {
    std::ifstream in(rom, std::ios::binary);
    if (!in) {
        return {};
    }
    if (system == "gba") {
        const Bytes head = ReadAt(in, 0xA0, 0x12);
        return {Text(head, 0, 12), Code(head, 0x0C)};
    }
    if (system == "gb" || system == "gbc") {
        const Bytes head = ReadAt(in, 0x134, 0x10);
        // 0x80 or 0xC0 at 0x143: a Game Boy Color cartridge, whose title is 11 bytes and a code.
        const bool color = head.size() == 0x10 && (head[0x0F] == 0x80 || head[0x0F] == 0xC0);
        const std::string code = color ? Code(head, 0x0B) : std::string();
        return {Text(head, 0, !code.empty() ? 11 : color ? 15 : 16), code};
    }
    if (system == "nds") {
        const Bytes head = ReadAt(in, 0, 0x10);
        return {Text(head, 0, 12), Code(head, 0x0C)};
    }
    if (system == "n64") {
        return ReadN64(in);
    }
    if (system == "snes") {
        std::error_code ec;
        return ReadSnes(in, fs::file_size(rom, ec));
    }
    if (system == "psx" || system == "ps2") {
        in.close();
        return {{}, ReadDiscSerial(rom)};
    }
    return {};
}

RomCode HeaderOf(const Entry& entry, std::string_view manifest_title) {
    RomCode code = entry.archive_member.empty() ? ReadRomCode(PathFromUtf8(entry.path), entry.system) : RomCode{};
    if (code.title.empty()) {
        code.title = std::string(manifest_title);
    }
    return code;
}

std::string GameOfCode(std::string_view system, std::string_view code) {
    if (code.size() == 10) { // a disc's serial: its region is in the letters, so only exactly
        for (const Known& known : KNOWN_CODES) {
            if (known.system == system && known.key == code) {
                return std::string(known.game);
            }
        }
        return {};
    }
    if (code.size() != 4) {
        return {};
    }
    // Game Boy Color codes can sit on a cartridge listed as Game Boy.
    const auto same_system = [&](std::string_view other) {
        return other == system || ((system == "gb" || system == "gbc") && (other == "gb" || other == "gbc"));
    };
    const Known* region = nullptr;
    for (const Known& known : KNOWN_CODES) {
        if (!same_system(known.system)) {
            continue;
        }
        if (known.key == code) {
            return std::string(known.game);
        }
        if (region == nullptr && known.key.substr(0, 3) == code.substr(0, 3)) {
            region = &known; // the game in another region: the box is the same game's
        }
    }
    return region != nullptr ? std::string(region->game) : std::string();
}

std::string GameOfTitle(std::string_view system, std::string_view title) {
    const std::string upper = Upper(Trim(std::string(title)));
    for (const Known& known : KNOWN_TITLES) {
        if ((known.system == system || ((system == "gb" || system == "gbc") && (known.system == "gb" || known.system == "gbc"))) &&
            Upper(known.key) == upper) {
            return std::string(known.game);
        }
    }
    return {};
}

namespace {

std::vector<std::string> Words(std::string_view name) {
    std::vector<std::string> words;
    std::string word;
    int depth = 0;
    const auto flush = [&] {
        const bool revision = (word.size() > 1 && word[0] == 'v' && std::isdigit(static_cast<unsigned char>(word[1]))) ||
                              word.starts_with("rev");
        const bool noise = word == "the" || word == "version" || word == "usa" || word == "europe" ||
                           word == "japan" || word == "world" || revision;
        if (!word.empty() && !noise) {
            words.push_back(word);
        }
        word.clear();
    };
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '(' || c == '[') {
            ++depth;
        } else if ((c == ')' || c == ']') && depth > 0) {
            --depth;
        } else if (depth == 0 && (std::isalnum(byte) || byte >= 0x80 ||
                                  (c == '.' && !word.empty() && std::isdigit(static_cast<unsigned char>(word.back()))))) {
            word.push_back(static_cast<char>(std::tolower(byte)));
            continue;
        }
        flush();
    }
    flush();
    std::sort(words.begin(), words.end());
    words.erase(std::unique(words.begin(), words.end()), words.end());
    return words;
}

} // namespace

bool NameOfGame(std::string_view name, std::string_view game) {
    const std::vector<std::string> mine = Words(name), its = Words(game);
    return !mine.empty() && std::includes(its.begin(), its.end(), mine.begin(), mine.end());
}

std::optional<Entry> FindHackBase(const std::vector<Entry>& games, std::string_view system, const fs::path& rom,
                                  std::string_view name, const RomCode& header) {
    // Homebrew leaves placeholder codes ("0000", "AAAA") that many games share.
    const std::string_view code = header.code;
    const bool placeholder = !code.empty() && (code.find_first_not_of(code[0]) == std::string_view::npos ||
                                               code.find_first_not_of("0123456789") == std::string_view::npos);
    if ((header.code.empty() && header.title.empty()) || placeholder) {
        return std::nullopt;
    }
    // Which of two files is the hack: the base is the code's known game, or (an unknown code)
    // the game whose whole name is in the hack's ("Space Quest" of "Space Quest Redux").
    const std::string known = !header.code.empty() ? GameOfCode(system, header.code) : GameOfTitle(system, header.title);
    if (!known.empty() && NameOfGame(name, known)) {
        return std::nullopt;
    }
    std::error_code ec;
    for (const Entry& game : games) {
        if (game.system != system || !game.hack_of.empty() || !game.archive_member.empty() ||
            fs::equivalent(PathFromUtf8(game.path), rom, ec) || NameOfGame(name, game.name) ||
            !NameOfGame(game.name, !known.empty() ? std::string_view(known) : name)) {
            continue;
        }
        const RomCode other = ReadRomCode(PathFromUtf8(game.path), game.system);
        const bool same = !header.code.empty() ? other.code == header.code
                                               : other.code.empty() && Upper(other.title) == Upper(header.title);
        if (same) {
            return game;
        }
    }
    return std::nullopt;
}

std::string FindBaseGame(const ThumbnailIndex& index, std::string_view system, std::string_view original_name,
                         const RomCode& header) {
    // A name from the lists, as the collection has it (its region or revision may differ).
    const auto fitted = [&](const std::string& game) -> std::string {
        if (game.empty()) {
            return {};
        }
        if (index.names.empty()) {
            return game;
        }
        return MatchThumbnail(index, game);
    };
    if (std::string game = fitted(GameOfCode(system, header.code)); !game.empty()) {
        return game;
    }
    if (std::string game = fitted(GameOfTitle(system, header.title)); !game.empty()) {
        return game;
    }
    if (std::string game = MatchBaseGame(index, original_name, header.title); !game.empty()) {
        return game;
    }
    // A hack named nothing like its game ("Gen 3 Evolved"): the header's title alone.
    return header.title.empty() ? std::string() : MatchBaseGame(index, header.title, header.title);
}

} // namespace den::library
