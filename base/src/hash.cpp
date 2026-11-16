// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#include "den/base/hash.h"

namespace den {

namespace {

std::array<std::uint32_t, 256> MakeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        table[i] = c;
    }
    return table;
}

constexpr int MD5_SHIFT[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                               5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

std::array<std::uint32_t, 64> MakeMd5Table() {
    std::array<std::uint32_t, 64> k{};
    for (int i = 0; i < 64; ++i) {
        k[i] = static_cast<std::uint32_t>(std::floor(std::fabs(std::sin(i + 1.0)) * 4294967296.0));
    }
    return k;
}

std::uint32_t RotL(std::uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

} // namespace

std::uint32_t Crc32Update(std::uint32_t crc, const void* data, std::size_t size) {
    static const std::array<std::uint32_t, 256> table = MakeCrcTable();
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

std::optional<std::uint32_t> Crc32File(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::uint32_t crc = 0;
    std::vector<char> buffer(1u << 16);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        crc = Crc32Update(crc, buffer.data(), static_cast<std::size_t>(in.gcount()));
    }
    return in.bad() ? std::nullopt : std::optional<std::uint32_t>(crc);
}

Md5::Md5() : h{0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u} {}

void Md5::Block(const std::uint8_t* p) {
    static const std::array<std::uint32_t, 64> K = MakeMd5Table();
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<std::uint32_t>(p[i * 4]) | (static_cast<std::uint32_t>(p[i * 4 + 1]) << 8) |
               (static_cast<std::uint32_t>(p[i * 4 + 2]) << 16) |
               (static_cast<std::uint32_t>(p[i * 4 + 3]) << 24);
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const std::uint32_t next = b + RotL(a + f + K[i] + m[g], MD5_SHIFT[i]);
        a = d;
        d = c;
        c = b;
        b = next;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

void Md5::Update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total += size;
    while (size > 0) {
        const std::size_t take = std::min(size, pending.size() - pending_size);
        std::memcpy(pending.data() + pending_size, bytes, take);
        pending_size += take;
        bytes += take;
        size -= take;
        if (pending_size == pending.size()) {
            Block(pending.data());
            pending_size = 0;
        }
    }
}

std::string Md5::FinishHex() {
    std::array<std::uint8_t, 128> tail{};
    std::memcpy(tail.data(), pending.data(), pending_size);
    tail[pending_size] = 0x80;
    const std::size_t padded = pending_size + 9 <= 64 ? 64 : 128;
    const std::uint64_t bits = total * 8;
    for (int i = 0; i < 8; ++i) {
        tail[padded - 8 + i] = static_cast<std::uint8_t>(bits >> (8 * i));
    }
    Block(tail.data());
    if (padded == 128) {
        Block(tail.data() + 64);
    }
    static constexpr char HEX[] = "0123456789abcdef";
    std::string out;
    for (const std::uint32_t word : h) {
        for (int i = 0; i < 4; ++i) {
            const std::uint8_t byte = static_cast<std::uint8_t>(word >> (8 * i));
            out.push_back(HEX[byte >> 4]);
            out.push_back(HEX[byte & 0xF]);
        }
    }
    return out;
}

std::string Md5Hex(const void* data, std::size_t size) {
    Md5 hash;
    hash.Update(data, size);
    return hash.FinishHex();
}

std::string Sha256Hex(const void* data, std::size_t size) {
    Sha256 hash;
    hash.Update(data, size);
    return hash.FinishHex();
}

} // namespace den
