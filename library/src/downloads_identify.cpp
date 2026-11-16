// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What a downloaded file is: a game of which console, a patch, an archive, a BIOS file or
// something else. The extension decides when only one console takes it; otherwise the file's
// first bytes, then the words around it (folders, the archive's and the page's names).

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>

#include "den/base/archive.h"
#include "den/base/files.h"
#include "den/library/downloads.h"
#include "den/library/scan.h"
#include "den/patching/patch.h"

namespace den::library::downloads {

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<std::uint8_t>;

// The bytes at `offset`, fewer at the end of the file.
Bytes ReadAt(std::ifstream& in, std::uint64_t offset, std::size_t size) {
    Bytes out(size);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    out.resize(static_cast<std::size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    return out;
}

bool Has(const Bytes& data, std::size_t at, std::string_view text) {
    return !text.empty() && data.size() >= at + text.size() && std::memcmp(data.data() + at, text.data(), text.size()) == 0;
}

bool HasBytes(const Bytes& data, std::size_t at, std::initializer_list<std::uint8_t> bytes) {
    if (data.size() < at + bytes.size()) {
        return false;
    }
    return std::equal(bytes.begin(), bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(at));
}

std::uint32_t Le32(const Bytes& data, std::size_t at) {
    return data.size() < at + 4 ? 0
                                : static_cast<std::uint32_t>(data[at]) | static_cast<std::uint32_t>(data[at + 1]) << 8 |
                                      static_cast<std::uint32_t>(data[at + 2]) << 16 |
                                      static_cast<std::uint32_t>(data[at + 3]) << 24;
}

std::uint64_t Be64(const Bytes& data, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8 && at + i < data.size(); ++i) {
        value = value << 8 | data[at + i];
    }
    return value;
}

bool Wanted(const std::vector<std::string>& candidates, std::string_view system) {
    return candidates.empty() || std::find(candidates.begin(), candidates.end(), system) != candidates.end();
}

// A disc image: the console from its first sectors. 2048-byte sectors or raw 2352-byte ones.
std::string SniffDisc(std::ifstream& in, std::uint64_t size, const std::vector<std::string>& candidates) {
    const Bytes start = ReadAt(in, 0, 0x40);
    const bool raw = HasBytes(start, 0, {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00});
    const std::size_t head = raw ? 16 : 0;
    if (Has(start, head, "SEGA SEGASATURN") && Wanted(candidates, "saturn")) {
        return "saturn";
    }
    if (Has(start, head, "SEGA SEGAKATANA") && Wanted(candidates, "dreamcast")) {
        return "dreamcast";
    }
    if (Has(start, head, "SEGADISCSYSTEM") && Wanted(candidates, "segacd")) {
        return "segacd";
    }
    // The ISO 9660 primary volume descriptor in sector 16; mode 1 and mode 2 raw sectors.
    for (const std::uint64_t at : raw ? std::array<std::uint64_t, 2>{16 * 2352 + 16, 16 * 2352 + 24}
                                      : std::array<std::uint64_t, 2>{16 * 2048, 16 * 2048}) {
        const Bytes pvd = ReadAt(in, at, 48);
        if (!Has(pvd, 0, "\x01" "CD001")) {
            continue;
        }
        const std::string system_id(pvd.begin() + 8, pvd.begin() + 40);
        if (system_id.starts_with("PSP GAME") && Wanted(candidates, "psp")) {
            return "psp";
        }
        if (system_id.starts_with("PLAYSTATION")) {
            // The second console's games are DVDs; a CD holds at most about 800 MB.
            const bool dvd = size > (900ull << 20);
            if (dvd && Wanted(candidates, "ps2")) {
                return "ps2";
            }
            if (Wanted(candidates, "psx")) {
                return "psx";
            }
        }
    }
    return {};
}

// What a CHD's track metadata says: CD tracks of mode 2 are PlayStation, of mode 1 Saturn,
// GD-ROM tracks Dreamcast, a DVD the second PlayStation.
std::string SniffChd(std::ifstream& in, const std::vector<std::string>& candidates) {
    const Bytes header = ReadAt(in, 0, 0x7C);
    if (!Has(header, 0, "MComprHD") || Be64(header, 8) >> 32 < 5) {
        return {};
    }
    std::uint64_t meta = Be64(header, 0x30);
    for (int i = 0; meta != 0 && i < 128; ++i) {
        const Bytes entry = ReadAt(in, meta, 16);
        if (entry.size() < 16) {
            break;
        }
        const std::string tag(entry.begin(), entry.begin() + 4);
        const std::size_t length = static_cast<std::size_t>(entry[5]) << 16 | entry[6] << 8 | entry[7];
        const Bytes text = ReadAt(in, meta + 24, std::min<std::size_t>(length, 256));
        const std::string value(text.begin(), text.end());
        if (tag == "CHGD" && Wanted(candidates, "dreamcast")) {
            return "dreamcast";
        }
        if (tag == "DVD " && Wanted(candidates, "ps2")) {
            return "ps2";
        }
        if (tag == "CHT2" || tag == "CHTR") {
            if (value.find("MODE2") != std::string::npos && Wanted(candidates, "psx")) {
                return "psx";
            }
            if (value.find("MODE1") != std::string::npos && Wanted(candidates, "saturn")) {
                return "saturn";
            }
        }
        meta = Be64(entry, 8);
    }
    return {};
}

// A patch under another name: its magic.
bool LooksLikePatch(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    const Bytes head = ReadAt(in, 0, 8);
    return Has(head, 0, "PATCH") || Has(head, 0, "UPS1") || Has(head, 0, "BPS1") || Has(head, 0, "APS1") ||
           Has(head, 0, "PPF") || HasBytes(head, 0, {0xD6, 0xC3, 0xC4});
}

} // namespace

