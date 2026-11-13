// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Patch writers for the tests: simple, correct encoders of every format the app applies, so
// the tests need no patch or ROM file in the repository.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/files.h"

namespace den::patching::testing {

using Bytes = std::vector<std::uint8_t>;

inline void Put(Bytes& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}
inline void Put(Bytes& out, const std::uint8_t* data, std::size_t size) {
    out.insert(out.end(), data, data + size);
}
inline void PutLe(Bytes& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}
inline void PutBe(Bytes& out, std::uint64_t value, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}
inline std::uint8_t At(const Bytes& data, std::size_t i) {
    return i < data.size() ? data[i] : 0;
}

inline void WriteFile(const std::filesystem::path& file, const Bytes& data) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
inline Bytes ReadFile(const std::filesystem::path& file) {
    return ReadBinaryFile(file).value_or(Bytes{});
}

/// Bytes that look like a ROM: a seeded stream with runs of one value in it.
inline Bytes MakeRom(std::size_t size, std::uint32_t seed) {
    Bytes rom(size);
    std::uint32_t state = seed * 2654435761u + 1;
    for (std::size_t i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        rom[i] = static_cast<std::uint8_t>(state >> 24);
        if ((state >> 8) % 97 == 0) {
            const std::size_t run = std::min<std::size_t>(size - i, 40 + (state >> 16) % 300);
            std::memset(rom.data() + i, 0xFF, run);
            i += run - 1;
        }
    }
    return rom;
}

/// A changed copy: edits in place, a moved block, a repeated run and `grow` more bytes.
inline Bytes MakeHack(const Bytes& rom, std::uint32_t seed, std::ptrdiff_t grow = 0) {
    Bytes hack = rom;
    hack.resize(static_cast<std::size_t>(static_cast<std::ptrdiff_t>(rom.size()) + grow));
    std::uint32_t state = seed * 40503u + 7;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    const std::size_t size = std::min(rom.size(), hack.size());
    for (int edit = 0; edit < 40; ++edit) {
        const std::size_t at = next() % size;
        const std::size_t length = std::min<std::size_t>(size - at, 1 + next() % 600);
        for (std::size_t i = 0; i < length; ++i) {
            hack[at + i] = static_cast<std::uint8_t>(next());
        }
    }
    if (size > 9000) {
        std::memcpy(hack.data() + 5000, rom.data() + 1000, 3000); // a block from elsewhere
        std::memset(hack.data() + 300, 0x5A, 2000);               // a long run
    }
    for (std::size_t i = rom.size(); i < hack.size(); ++i) {
        hack[i] = static_cast<std::uint8_t>(i % 7 == 0 ? next() : 0xEE);
    }
    return hack;
}

// IPS: records of changed runs, long runs of one byte as RLE, the cut size after "EOF" when
// the target is shorter.
inline Bytes MakeIps(const Bytes& source, const Bytes& target) {
    Bytes out;
    Put(out, "PATCH");
    std::size_t i = 0;
    while (i < target.size()) {
        if (i < source.size() && source[i] == target[i]) {
            ++i;
            continue;
        }
        std::size_t run = 1;
        while (i + run < target.size() && target[i + run] == target[i] && run < 0xFFFF) {
            ++run;
        }
        if (run >= 16) {
            PutBe(out, i, 3);
            PutBe(out, 0, 2);
            PutBe(out, run, 2);
            out.push_back(target[i]);
            i += run;
            continue;
        }
        std::size_t end = i + 1;
        while (end < target.size() && end - i < 0xFFFF && (end >= source.size() || source[end] != target[end])) {
            ++end;
        }
        PutBe(out, i, 3);
        PutBe(out, end - i, 2);
        Put(out, target.data() + i, end - i);
        i = end;
    }
    Put(out, "EOF");
    if (target.size() < source.size()) {
        PutBe(out, target.size(), 3);
    }
    return out;
}

inline void PutBeatNumber(Bytes& out, std::uint64_t value) {
    for (;;) {
        const std::uint8_t x = value & 0x7F;
        value >>= 7;
        if (value == 0) {
            out.push_back(0x80 | x);
            return;
        }
        out.push_back(x);
        --value;
    }
}

inline void PutBeatFooter(Bytes& out, const Bytes& source, const Bytes& target) {
    PutLe(out, Crc32(source.data(), source.size()), 4);
    PutLe(out, Crc32(target.data(), target.size()), 4);
    PutLe(out, Crc32(out.data(), out.size()), 4);
}

inline Bytes MakeUps(const Bytes& source, const Bytes& target) {
    Bytes out;
    Put(out, "UPS1");
    PutBeatNumber(out, source.size());
    PutBeatNumber(out, target.size());
    const std::size_t size = std::max(source.size(), target.size());
    std::size_t previous = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (At(source, i) == At(target, i)) {
            continue;
        }
        PutBeatNumber(out, i - previous);
        while (i < size && At(source, i) != At(target, i)) {
            out.push_back(At(source, i) ^ At(target, i));
            ++i;
        }
        out.push_back(0);
        previous = i + 1;
    }
    PutBeatFooter(out, source, target);
    return out;
}

/// Writes BPS actions one by one, so a test can use each of the four.
struct BpsWriter {
    Bytes out;
    std::uint64_t source_at = 0, target_at = 0;

