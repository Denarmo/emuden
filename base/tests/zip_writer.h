// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A zip archive written byte for byte for tests, every file stored (not compressed).

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "den/base/files.h"

namespace den::test {

inline std::string StoredZipFiles(const std::vector<std::pair<std::string, std::string>>& files) {
    const auto le = [](std::string& out, std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
        }
    };
    std::string out, central;
    for (const auto& [name, data] : files) {
        const std::uint32_t crc = Crc32(data.data(), data.size());
        const std::size_t offset = out.size();
        le(out, 0x04034b50, 4), le(out, 20, 2), le(out, 0, 2), le(out, 0, 2), le(out, 0, 4);
        le(out, crc, 4), le(out, data.size(), 4), le(out, data.size(), 4);
        le(out, name.size(), 2), le(out, 0, 2);
        out += name + data;
        le(central, 0x02014b50, 4), le(central, 20, 2), le(central, 20, 2), le(central, 0, 2);
        le(central, 0, 2), le(central, 0, 4), le(central, crc, 4);
        le(central, data.size(), 4), le(central, data.size(), 4), le(central, name.size(), 2);
        le(central, 0, 2), le(central, 0, 2), le(central, 0, 2), le(central, 0, 2), le(central, 0, 4);
        le(central, offset, 4);
        central += name;
    }
    const std::size_t start = out.size();
    out += central;
    le(out, 0x06054b50, 4), le(out, 0, 2), le(out, 0, 2), le(out, files.size(), 2);
    le(out, files.size(), 2), le(out, central.size(), 4), le(out, start, 4), le(out, 0, 2);
    return out;
}

} // namespace den::test
