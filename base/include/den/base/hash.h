// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "den/base/files.h"
#include "den/base/sha256.h"

namespace den {

/// CRC-32 (zip, No-Intro) continued from `crc`, so a file can be fed in pieces. Start with 0.
std::uint32_t Crc32Update(std::uint32_t crc, const void* data, std::size_t size);
std::optional<std::uint32_t> Crc32File(const std::filesystem::path& file);

/// MD5 fed in pieces (RetroAchievements and old DAT files identify games by it).
class Md5 {
public:
    Md5();
    void Update(const void* data, std::size_t size);
    /// Lower-case hex. The object is finished afterwards.
    std::string FinishHex();

private:
    void Block(const std::uint8_t* p);

    std::array<std::uint32_t, 4> h;
    std::array<std::uint8_t, 64> pending{};
    std::size_t pending_size = 0;
    std::uint64_t total = 0;
};

std::string Md5Hex(const void* data, std::size_t size);
std::string Sha256Hex(const void* data, std::size_t size);

} // namespace den
