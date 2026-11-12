// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace den {

/// SHA-256 fed in pieces, so a download is hashed while it is written.
class Sha256 {
public:
    Sha256();
    void Update(const void* data, std::size_t size);
    /// Lower-case hex. The object is finished afterwards.
    std::string FinishHex();

private:
    void Block(const std::uint8_t* p);

    std::array<std::uint32_t, 8> h;
    std::array<std::uint8_t, 64> pending{};
    std::size_t pending_size = 0;
    std::uint64_t total = 0;
};

/// SHA-256 of a whole file, nullopt when it cannot be read.
std::optional<std::string> Sha256File(const std::filesystem::path& file);

} // namespace den
