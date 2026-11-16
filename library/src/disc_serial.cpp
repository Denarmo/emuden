// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The boot serial of a PlayStation or PlayStation 2 disc image: SYSTEM.CNF in the disc's
// ISO 9660 file system names the program it boots ("BOOT2 = cdrom0:\SLUS_209.46;1").

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "den/base/files.h"
#include "den/library/rom_codes.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t PVD_SECTOR = 16;
constexpr std::uint32_t MAX_DIR_BYTES = 1u << 20;
constexpr std::uint32_t MAX_CNF_BYTES = 64u << 10;

// How 2048 bytes of user data sit in the image's sectors.
struct Layout {
    std::uint32_t sector = 2048; // bytes per sector in the file
    std::uint32_t offset = 0;    // where the user data starts in one
};

std::uint32_t Le32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool ReadData(std::ifstream& in, const Layout& layout, std::uint32_t lba, std::uint32_t bytes, std::vector<unsigned char>& out) {
    out.assign(bytes, 0);
    for (std::uint32_t done = 0; done < bytes;) {
        const std::uint32_t chunk = std::min<std::uint32_t>(2048, bytes - done);
        in.clear();
        in.seekg(static_cast<std::streamoff>(lba + done / 2048) * layout.sector + layout.offset);
        in.read(reinterpret_cast<char*>(out.data() + done), chunk);
        if (in.gcount() != static_cast<std::streamsize>(chunk)) {
            return false;
        }
        done += chunk;
    }
    return true;
}

// The layout whose sector 16 is a primary volume descriptor: a cooked ISO, or raw CD sectors
// of mode 1 (data after 16 bytes) or mode 2 (after 24), or 2336-byte mode 2 sectors.
std::optional<Layout> FindLayout(std::ifstream& in, std::vector<unsigned char>& pvd) {
    for (const Layout layout : {Layout{2048, 0}, Layout{2352, 24}, Layout{2352, 16}, Layout{2336, 8}}) {
        if (ReadData(in, layout, PVD_SECTOR, 2048, pvd) && pvd[0] == 1 &&
            std::equal(pvd.begin() + 1, pvd.begin() + 6, "CD001")) {
            return layout;
        }
    }
    return std::nullopt;
}

// The first data file a cue sheet or playlist names, beside it.
fs::path FirstTrack(const fs::path& sheet) {
    const auto text = ReadTextFile(sheet);
    if (!text) {
        return {};
    }
    const bool cue = ToLower(PathToUtf8(sheet.extension())) == ".cue";
    std::size_t start = 0;
    while (start < text->size()) {
        std::size_t end = text->find('\n', start);
        end = end == std::string::npos ? text->size() : end;
        std::string line = Trim(text->substr(start, end - start));
        start = end + 1;
        if (cue) {
            if (!ToLower(line).starts_with("file")) {
                continue;
            }
            const std::size_t open = line.find('"'), close = line.rfind('"');
            line = open != std::string::npos && close > open ? line.substr(open + 1, close - open - 1)
                                                             : Trim(line.substr(4, line.rfind(' ') - 4));
        } else if (line.empty() || line.starts_with('#')) {
            continue;
        }
        return sheet.parent_path() / PathFromUtf8(line);
    }
    return {};
}

std::string Upper(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

// "cdrom0:\SLUS_209.46;1" -> "SLUS-20946".
std::string SerialOf(std::string_view boot) {
    const std::size_t cut = boot.find_last_of("\\:/");
    std::string_view file = cut == std::string_view::npos ? boot : boot.substr(cut + 1);
    file = file.substr(0, file.find(';'));
    std::string letters, digits;
    for (const char c : file) {
        if (std::isalpha(static_cast<unsigned char>(c)) && digits.empty()) {
            letters.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            digits.push_back(c);
        }
    }
    return letters.size() == 4 && digits.size() == 5 ? letters + "-" + digits : std::string();
}

std::string BootSerial(std::string_view cnf) {
    std::size_t start = 0;
    while (start < cnf.size()) {
        std::size_t end = cnf.find_first_of("\r\n", start);
        end = end == std::string_view::npos ? cnf.size() : end;
        const std::string line = Trim(std::string(cnf.substr(start, end - start)));
        start = end + 1;
        const std::size_t equals = line.find('=');
        const std::string key = Upper(Trim(line.substr(0, equals)));
        if (equals != std::string::npos && (key == "BOOT" || key == "BOOT2")) {
            if (std::string serial = SerialOf(Trim(line.substr(equals + 1))); !serial.empty()) {
                return serial;
            }
        }
    }
    return {};
}

} // namespace

std::string ReadDiscSerial(const fs::path& disc) {
    const std::string extension = ToLower(PathToUtf8(disc.extension()));
    if (extension == ".cue" || extension == ".m3u") {
        const fs::path track = FirstTrack(disc);
        return track.empty() || ToLower(PathToUtf8(track.extension())) == ".m3u" ? std::string() : ReadDiscSerial(track);
    }
    std::ifstream in(disc, std::ios::binary);
    std::vector<unsigned char> pvd;
    const std::optional<Layout> layout = in ? FindLayout(in, pvd) : std::nullopt;
    if (!layout) {
        return {};
    }
    // The root directory's records: length, extent at 2, size at 10, flags at 25, name at 33.
    const unsigned char* root = pvd.data() + 156;
    const std::uint32_t root_size = std::min(Le32(root + 10), MAX_DIR_BYTES);
    std::vector<unsigned char> dir;
    if (!ReadData(in, *layout, Le32(root + 2), root_size, dir)) {
        return {};
    }
    for (std::size_t at = 0; at < dir.size();) {
        const unsigned char length = dir[at];
        if (length == 0) {
            at = (at / 2048 + 1) * 2048; // records do not cross sectors
            continue;
        }
        if (at + 33 > dir.size() || at + length > dir.size() || dir[at + 32] > length - 33) {
            break;
        }
        const std::string name(reinterpret_cast<const char*>(dir.data() + at + 33), dir[at + 32]);
        if ((dir[at + 25] & 0x02) == 0 && Upper(name.substr(0, name.find(';'))) == "SYSTEM.CNF") {
            std::vector<unsigned char> cnf;
            const std::uint32_t size = std::min(Le32(dir.data() + at + 10), MAX_CNF_BYTES);
            if (!ReadData(in, *layout, Le32(dir.data() + at + 2), size, cnf)) {
                return {};
            }
            return BootSerial(std::string_view(reinterpret_cast<const char*>(cnf.data()), cnf.size()));
        }
        at += length;
    }
    return {};
}

} // namespace den::library
