// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// xdelta3's DJW sections: a static Huffman coder in the manner of bzip2. A section is cut
// into sectors; each sector is coded with one of up to eight prefix codes ("groups"). The
// code lengths of the groups, and the group of every sector, are themselves written as
// move-to-front positions with run lengths, under a prefix code of their own.
//
//   3 bits   groups - 1
//   5 bits   sector size / 5 - 1                 (only with more than one group)
//   4 bits   n - 7, then n times 4 bits          the lengths of the code-length code
//   ...      256 lengths per group               (a symbol absent from the group before
//                                                 it is absent again and not written)
//   3 bits   per group and one more              the lengths of the selector code
//   ...      the group of every sector           (only with more than one group)
//   ...      the bytes, sector by sector
//
// Each section stands alone: nothing is kept between windows.

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "secondary.h"

namespace den::patching {

namespace {

constexpr unsigned SYMBOLS = 256;
constexpr unsigned MAX_GROUPS = 8;
constexpr unsigned MAX_LENGTH = 20;          // of a byte's code
constexpr unsigned LENGTH_CODES = MAX_LENGTH + 2; // two run symbols, then positions 1 to 20
constexpr unsigned MAX_LENGTH_LENGTH = 15;   // of a code-length symbol's code
constexpr unsigned MAX_SELECTOR_LENGTH = 7;
constexpr unsigned SECTOR_STEP = 5;

// A canonical prefix code, read a bit at a time.
struct Prefix {
    std::array<std::uint32_t, MAX_LENGTH + 2> first{}, limit{};
    std::array<std::uint16_t, SYMBOLS> order{};
    unsigned shortest = 0, longest = 0, placed = 0;
    bool usable = false;

    /// `lengths[i]` is the code length of symbol i, 0 when it has no code.
    void Build(const std::uint8_t* lengths, unsigned count, unsigned max_length) {
        std::array<std::uint32_t, MAX_LENGTH + 2> have{}, index{};
        usable = false;
        for (unsigned i = 0; i < count; ++i) {
            if (lengths[i] > max_length) {
                return;
            }
            ++have[lengths[i]];
        }
        shortest = 1;
        while (shortest <= max_length && have[shortest] == 0) {
            ++shortest;
        }
        if (shortest > max_length) {
            return; // no symbol has a code
        }
        longest = max_length;
        while (have[longest] == 0) {
            --longest;
        }
        index[shortest] = 0;
        first[shortest] = 0;
        limit[shortest] = have[shortest] - 1;
        for (unsigned i = shortest + 1; i <= longest; ++i) {
            const std::uint32_t next = (limit[i - 1] + 1) << 1;
            index[i] = index[i - 1] + have[i - 1];
            limit[i] = next + have[i] - 1;
            first[i] = next - index[i];
        }
        placed = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (lengths[i] != 0) {
                order[index[lengths[i]]++] = static_cast<std::uint16_t>(i);
                ++placed;
            }
        }
        usable = true;
    }

