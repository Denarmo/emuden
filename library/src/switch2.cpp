// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Switch 2 packages (.nup): a superblock, a table of contents with one record per package
// (base game, update, add-on) and per-package metadata as type-length-value records. All
// integers are little-endian.

#include <array>
#include <cstring>
#include <fstream>

#include "den/library/scan.h"

namespace den::library {

namespace {

constexpr std::size_t SUPERBLOCK_SIZE = 0x80;
constexpr std::size_t TOC_HEADER_SIZE = 0x30;
constexpr std::size_t PACKAGE_RECORD_SIZE = 0x40;
constexpr std::uint32_t MAX_PACKAGES = 4096;
constexpr std::uint32_t MAX_META = 32u << 20;

constexpr std::uint16_t META_ICON = 0x0003;
constexpr std::uint16_t META_TITLE = 0x0005;

template <typename T>
T Read(const std::uint8_t* at) {
    T value{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(at[i]) << (8 * i);
    }
    return value;
}

bool ReadAt(std::ifstream& file, std::uint64_t offset, void* out, std::size_t size) {
    file.clear();
    return file.seekg(static_cast<std::streamoff>(offset)) &&
           file.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
}

struct Package {
    std::uint64_t title_id = 0;
    std::uint8_t kind = 0; // 0 base, 1 update, 2 add-on
    std::uint64_t meta_offset = 0;
    std::uint32_t meta_size = 0;
};

// The known games, for files that carry no title of their own.
struct KnownTitle {
    std::uint64_t id;
    std::string_view title;
};
constexpr KnownTitle KNOWN_TITLES[] = {
    {0x040074A01BF12000, "Donkey Kong Bananza"},
    {0x040077201BE50000, "Kirby Air Riders"},
    {0x04007C6028AA8000, "Yoshi and the Mysterious Book (Demo)"},
    {0x0400D4201C6EA000, "Mario Tennis Fever"},
    {0x0400E88025B1A000, "Star Fox"},
    {0x04007EF01F8AE000, "Fire Emblem: Fortune's Weave"},
    {0x0400C3F00006E000, "Mario Kart World"},
    {0x04008E001F098000, "Nintendo Switch Sports Resort"},
    {0x0400A3A01EE4C000, "Pok\xC3\xA9mon Pokopia"},
    {0x0400A4802437E000, "Drag x Drive Global Jam"},
    {0x040041E0212AE000, "Fast Fusion"},
    {0x04005B902517C000, "Kirby Air Riders Global Test Ride"},
    {0x040057C02159A000, "Nintendo Switch 2 Welcome Tour"},
    {0x040004F027560000, "Orbitals Global Friends Pass"},
    {0x0400380020056000, "Raiders"},
    {0x0400D1D02399A000, "The Duskbloods Network Test"},
    {0x0400486024DEC000, "The Legend of Zelda: Ocarina of Time"},
    {0x0400245021D2C000, "Yoshi and the Mysterious Book"},
    {0x0400DDB020648000, "Cyberpunk 2077: Ultimate Edition"},
    {0x0100606026F12000, "Final Fantasy Resonance"},
};

int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

std::optional<Switch2Info> ReadSwitch2Package(const std::filesystem::path& path, bool with_icon) {
    std::ifstream file(path, std::ios::binary);
    std::array<std::uint8_t, SUPERBLOCK_SIZE> super{};
    if (!file || !ReadAt(file, 0, super.data(), super.size()) ||
        std::memcmp(super.data(), "NUP\x01", 4) != 0) {
        return std::nullopt;
    }
    const auto toc_offset = Read<std::uint64_t>(super.data() + 0x08);
    std::array<std::uint8_t, TOC_HEADER_SIZE> toc{};
    if (!ReadAt(file, toc_offset, toc.data(), toc.size()) || std::memcmp(toc.data(), "TOC\x01", 4) != 0) {
        return std::nullopt;
    }
    const auto count = Read<std::uint32_t>(toc.data() + 0x04);
    if (count == 0 || count > MAX_PACKAGES) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> records(static_cast<std::size_t>(count) * PACKAGE_RECORD_SIZE);
    if (!ReadAt(file, toc_offset + TOC_HEADER_SIZE, records.data(), records.size())) {
        return std::nullopt;
    }
    std::vector<Package> packages;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* record = records.data() + i * PACKAGE_RECORD_SIZE;
        Package package;
        package.title_id = Read<std::uint64_t>(record);
        package.kind = record[0x18];
        package.meta_offset = Read<std::uint64_t>(record + 0x20);
        package.meta_size = Read<std::uint32_t>(record + 0x28);
        packages.push_back(package);
    }
    // The base game names the entry; an update's picture and title are the newer ones.
    const Package* base = &packages.front();
    for (const Package& package : packages) {
        if (package.kind == 0) {
            base = &package;
            break;
        }
    }
    Switch2Info info;
    info.program_id = base->title_id;
    for (const Package& package : packages) {
        if (package.kind == 2 || package.meta_offset == 0 || package.meta_size == 0 ||
            package.meta_size > MAX_META) {
            continue;
        }
        std::vector<std::uint8_t> meta(package.meta_size);
        if (!ReadAt(file, package.meta_offset, meta.data(), meta.size())) {
            continue;
        }
        std::size_t at = 0;
        while (at + 8 <= meta.size()) {
            const auto type = Read<std::uint16_t>(meta.data() + at);
            const auto size = Read<std::uint32_t>(meta.data() + at + 4);
            at += 8;
            if (size > meta.size() - at) {
                break;
            }
            const char* payload = reinterpret_cast<const char*>(meta.data() + at);
            if (type == META_TITLE && size != 0) {
                info.title.assign(payload, size);
            } else if (type == META_ICON && size != 0 && with_icon) {
                info.icon.assign(payload, size);
            }
            at += size;
        }
    }
    while (!info.title.empty() && info.title.back() == '\0') {
        info.title.pop_back();
    }
    return info;
}

std::uint64_t ProgramIdFromName(std::string_view name) {
    for (std::size_t open = name.find('['); open != std::string_view::npos;
         open = name.find('[', open + 1)) {
        const std::size_t close = name.find(']', open);
        if (close == std::string_view::npos) {
            break;
        }
        const std::string_view tag = name.substr(open + 1, close - open - 1);
        if (tag.size() != 16) {
            continue;
        }
        std::uint64_t id = 0;
        bool hex = true;
        for (const char c : tag) {
            const int digit = HexDigit(c);
            hex = hex && digit >= 0;
            id = (id << 4) | static_cast<std::uint64_t>(digit & 0xF);
        }
        if (hex && id != 0) {
            return id;
        }
    }
    return 0;
}

std::string_view KnownSwitch2Title(std::uint64_t program_id) {
    for (const KnownTitle& known : KNOWN_TITLES) {
        if (known.id == program_id) {
            return known.title;
        }
    }
    return {};
}

std::string Switch2Id(std::uint64_t program_id) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string hex(16, '0');
    for (int i = 0; i < 16; ++i) {
        hex[15 - i] = HEX[(program_id >> (4 * i)) & 0xF];
    }
    return "switch2:" + hex;
}

} // namespace den::library
