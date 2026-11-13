// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Writes VCDIFF (RFC 3284) windows the way xdelta3 does, for the tests.

#pragma once

#include <array>

#include "encoders.h"

namespace den::patching::testing {

inline void PutNumber(Bytes& out, std::uint64_t value) {
    std::uint8_t digits[10];
    int count = 0;
    do {
        digits[count++] = value & 0x7F;
        value >>= 7;
    } while (value != 0);
    while (count-- > 0) {
        out.push_back(digits[count] | (count > 0 ? 0x80 : 0));
    }
}

inline std::uint32_t Adler32(const Bytes& data) {
    std::uint32_t a = 1, b = 0;
    for (const std::uint8_t byte : data) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

enum class CopyMode { Self, Here, Recent, Exact, Any };

class VcdiffWriter {
public:
    Bytes out;

    explicit VcdiffWriter(std::uint8_t compressor = 0, std::string_view app_data = {}) {
        out = {0xD6, 0xC3, 0xC4, 0x00};
        out.push_back(static_cast<std::uint8_t>((compressor != 0 ? 1 : 0) | (app_data.empty() ? 0 : 4)));
        if (compressor != 0) {
            out.push_back(compressor);
        }
        if (!app_data.empty()) {
            PutNumber(out, app_data.size());
            Put(out, app_data);
        }
    }

    /// `flags`: 1 = copies come from the source, 2 = from the result so far, 4 = Adler-32.
    void Begin(std::uint8_t flags, std::uint64_t source_length = 0, std::uint64_t source_at = 0) {
        flags_ = flags;
        source_length_ = source_length;
        source_at_ = source_at;
        data_.clear();
        instructions_.clear();
        addresses_.clear();
        window_.clear();
        recent_ = {};
        exact_ = {};
        next_ = 0;
        turn_ = 0;
    }

    void Add(const std::uint8_t* bytes, std::size_t size) {
        if (size <= 17) {
            instructions_.push_back(static_cast<std::uint8_t>(1 + size));
        } else {
            instructions_.push_back(1);
            PutNumber(instructions_, size);
        }
        Put(data_, bytes, size);
        Put(window_, bytes, size);
    }

    void Run(std::uint8_t value, std::size_t size) {
        instructions_.push_back(0);
        PutNumber(instructions_, size);
        data_.push_back(value);
        window_.insert(window_.end(), size, value);
    }

    /// `address` counts from the start of the source window; the target window follows it.
    /// `bytes` are what the copy gives.
    void Copy(std::uint64_t address, const std::uint8_t* bytes, std::size_t size,
              CopyMode mode = CopyMode::Any) {
        const int coded = Address(address, mode);
        if (size >= 4 && size <= 18) {
            instructions_.push_back(static_cast<std::uint8_t>(19 + 16 * coded + (size - 3)));
        } else {
            instructions_.push_back(static_cast<std::uint8_t>(19 + 16 * coded));
            PutNumber(instructions_, size);
        }
        Put(window_, bytes, size);
    }

    /// One instruction byte for both: 1 to 4 added bytes, then a copy of 4 to 6.
    void AddThenCopy(const std::uint8_t* added, std::size_t add_size, std::uint64_t address,
                     const std::uint8_t* copied, std::size_t copy_size) {
        Put(data_, added, add_size);
        Put(window_, added, add_size);
        const int coded = Address(address, CopyMode::Self);
        instructions_.push_back(static_cast<std::uint8_t>(163 + coded * 12 + (add_size - 1) * 3 + (copy_size - 4)));
        Put(window_, copied, copy_size);
    }

    /// One instruction byte for both: a copy of 4, then one added byte.
    void CopyThenAdd(std::uint64_t address, const std::uint8_t* copied, std::uint8_t added) {
        const int coded = Address(address, CopyMode::Here);
        instructions_.push_back(static_cast<std::uint8_t>(247 + coded));
        Put(window_, copied, 4);
        data_.push_back(added);
        window_.push_back(added);
    }

    /// `packed`: the bits that claim a section went through the secondary compressor.
    void End(std::uint8_t packed = 0, bool break_check = false) {
        Bytes body;
        PutNumber(body, window_.size());
        body.push_back(packed);
        PutNumber(body, data_.size());
        PutNumber(body, instructions_.size());
        PutNumber(body, addresses_.size());
        if (flags_ & 4) {
            PutBe(body, Adler32(window_) + (break_check ? 1 : 0), 4);
        }
        Put(body, data_.data(), data_.size());
        Put(body, instructions_.data(), instructions_.size());
        Put(body, addresses_.data(), addresses_.size());
        out.push_back(flags_);
        if (flags_ & 3) {
            PutNumber(out, source_length_);
            PutNumber(out, source_at_);
        }
        PutNumber(out, body.size());
        Put(out, body.data(), body.size());
    }

    std::size_t Written() const {
        return window_.size();
    }

private:
    // Writes the address the way `mode` asks when that is possible, and returns the mode
    // number of the instruction.
    int Address(std::uint64_t address, CopyMode mode) {
        const std::uint64_t here = source_length_ + window_.size();
        if (mode == CopyMode::Any) {
            mode = static_cast<CopyMode>(turn_++ % 4);
        }
        int coded = 0;
        const std::size_t slot = static_cast<std::size_t>(address % exact_.size());
        if (mode == CopyMode::Exact && exact_[slot] == address) {
            coded = 6 + static_cast<int>(slot / 256);
            addresses_.push_back(static_cast<std::uint8_t>(slot % 256));
        } else if (mode == CopyMode::Recent && address >= recent_[turn_ % 4]) {
            coded = 2 + static_cast<int>(turn_ % 4);
            PutNumber(addresses_, address - recent_[turn_ % 4]);
        } else if (mode == CopyMode::Here) {
            coded = 1;
            PutNumber(addresses_, here - address);
        } else {
            PutNumber(addresses_, address);
        }
        recent_[next_] = address;
        next_ = (next_ + 1) % 4;
        exact_[slot] = address;
        return coded;
    }

    std::uint8_t flags_ = 0;
    std::uint64_t source_length_ = 0, source_at_ = 0;
    Bytes data_, instructions_, addresses_, window_;
    std::array<std::uint64_t, 4> recent_{};
    std::array<std::uint64_t, 768> exact_{};
    int next_ = 0;
    unsigned turn_ = 0;
};

// A whole patch: windows of `window` target bytes against the whole source, with copies of
// unchanged bytes, runs, and added bytes for the rest.
inline Bytes MakeVcdiff(const Bytes& source, const Bytes& target, std::size_t window = 32768) {
    VcdiffWriter w(0, "made by the tests");
    for (std::size_t start = 0; start < target.size(); start += window) {
        const std::size_t end = std::min(target.size(), start + window);
        w.Begin(1 | 4, source.size(), 0);
        std::size_t i = start;
        while (i < end) {
            std::size_t same = 0;
            while (i + same < end && i + same < source.size() && source[i + same] == target[i + same]) {
                ++same;
            }
            if (same >= 4) {
                w.Copy(i, target.data() + i, same);
                i += same;
                continue;
            }
            std::size_t run = 1;
            while (i + run < end && target[i + run] == target[i]) {
                ++run;
            }
            if (run >= 8) {
                w.Run(target[i], run);
                i += run;
                continue;
            }
            std::size_t stop = i + 1;
            while (stop < end && (stop >= source.size() || source[stop] != target[stop])) {
                ++stop;
            }
            w.Add(target.data() + i, stop - i);
            i = stop;
        }
        w.End();
    }
    return w.out;
}

} // namespace den::patching::testing
