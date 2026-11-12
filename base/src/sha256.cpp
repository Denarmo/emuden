// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include "den/base/sha256.h"

namespace den {

namespace {

constexpr std::array<std::uint32_t, 64> K{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

std::uint32_t Rotr(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

} // namespace

Sha256::Sha256()
    : h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::Block(const std::uint8_t* p) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t{p[i * 4]} << 24) | (std::uint32_t{p[i * 4 + 1]} << 16) |
               (std::uint32_t{p[i * 4 + 2]} << 8) | std::uint32_t{p[i * 4 + 3]};
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::array<std::uint32_t, 8> v = h;
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = Rotr(v[4], 6) ^ Rotr(v[4], 11) ^ Rotr(v[4], 25);
        const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t t1 = v[7] + s1 + ch + K[i] + w[i];
        const std::uint32_t s0 = Rotr(v[0], 2) ^ Rotr(v[0], 13) ^ Rotr(v[0], 22);
        const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const std::uint32_t t2 = s0 + maj;
        v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
    }
    for (int i = 0; i < 8; ++i) {
        h[i] += v[i];
    }
}

void Sha256::Update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total += size;
    if (pending_size > 0) {
        const std::size_t take = std::min(size, pending.size() - pending_size);
        std::memcpy(pending.data() + pending_size, bytes, take);
        pending_size += take;
        bytes += take;
        size -= take;
        if (pending_size < pending.size()) {
            return;
        }
        Block(pending.data());
        pending_size = 0;
    }
    for (; size >= 64; bytes += 64, size -= 64) {
        Block(bytes);
    }
    if (size > 0) {
        std::memcpy(pending.data(), bytes, size);
        pending_size = size;
    }
}

std::string Sha256::FinishHex() {
    std::array<std::uint8_t, 128> tail{};
    std::memcpy(tail.data(), pending.data(), pending_size);
    tail[pending_size] = 0x80;
    const std::size_t padded = pending_size + 9 <= 64 ? 64 : 128;
    const std::uint64_t bits = total * 8;
    for (int i = 0; i < 8; ++i) {
        tail[padded - 1 - i] = static_cast<std::uint8_t>(bits >> (8 * i));
    }
    Block(tail.data());
    if (padded == 128) {
        Block(tail.data() + 64);
    }
    static constexpr char HEX[] = "0123456789abcdef";
    std::string out;
    for (const std::uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(HEX[(word >> shift) & 0xF]);
        }
    }
    return out;
}

std::optional<std::string> Sha256File(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    Sha256 hash;
    std::vector<char> buffer(1u << 16);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hash.Update(buffer.data(), static_cast<std::size_t>(in.gcount()));
    }
    if (in.bad()) {
        return std::nullopt;
    }
    return hash.FinishHex();
}

} // namespace den