std::string SniffSystem(const fs::path& file, const std::vector<std::string>& candidates) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::error_code ec;
    const std::uint64_t size = fs::file_size(file, ec);
    const Bytes head = ReadAt(in, 0, 0x200);
    const auto pick = [&](std::string_view system) { return Wanted(candidates, system) ? std::string(system) : std::string(); };
    // Cartridges: the console's own header.
    if (HasBytes(head, 0x04, {0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21}) && head.size() > 0xB2 && head[0xB2] == 0x96) {
        return pick("gba");
    }
    if (HasBytes(head, 0xC0, {0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21})) {
        return pick("nds");
    }
    if (HasBytes(head, 0x104, {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B})) {
        return pick(head.size() > 0x143 && (head[0x143] & 0x80) ? "gbc" : "gb");
    }
    if (Has(head, 0, "NES\x1A")) {
        return pick("nes");
    }
    if (HasBytes(head, 0, {0x80, 0x37, 0x12, 0x40}) || HasBytes(head, 0, {0x37, 0x80, 0x40, 0x12}) ||
        HasBytes(head, 0, {0x40, 0x12, 0x37, 0x80})) {
        return pick("n64");
    }
    if (Has(head, 0x100, "SEGA 32X")) {
        return pick("32x");
    }
    if (Has(head, 0x100, "SEGA")) {
        return pick("genesis");
    }
    if (Has(head, 1, "ATARI7800")) {
        return pick("atari7800");
    }
    if (Has(head, 0, "LYNX")) {
        return pick("lynx");
    }
    if (Has(head, 0, "g GCE")) {
        return pick("vectrex");
    }
    if (HasBytes(head, 0x1C, {0xC2, 0x33, 0x9F, 0x3D})) {
        return pick("gamecube");
    }
    if (HasBytes(head, 0x18, {0x5D, 0x1C, 0x9E, 0xA3}) || Has(head, 0, "WBFS")) {
        return pick("wii");
    }
    if (Has(head, 0, std::string_view("\0PBP", 4))) {
        // A PlayStation game converted for the handheld carries a disc image in DATA.PSAR.
        const Bytes psar = ReadAt(in, Le32(head, 0x24), 12);
        return pick(Has(psar, 0, "PSISOIMG") || Has(psar, 0, "PSTITLEIMG") ? "psx" : "psp");
    }
    if (Has(head, 0, "MComprHD")) {
        return SniffChd(in, candidates);
    }
    for (const std::uint64_t at : {0x7FF0ull, 0x3FF0ull, 0x1FF0ull}) {
        const Bytes tmr = ReadAt(in, at, 16);
        if (Has(tmr, 0, "TMR SEGA")) {
            const int region = tmr.size() == 16 ? tmr[15] >> 4 : 0;
            return pick(region >= 5 && region <= 7 ? "gg" : "sms");
        }
    }
    if (std::string disc = SniffDisc(in, size, candidates); !disc.empty()) {
        return disc;
    }
    if (Has(head, 0, "AB") && size <= (4u << 20) && Wanted(candidates, "msx")) {
        return "msx";
    }
    // A plain Atari 2600 cartridge: no header at all, one of the sizes it came in.
    if (!candidates.empty() && Wanted(candidates, "atari2600") &&
        (size == 2048 || size == 4096 || size == 8192 || size == 12288 || size == 16384 || size == 32768)) {
        return "atari2600";
    }
    return {};
}