    /// The next symbol, or -1 when the bits are no code.
    int Read(BitReader& bits) const {
        if (!usable) {
            return -1;
        }
        std::uint32_t code = 0;
        for (unsigned length = 1; length <= longest; ++length) {
            code = (code << 1) | bits.Bit();
            if (length >= shortest && code <= limit[length]) {
                const std::uint32_t at = code - first[length];
                return first[length] <= code && at < placed ? order[at] : -1;
            }
        }
        return -1;
    }
};

// `count` values written as positions in a move-to-front list: symbols 0 and 1 are the
// digits of a run of the list's head (bijective base 2, low digit first), symbol n + 1 is
// position n. With `period`, a value whose counterpart one period earlier is 0 is 0 and
// takes no bits.
bool ReadMoveToFront(BitReader& bits, const Prefix& prefix, std::uint8_t* list, unsigned list_size,
                     std::size_t count, std::size_t period, std::uint8_t* values) {
    std::size_t n = 0, repeat = 0;
    unsigned position = 0, digit = 0;
    while (n < count) {
        if (period != 0 && n >= period && values[n - period] == 0) {
            values[n++] = 0;
        } else if (repeat != 0) {
            values[n++] = list[0];
            --repeat;
        } else if (position != 0) {
            const std::uint8_t value = list[position];
            for (unsigned k = position; k != 0; --k) {
                list[k] = list[k - 1];
            }
            list[0] = value;
            values[n++] = value;
            position = 0;
        } else {
            const int symbol = prefix.Read(bits);
            if (symbol < 0 || bits.in.bad) {
                return false;
            }
            if (symbol <= 1) {
                if (digit > 30) {
                    return false;
                }
                repeat = static_cast<std::size_t>(symbol + 1) << digit++;
            } else {
                position = static_cast<unsigned>(symbol - 1);
                digit = 0;
                if (position >= list_size) {
                    return false;
                }
            }
        }
    }
    return repeat == 0;
}

class DjwDecoder final : public Secondary {
public:
    bool Decode(Cursor& in, std::uint8_t* out, std::size_t size) override {
        if (size == 0) {
            return false;
        }
        BitReader bits{in};
        const unsigned groups = bits.Number(3) + 1;
        const std::size_t sector = groups > 1 ? (bits.Number(5) + 1) * SECTOR_STEP : size;
        const std::size_t sectors = 1 + (size - 1) / sector;

        // The code of the code lengths.
        std::array<std::uint8_t, LENGTH_CODES> length_lengths{};
        const unsigned written = bits.Number(4) + 7;
        for (unsigned i = 0; i < written; ++i) {
            length_lengths[i] = static_cast<std::uint8_t>(bits.Number(4));
        }
        Prefix length_code;
        length_code.Build(length_lengths.data(), LENGTH_CODES, MAX_LENGTH_LENGTH);
        // The list starts with the lengths in the order they are likely.
        std::array<std::uint8_t, MAX_LENGTH + 1> length_list = {0,  4,  5, 6,  7,  8,  9,  10, 3,  11, 2,
                                                                12, 13, 1, 14, 15, 16, 17, 18, 19, 20};
        std::vector<std::uint8_t> lengths(static_cast<std::size_t>(groups) * SYMBOLS);
        if (in.bad || !ReadMoveToFront(bits, length_code, length_list.data(), MAX_LENGTH + 1, lengths.size(),
                                       SYMBOLS, lengths.data())) {
            return false;
        }
        std::vector<Prefix> codes(groups);
        for (unsigned group = 0; group < groups; ++group) {
            codes[group].Build(lengths.data() + group * SYMBOLS, SYMBOLS, MAX_LENGTH);
        }

        std::vector<std::uint8_t> selected;
        if (groups > 1) {
            std::array<std::uint8_t, MAX_GROUPS + 1> selector_lengths{}, selector_list{};
            for (unsigned i = 0; i <= groups; ++i) {
                selector_lengths[i] = static_cast<std::uint8_t>(bits.Number(3));
                selector_list[i] = static_cast<std::uint8_t>(i);
            }
            Prefix selector;
            selector.Build(selector_lengths.data(), groups + 1, MAX_SELECTOR_LENGTH);
            selected.resize(sectors);
            if (in.bad ||
                !ReadMoveToFront(bits, selector, selector_list.data(), groups, sectors, 0, selected.data())) {
                return false;
            }
        }

        std::size_t at = 0;
        for (std::size_t s = 0; s < sectors; ++s) {
            const unsigned group = groups > 1 ? selected[s] : 0;
            if (group >= groups) {
                return false;
            }
            const Prefix& code = codes[group];
            for (const std::size_t end = std::min(size, at + sector); at < end; ++at) {
                const int symbol = code.Read(bits);
                if (symbol < 0 || in.bad) {
                    return false;
                }
                out[at] = static_cast<std::uint8_t>(symbol);
            }
        }
        return !in.bad;
    }
};

} // namespace

std::unique_ptr<Secondary> MakeDjwDecoder() {
    return std::make_unique<DjwDecoder>();
}

} // namespace den::patching
