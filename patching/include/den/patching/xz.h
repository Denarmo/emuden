// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A .xz file read from its start to its end (XZ Embedded): one stream, LZMA2 without
// filters, a dictionary of at most 64 MiB.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

struct xz_dec;

namespace den::patching {

/// The file starts like a .xz.
bool IsXzFile(const std::filesystem::path& file);

class XzFileReader {
public:
    explicit XzFileReader(const std::filesystem::path& file);
    ~XzFileReader();
    XzFileReader(const XzFileReader&) = delete;
    XzFileReader& operator=(const XzFileReader&) = delete;

    /// Fills `out`; `got` is less than `size` only at the end of the data. False when the
    /// file cannot be read or is damaged.
    bool Read(std::uint8_t* out, std::size_t size, std::size_t& got);

private:
    std::ifstream in_;
    std::vector<std::uint8_t> buffer_;
    std::size_t at_ = 0, filled_ = 0;
    xz_dec* state_ = nullptr;
    bool ended_ = false, failed_ = false;
};

} // namespace den::patching