bool IsBiosName(const std::string& file_name) {
    const std::string name = ToLower(file_name);
    if (name.find("[bios]") != std::string::npos || name.find("(bios)") != std::string::npos) {
        return true;
    }
    const std::size_t dot = name.rfind('.');
    const std::string stem = name.substr(0, dot);
    const std::string extension = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    static constexpr std::string_view EXACT[] = {
        "gba_bios", "gb_bios", "gbc_bios", "sgb_bios", "sgb2_bios", "bios7", "bios9", "firmware",
        "dsi_bios7", "dsi_bios9", "dsi_firmware", "dsi_nand", "dc_boot", "dc_flash", "dc_bios", "naomi_boot",
        "saturn_bios", "sega_101", "mpr-17933", "mpr-18811-mx", "mpr-19367-mx", "bios_cd_u", "bios_cd_e",
        "bios_cd_j", "bios_md", "bios_gg", "bios_e", "bios_j", "bios_u", "syscard1", "syscard2", "syscard3",
        "gexpress", "disksys", "5200", "7800 bios (u)", "7800 bios (e)", "lynxboot", "ps1_rom", "psxonpsp660",
        "kick13", "kick20", "kick31", "ipl", "bios", "panafz1", "panafz10", "goldstar", "cpc464", "msx2",
        "msx2ext", "neogeo", "pgm", "naomi", "awbios", "skns", "stvbios", "jagboot", "pokemini_bios",
        "bios_mo5", "scph-1001", "rom1", "rom2", "segaboot",
    };
    if (std::find(std::begin(EXACT), std::end(EXACT), stem) != std::end(EXACT) &&
        (extension == "bin" || extension == "rom" || extension == "img" || extension == "zip" || extension == "pce" ||
         extension == "min" || extension == "gb" || extension == "gba" || extension == "nds" || extension.empty())) {
        // "bios.zip" and "neogeo.zip" are arcade BIOS sets; a game is rarely called that.
        return true;
    }
    // PlayStation BIOS dumps: scph1001.bin, scph-7502.bin, SCPH5501.BIN, ps2-0230a-20080220.bin.
    if (stem.starts_with("scph") && (extension == "bin" || extension == "rom" || extension.empty())) {
        return true;
    }
    if (stem.starts_with("ps2-") && stem.size() > 8 && extension == "bin") {
        return true;
    }
    return (stem.ends_with("_bios") || stem.starts_with("bios_") || stem.ends_with(" bios")) &&
           (extension == "bin" || extension == "rom" || extension == "img");
}

