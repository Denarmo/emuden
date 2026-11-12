// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace den::plugins {

/// The official plugin source and the Ed25519 public keys (base64) its index is signed with.
inline constexpr const char* OFFICIAL_SOURCE = "github:Denarmo/emuden-plugins@main";
const std::vector<std::string>& OfficialKeys();

/// True when `signature_b64` is the Ed25519 signature of `data` under the public key `key_b64`
/// (32 raw bytes, base64).
bool VerifyEd25519(std::span<const std::uint8_t> data, std::string_view signature_b64,
                   std::string_view key_b64);

/// Standard base64 with padding; nullopt for anything else. Surrounding whitespace is ignored.
std::optional<std::vector<std::uint8_t>> DecodeBase64(std::string_view text);
std::string EncodeBase64(std::span<const std::uint8_t> data);

/// A key pair and a signature, for tests and tools that sign their own fixtures.
struct KeyPair {
    std::string public_b64;
    std::vector<std::uint8_t> secret; // 64 bytes
};
KeyPair KeyPairFromSeed(std::span<const std::uint8_t, 32> seed);
std::string SignEd25519(std::span<const std::uint8_t> data, const KeyPair& key);

} // namespace den::plugins
