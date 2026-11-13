// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A 7z archive written byte for byte for tests: every file stored (the Copy method) in one
// solid block, the header not packed. What a 7z tool writes with other methods is in
// base/tests/data.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "den/base/files.h"
#include "den/base/utf8.h"

namespace den::test {

/// `files`: name (forward slashes, UTF-8) and content; no file may be empty.
inline std::string StoredSevenZip(const std::vector<std::pair<std::string, std::string>>& files) {
    const auto fixed = [](std::string& out, std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
        }
    };
    // The format's number: the count of leading 1 bits in the first byte says how many
    // bytes follow.
    const auto number = [&](std::string& out, std::uint64_t value) {
        for (int extra = 0; extra < 8; ++extra) {
            if (value < (1ull << (7 * (extra + 1)))) {
                out.push_back(static_cast<char>(((0xFF00 >> extra) & 0xFF) | (value >> (8 * extra))));
                fixed(out, value, extra);
                return;
            }
        }
        out.push_back(static_cast<char>(0xFF));
        fixed(out, value, 8);
    };

    std::string packed;
    for (const auto& file : files) {
        packed += file.second;
    }
    std::string header;
    header += '\x01';                    // Header
    header += '\x04';                    // MainStreamsInfo
    header += '\x06';                    // PackInfo
    number(header, 0);                   // the block starts right after the start header
    number(header, 1);                   // one packed stream
    header += '\x09';                    // its size
    number(header, packed.size());
    header += '\x00';
    header += '\x07';                    // UnpackInfo
    header += '\x0B';                    // Folder
    number(header, 1);
    header += '\x00';                    // not external
    number(header, 1);                   // one coder
    header += '\x01';                    // an id of one byte, no properties
    header += '\x00';                    // Copy
    header += '\x0C';                    // CodersUnpackSize
    number(header, packed.size());
    header += '\x00';
    header += '\x08';                    // SubStreamsInfo
    header += '\x0D';                    // NumUnpackStream
    number(header, files.size());
    if (files.size() > 1) {
        header += '\x09';                // every size but the last
        for (std::size_t i = 0; i + 1 < files.size(); ++i) {
            number(header, files[i].second.size());
        }
    }
    header += '\x0A';                    // CRC
    header += '\x01';                    // all defined
    for (const auto& file : files) {
        fixed(header, Crc32(file.second.data(), file.second.size()), 4);
    }
    header += '\x00';
    header += '\x00';                    // end of MainStreamsInfo
    header += '\x05';                    // FilesInfo
    number(header, files.size());
    std::string names;
    for (const auto& file : files) {
        for (std::size_t at = 0; at < file.first.size();) {
            const char32_t code = NextCodePoint(file.first, at);
            if (code >= 0x10000) {
                fixed(names, 0xD800 + ((code - 0x10000) >> 10), 2);
                fixed(names, 0xDC00 + ((code - 0x10000) & 0x3FF), 2);
            } else {
                fixed(names, code, 2);
            }
        }
        fixed(names, 0, 2);
    }
    header += '\x11';                    // Names
    number(header, names.size() + 1);
    header += '\x00';                    // not external
    header += names;
    header += '\x00';                    // end of FilesInfo
    header += '\x00';                    // end of Header

    std::string start;
    fixed(start, packed.size(), 8);
    fixed(start, header.size(), 8);
    fixed(start, Crc32(header.data(), header.size()), 4);
    std::string out("7z\xBC\xAF\x27\x1C", 6);
    out += '\x00';
    out += '\x04';
    fixed(out, Crc32(start.data(), start.size()), 4);
    return out + start + packed + header;
}

} // namespace den::test
