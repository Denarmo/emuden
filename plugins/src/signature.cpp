// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include "den/base/files.h"
#include "den/plugins/signature.h"

extern "C" {
#include "third_party/monocypher/monocypher-ed25519.h"
}

namespace den::plugins {

const std::vector<std::string>& OfficialKeys() {
    // The content of the source's keys/source.pub, and a spare.
    static const std::vector<std::string> keys = {
        "DpeFj9fFiUNYnkAvvJCIMmtSH4QnFoS9EaI8spwQwyQ=",
        "Qk/CY7aA9TbBoUs/lJ1+pJqUKSVBRSV05f8SkDW8/u8=",
    };
    return keys;
}

std::optional<std::vector<std::uint8_t>> DecodeBase64(std::string_view text) {
    const std::string trimmed = Trim(text);
    if (trimmed.empty() || trimmed.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(trimmed.size() / 4 * 3);
    for (std::size_t i = 0; i < trimmed.size(); i += 4) {
        std::uint32_t group = 0;
        int padding = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            const char c = trimmed[i + j];
            int value = 0;
            if (c >= 'A' && c <= 'Z') {
                value = c - 'A';
            } else if (c >= 'a' && c <= 'z') {
                value = c - 'a' + 26;
            } else if (c >= '0' && c <= '9') {
                value = c - '0' + 52;
            } else if (c == '+') {
                value = 62;
            } else if (c == '/') {
                value = 63;
            } else if (c == '=' && i + 4 == trimmed.size() && j >= 2) {
                ++padding;
            } else {
                return std::nullopt;
            }
            if (padding > 0 && c != '=') {
                return std::nullopt;
            }
            group = (group << 6) | static_cast<std::uint32_t>(value);
        }
        out.push_back(static_cast<std::uint8_t>(group >> 16));
        if (padding < 2) {
            out.push_back(static_cast<std::uint8_t>(group >> 8));
        }
        if (padding < 1) {
            out.push_back(static_cast<std::uint8_t>(group));
        }
    }
    return out;
}

std::string EncodeBase64(std::span<const std::uint8_t> data) {
    static const char ALPHABET[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::size_t left = data.size() - i;
        const std::uint32_t group = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (left > 1 ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0) |
                                    (left > 2 ? static_cast<std::uint32_t>(data[i + 2]) : 0);
        out.push_back(ALPHABET[group >> 18]);
        out.push_back(ALPHABET[(group >> 12) & 63]);
        out.push_back(left > 1 ? ALPHABET[(group >> 6) & 63] : '=');
        out.push_back(left > 2 ? ALPHABET[group & 63] : '=');
    }
    return out;
}

bool VerifyEd25519(std::span<const std::uint8_t> data, std::string_view signature_b64,
                   std::string_view key_b64) {
    const auto key = DecodeBase64(key_b64);
    const auto signature = DecodeBase64(signature_b64);
    if (!key || key->size() != 32 || !signature || signature->size() != 64) {
        return false;
    }
    return crypto_ed25519_check(signature->data(), key->data(), data.data(), data.size()) == 0;
}

KeyPair KeyPairFromSeed(std::span<const std::uint8_t, 32> seed) {
    std::uint8_t seed_copy[32];
    std::memcpy(seed_copy, seed.data(), 32);
    std::uint8_t public_key[32];
    KeyPair pair;
    pair.secret.resize(64);
    crypto_ed25519_key_pair(pair.secret.data(), public_key, seed_copy);
    pair.public_b64 = EncodeBase64(public_key);
    return pair;
}

std::string SignEd25519(std::span<const std::uint8_t> data, const KeyPair& key) {
    std::uint8_t signature[64];
    crypto_ed25519_sign(signature, key.secret.data(), data.data(), data.size());
    return EncodeBase64(signature);
}

} // namespace den::plugins
