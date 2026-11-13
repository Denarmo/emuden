// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// xdelta3's LZMA sections: one .xz stream per kind of section for the whole patch. Every
// window's section is the next piece of it, cut where the encoder flushed, so the decoder
// lives as long as the patch is applied. Decoded by XZ Embedded.

#include <mutex>

#include <xz.h>

#include "secondary.h"

namespace den::patching {

namespace {

// The largest dictionary xz's presets use ("xdelta3 -9").
constexpr std::uint32_t MAX_DICTIONARY = 64u << 20;

class LzmaDecoder final : public Secondary {
public:
    ~LzmaDecoder() override {
        xz_dec_end(state_);
    }

    bool Decode(Cursor& in, std::uint8_t* out, std::size_t size) override {
        if (state_ == nullptr) {
            static std::once_flag tables;
            std::call_once(tables, [] { xz_crc32_init(); });
            state_ = xz_dec_init(XZ_DYNALLOC, MAX_DICTIONARY);
            if (state_ == nullptr) {
                return false;
            }
        }
        xz_buf buffer{};
        buffer.in = in.data;
        buffer.in_pos = in.at;
        buffer.in_size = in.size;
        buffer.out = out;
        buffer.out_size = size;
        // XZ_OK with room left and input left means "call again"; without progress the
        // decoder itself answers XZ_BUF_ERROR, so this ends.
        while (buffer.out_pos < buffer.out_size) {
            if (xz_dec_run(state_, &buffer) != XZ_OK || buffer.in_pos == buffer.in_size) {
                break;
            }
        }
        in.at = buffer.in_pos;
        return buffer.out_pos == buffer.out_size;
    }

private:
    xz_dec* state_ = nullptr;
};

} // namespace

std::unique_ptr<Secondary> MakeLzmaDecoder() {
    return std::make_unique<LzmaDecoder>();
}

std::unique_ptr<Secondary> MakeSecondary(std::uint8_t id) {
    switch (id) {
    case SECONDARY_DJW:
        return MakeDjwDecoder();
    case SECONDARY_LZMA:
        return MakeLzmaDecoder();
    case SECONDARY_FGK:
        return MakeFgkDecoder();
    default:
        return nullptr;
    }
}

} // namespace den::patching