    BpsWriter(const Bytes& source, const Bytes& target, std::string_view metadata = {}) {
        Put(out, "BPS1");
        PutBeatNumber(out, source.size());
        PutBeatNumber(out, target.size());
        PutBeatNumber(out, metadata.size());
        Put(out, metadata);
    }
    void SourceRead(std::uint64_t length) {
        PutBeatNumber(out, (length - 1) << 2);
    }
    void TargetRead(const std::uint8_t* data, std::uint64_t length) {
        PutBeatNumber(out, ((length - 1) << 2) | 1);
        Put(out, data, static_cast<std::size_t>(length));
    }
    void SourceCopy(std::uint64_t offset, std::uint64_t length) {
        PutBeatNumber(out, ((length - 1) << 2) | 2);
        Step(source_at, offset);
        source_at = offset + length;
    }
    void TargetCopy(std::uint64_t offset, std::uint64_t length) {
        PutBeatNumber(out, ((length - 1) << 2) | 3);
        Step(target_at, offset);
        target_at = offset + length;
    }
    Bytes Finish(const Bytes& source, const Bytes& target) {
        PutBeatFooter(out, source, target);
        return out;
    }

private:
    void Step(std::uint64_t from, std::uint64_t to) {
        PutBeatNumber(out, to >= from ? (to - from) << 1 : ((from - to) << 1) | 1);
    }
};

// BPS from a linear comparison: unchanged bytes are read from the source, runs of one byte are
// copied from the result, the rest is carried in the patch.
inline Bytes MakeBps(const Bytes& source, const Bytes& target, std::string_view metadata = {}) {
    BpsWriter w(source, target, metadata);
    std::size_t i = 0;
    while (i < target.size()) {
        std::size_t same = 0;
        while (i + same < target.size() && i + same < source.size() && source[i + same] == target[i + same]) {
            ++same;
        }
        if (same >= 4) {
            w.SourceRead(same);
            i += same;
            continue;
        }
        std::size_t run = 1;
        while (i + run < target.size() && target[i + run] == target[i]) {
            ++run;
        }
        if (run >= 8) {
            w.TargetRead(target.data() + i, 1);
            w.TargetCopy(i, run - 1);
            i += run;
            continue;
        }
        std::size_t end = i + 1;
        while (end < target.size() && (end >= source.size() || source[end] != target[end]) &&
               !(end + 8 <= target.size() && std::count(target.begin() + end, target.begin() + end + 8, target[end]) == 8)) {
            ++end;
        }
        w.TargetRead(target.data() + i, end - i);
        i = end;
    }
    return w.Finish(source, target);
}

inline std::uint16_t Crc16(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size; ++i) {
        crc = static_cast<std::uint16_t>(crc ^ (data[i] << 8));
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<std::uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

inline Bytes MakeAps(const Bytes& source, const Bytes& target) {
    constexpr std::size_t BLOCK = 0x10000;
    Bytes out;
    Put(out, "APS1");
    PutLe(out, source.size(), 4);
    PutLe(out, target.size(), 4);
    for (std::size_t at = 0; at < std::max(source.size(), target.size()); at += BLOCK) {
        Bytes a(BLOCK), b(BLOCK), change(BLOCK);
        bool differs = false;
        for (std::size_t i = 0; i < BLOCK; ++i) {
            a[i] = At(source, at + i);
            b[i] = At(target, at + i);
            change[i] = a[i] ^ b[i];
            differs = differs || change[i] != 0;
        }
        if (differs) {
            PutLe(out, at, 4);
            PutLe(out, Crc16(a.data(), BLOCK), 2);
            PutLe(out, Crc16(b.data(), BLOCK), 2);
            Put(out, change.data(), BLOCK);
        }
    }
    return out;
}

// PPF 1, 2 or 3 for a target as long as the source. Version 3 carries undo data and a
// description block; 2 and 3 the check block of the source at 0x9320.
inline Bytes MakePpf(const Bytes& source, const Bytes& target, int version) {
    Bytes out;
    Put(out, version == 1 ? "PPF10" : version == 2 ? "PPF20" : "PPF30");
    out.push_back(static_cast<std::uint8_t>(version - 1));
    std::string description = "A test patch";
    description.resize(50, ' ');
    Put(out, description);
    if (version == 2) {
        PutLe(out, source.size(), 4);
        Put(out, source.data() + 0x9320, 1024);
    } else if (version == 3) {
        out.push_back(0); // a BIN image
        out.push_back(1); // with the check block
        out.push_back(1); // with undo data
        out.push_back(0);
        Put(out, source.data() + 0x9320, 1024);
    }
    for (std::size_t i = 0; i < target.size(); ++i) {
        if (source[i] == target[i]) {
            continue;
        }
        std::size_t end = i + 1;
        while (end < target.size() && end - i < 255 && source[end] != target[end]) {
            ++end;
        }
        PutLe(out, i, version == 3 ? 8 : 4);
        out.push_back(static_cast<std::uint8_t>(end - i));
        Put(out, target.data() + i, end - i);
        if (version == 3) {
            Put(out, source.data() + i, end - i);
        }
        i = end - 1;
    }
    if (version >= 2) {
        const std::string text = "made by the tests";
        Put(out, "@BEGIN_FILE_ID.DIZ");
        Put(out, text);
        Put(out, "@END_FILE_ID.DIZ");
        PutLe(out, text.size(), version == 3 ? 2 : 4);
    }
    return out;
}

} // namespace den::patching::testing
