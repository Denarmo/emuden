// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/base/utf8.h"

namespace den {

std::uint32_t NextCodePoint(std::string_view text, std::size_t& at) {
    if (at >= text.size()) {
        return 0;
    }
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t extra = 0;
    std::uint32_t code = lead;
    if (lead < 0x80) {
        ++at;
        return code;
    } else if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        code = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        code = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        code = lead & 0x07;
    } else {
        ++at;
        return 0xFFFD;
    }
    if (at + extra >= text.size()) {
        ++at;
        return 0xFFFD;
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const auto next = static_cast<unsigned char>(text[at + i]);
        if ((next & 0xC0) != 0x80) {
            ++at;
            return 0xFFFD;
        }
        code = (code << 6) | (next & 0x3F);
    }
    at += extra + 1;
    return code;
}

std::size_t CodePointCount(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t at = 0; at < text.size(); ++count) {
        NextCodePoint(text, at);
    }
    return count;
}

} // namespace den
