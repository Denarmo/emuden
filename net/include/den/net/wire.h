// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Little-endian fields of the messages EmuDEN devices send each other.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace den::net {

class Writer {
public:
    Writer& U8(std::uint8_t v) {
        bytes_.push_back(v);
        return *this;
    }
    Writer& U16(std::uint16_t v) {
        return Raw(v, 2);
    }
    Writer& U32(std::uint32_t v) {
        return Raw(v, 4);
    }
    Writer& U64(std::uint64_t v) {
        return Raw(v, 8);
    }
    Writer& I16(std::int16_t v) {
        return U16(static_cast<std::uint16_t>(v));
    }
    /// A length (u32) and the bytes.
    Writer& Bytes(std::span<const std::uint8_t> data) {
        U32(static_cast<std::uint32_t>(data.size()));
        bytes_.insert(bytes_.end(), data.begin(), data.end());
        return *this;
    }
    Writer& Str(const std::string& text) {
        return Bytes({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }
    std::vector<std::uint8_t>& Data() {
        return bytes_;
    }

private:
    Writer& Raw(std::uint64_t v, int count) {
        for (int i = 0; i < count; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
        return *this;
    }
    std::vector<std::uint8_t> bytes_;
};

/// Reads what a Writer wrote. A read past the end gives zeros and sets Failed().
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}
    std::uint8_t U8() {
        return static_cast<std::uint8_t>(Raw(1));
    }
    std::uint16_t U16() {
        return static_cast<std::uint16_t>(Raw(2));
    }
    std::uint32_t U32() {
        return static_cast<std::uint32_t>(Raw(4));
    }
    std::uint64_t U64() {
        return Raw(8);
    }
    std::int16_t I16() {
        return static_cast<std::int16_t>(U16());
    }
    std::vector<std::uint8_t> Bytes() {
        const std::uint32_t size = U32();
        if (failed_ || size > data_.size() - at_) {
            failed_ = true;
            return {};
        }
        std::vector<std::uint8_t> out(data_.begin() + static_cast<std::ptrdiff_t>(at_),
                                      data_.begin() + static_cast<std::ptrdiff_t>(at_ + size));
        at_ += size;
        return out;
    }
    std::string Str() {
        const std::vector<std::uint8_t> bytes = Bytes();
        return std::string(bytes.begin(), bytes.end());
    }
    bool Failed() const {
        return failed_;
    }

private:
    std::uint64_t Raw(int count) {
        if (failed_ || data_.size() - at_ < static_cast<std::size_t>(count)) {
            failed_ = true;
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < count; ++i) {
            v |= static_cast<std::uint64_t>(data_[at_ + static_cast<std::size_t>(i)]) << (8 * i);
        }
        at_ += static_cast<std::size_t>(count);
        return v;
    }
    std::span<const std::uint8_t> data_;
    std::size_t at_ = 0;
    bool failed_ = false;
};

} // namespace den::net