Identity Identify(const fs::path& file, const SystemTable& systems, const std::vector<std::string>& hints,
                  const std::string& name) {
    const std::string file_name = name.empty() ? PathToUtf8(file.filename()) : name;
    const std::string lower = ToLower(file_name);
    if (IsBiosName(file_name)) {
        return {Kind::Bios, {}, {}};
    }
    const std::string extension = systems.RomExtension(lower);
    if (patching::IsPatchExtension(extension) || LooksLikePatch(file)) {
        return {Kind::Patch, {}, {}};
    }
    if (IsArchiveExtension(extension) || IsSevenZip(file)) {
        return {Kind::Archive, {}, {}};
    }
    // Every console that takes the extension, and those for which it means only them.
    std::vector<std::string> candidates, strong;
    for (const SystemDef& def : systems.All()) {
        if (std::find(def.extensions.begin(), def.extensions.end(), extension) == def.extensions.end()) {
            continue;
        }
        candidates.push_back(def.id);
        if (std::find(def.folder_only_extensions.begin(), def.folder_only_extensions.end(), extension) ==
            def.folder_only_extensions.end()) {
            strong.push_back(def.id);
        }
    }
    if (strong.size() == 1 && candidates.size() == 1 && extension != "bin" && extension != "iso" && extension != "img") {
        return {Kind::Game, strong.front(), {}};
    }
    // A cue sheet or a disc list: what its first file holds.
    fs::path probe = file;
    if (extension == "gdi" && Wanted(candidates, "dreamcast")) {
        return {Kind::Game, "dreamcast", {}};
    }
    if (extension == "cue" || extension == "m3u") {
        if (const auto named = FilesNamedBy(file); !named.empty()) {
            probe = PathFromUtf8(named.front());
            if (!IsRegularFile(probe)) {
                // The key is lower case; find the file of that name beside the sheet.
                for (const fs::path& sibling : ListDirectory(file.parent_path())) {
                    if (ToLower(PathToUtf8(sibling.filename())) == ToLower(PathToUtf8(probe.filename()))) {
                        probe = sibling;
                    }
                }
            }
        }
    }
    // The bytes: a console's header, or a disc's track under a cartridge's extension (".bin").
    if (const SystemDef* sniffed = systems.Find(SniffSystem(probe, {}))) {
        const bool disc = std::find(sniffed->extensions.begin(), sniffed->extensions.end(), "cue") != sniffed->extensions.end();
        if (candidates.empty() || Wanted(candidates, sniffed->id) || (disc && extension != "cue" && extension != "m3u")) {
            return {Kind::Game, sniffed->id, {}};
        }
    }
    if (std::string sniffed = SniffSystem(probe, candidates); !sniffed.empty() && systems.Find(sniffed) != nullptr) {
        return {Kind::Game, sniffed, {}};
    }
    // The words around the file: a folder named for the console, or its name in the title.
    for (const std::string& hint : hints) {
        const std::string text = " " + ToLower(hint) + " ";
        std::string best;
        std::size_t best_length = 0;
        for (const std::string& id : candidates) {
            const SystemDef* def = systems.Find(id);
            std::vector<std::string> names = def->folder_aliases;
            names.push_back(def->id);
            names.push_back(ToLower(def->name));
            if (!def->short_name.empty()) {
                names.push_back(ToLower(def->short_name));
            }
            for (const std::string& word : names) {
                const std::size_t at = text.find(word);
                const auto edge = [&](std::size_t i) { return !std::isalnum(static_cast<unsigned char>(text[i])); };
                if (!word.empty() && word.size() > best_length && at != std::string::npos && edge(at - 1) &&
                    edge(at + word.size())) {
                    best = id;
                    best_length = word.size();
                }
            }
        }
        if (!best.empty()) {
            return {Kind::Game, best, {}};
        }
    }
    if (strong.size() == 1) {
        return {Kind::Game, strong.front(), {}};
    }
    if (candidates.empty()) {
        return {Kind::Unknown, {}, "EmuDEN does not know what kind of file this is"};
    }
    std::string names;
    for (const std::string& id : candidates) {
        names += (names.empty() ? "" : ", ") + systems.Find(id)->name;
    }
    return {Kind::Unknown, {}, "it could be a game for " + names + ", and nothing in it says which"};
}

std::string HeaderTitle(const fs::path& file, const std::string& system) {
    struct Place {
        std::string_view system;
        std::uint64_t offset;
        std::size_t size;
    };
    static constexpr Place PLACES[] = {{"gb", 0x134, 16}, {"gbc", 0x134, 15}, {"gba", 0xA0, 12},
                                       {"nds", 0x00, 12}, {"n64", 0x20, 20}};
    for (const Place& place : PLACES) {
        if (place.system != system) {
            continue;
        }
        std::ifstream in(file, std::ios::binary);
        if (system == "n64" && !HasBytes(ReadAt(in, 0, 4), 0, {0x80, 0x37, 0x12, 0x40})) {
            return {}; // only the big-endian dump has the name in order
        }
        const Bytes bytes = ReadAt(in, place.offset, place.size);
        std::string text;
        for (const std::uint8_t byte : bytes) {
            if (byte == 0) {
                break;
            }
            text.push_back(byte >= 0x20 && byte < 0x7F ? static_cast<char>(byte) : ' ');
        }
        return text;
    }
    return {};
}

} // namespace den::library::downloads
