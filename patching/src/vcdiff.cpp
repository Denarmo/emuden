// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// VCDIFF (RFC 3284) as xdelta3 writes it: the standard instruction table, source and target
// windows, the Adler-32 of each target window, and sections packed by one of its secondary
// compressors (secondary.h).

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "den/base/files.h"
#include "io.h"
#include "secondary.h"

namespace den::patching {

namespace {

constexpr std::uint8_t HAS_COMPRESSOR = 1, HAS_TABLE = 2, HAS_APP_DATA = 4;
constexpr std::uint8_t FROM_SOURCE = 1, FROM_TARGET = 2, HAS_ADLER = 4;
constexpr std::uint64_t MAX_WINDOW = 256ull << 20;
constexpr int RECENT_SLOTS = 4;
constexpr int EXACT_SLOTS = 3;

enum : std::uint8_t { NOOP, ADD, RUN, COPY };

struct Instruction {
    std::uint8_t type = NOOP, size = 0, mode = 0;
};
struct Code {
    Instruction first, second;
};

// The table of RFC 3284, section 5.6.
std::array<Code, 256> StandardTable() {
    std::array<Code, 256> table{};
    int at = 0;
    table[at++] = {{RUN, 0, 0}, {}};
    for (int size = 0; size <= 17; ++size) {
        table[at++] = {{ADD, static_cast<std::uint8_t>(size), 0}, {}};
    }
    for (int mode = 0; mode < 9; ++mode) {
        table[at++] = {{COPY, 0, static_cast<std::uint8_t>(mode)}, {}};
        for (int size = 4; size <= 18; ++size) {
            table[at++] = {{COPY, static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(mode)}, {}};
        }
    }
    for (int mode = 0; mode < 6; ++mode) {
        for (int add = 1; add <= 4; ++add) {
            for (int copy = 4; copy <= 6; ++copy) {
                table[at++] = {{ADD, static_cast<std::uint8_t>(add), 0},
                               {COPY, static_cast<std::uint8_t>(copy), static_cast<std::uint8_t>(mode)}};
            }
        }
    }
    for (int mode = 6; mode < 9; ++mode) {
        for (int add = 1; add <= 4; ++add) {
            table[at++] = {{ADD, static_cast<std::uint8_t>(add), 0}, {COPY, 4, static_cast<std::uint8_t>(mode)}};
        }
    }
    for (int mode = 0; mode < 9; ++mode) {
        table[at++] = {{COPY, 4, static_cast<std::uint8_t>(mode)}, {ADD, 1, 0}};
    }
    return table;
}

std::uint64_t Number(Cursor& p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 10; ++i) {
        const std::uint8_t byte = p.U8();
        value = (value << 7) | (byte & 0x7F);
        if (!(byte & 0x80)) {
            return value;
        }
    }
    p.bad = true;
    return 0;
}

std::uint32_t Adler32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t a = 1, b = 0;
    while (size > 0) {
        const std::size_t take = std::min<std::size_t>(size, 5552);
        for (std::size_t i = 0; i < take; ++i) {
            a += data[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        data += take;
        size -= take;
    }
    return (b << 16) | a;
}

struct Header {
    std::uint8_t compressor = 0;
    bool custom_table = false;
};

bool ReadHeader(Cursor& p, Header& header) {
    p.at = 4; // the magic and its version byte
    const std::uint8_t flags = p.U8();
    if (flags & HAS_COMPRESSOR) {
        header.compressor = p.U8();
    }
    if (flags & HAS_TABLE) {
        header.custom_table = true;
        p.Take(static_cast<std::size_t>(std::min<std::uint64_t>(Number(p), p.Left() + 1)));
    }
    if (flags & HAS_APP_DATA) {
        p.Take(static_cast<std::size_t>(std::min<std::uint64_t>(Number(p), p.Left() + 1)));
    }
    return !p.bad;
}

struct Window {
    std::uint8_t flags = 0, packed = 0;
    std::uint64_t source_length = 0, source_at = 0, target_length = 0;
    std::uint32_t adler = 0;
    Cursor data, instructions, addresses;
};

bool ReadWindow(Cursor& p, Window& w) {
    w.flags = p.U8();
    if (w.flags & (FROM_SOURCE | FROM_TARGET)) {
        w.source_length = Number(p);
        w.source_at = Number(p);
    }
    const std::uint64_t length = Number(p);
    if (p.bad || length > p.Left()) {
        return false;
    }
    const std::size_t end = p.at + static_cast<std::size_t>(length);
    w.target_length = Number(p);
    w.packed = p.U8();
    const std::uint64_t data = Number(p);
    const std::uint64_t instructions = Number(p);
    const std::uint64_t addresses = Number(p);
    if (w.flags & HAS_ADLER) {
        w.adler = static_cast<std::uint32_t>(p.Be(4));
    }
    if (p.bad || p.at > end || data > end - p.at || instructions > end - p.at - data ||
        addresses > end - p.at - data - instructions) {
        return false;
    }
    w.data = {p.Take(static_cast<std::size_t>(data)), static_cast<std::size_t>(data)};
    w.instructions = {p.Take(static_cast<std::size_t>(instructions)), static_cast<std::size_t>(instructions)};
    w.addresses = {p.Take(static_cast<std::size_t>(addresses)), static_cast<std::size_t>(addresses)};
    p.at = end;
    return (w.flags & (FROM_SOURCE | FROM_TARGET)) != (FROM_SOURCE | FROM_TARGET);
}

// The sections of a patch that went through its secondary compressor: a decoder per kind of
// section, made when the first such section comes, and the unpacked bytes of this window.
struct Unpacker {
    std::uint8_t compressor = 0;
    std::array<std::unique_ptr<Secondary>, 3> decoders;
    std::array<std::vector<std::uint8_t>, 3> bytes;

    /// Replaces `section` by what it unpacks to: its size first, then the packed bytes.
    Status Unpack(int kind, Cursor& section) {
        if (decoders[kind] == nullptr && (decoders[kind] = MakeSecondary(compressor)) == nullptr) {
            return Status::Fail("This xdelta patch is packed with a method this app does not know (" +
                                std::to_string(compressor) + ")");
        }
        const std::uint64_t size = Number(section);
        if (section.bad || size == 0 || size > MAX_WINDOW) {
            return Corrupt("xdelta");
        }
        bytes[kind].resize(static_cast<std::size_t>(size));
        if (!decoders[kind]->Decode(section, bytes[kind].data(), bytes[kind].size()) || section.bad ||
            section.Left() != 0) {
            return Corrupt("xdelta");
        }
        section = {bytes[kind].data(), bytes[kind].size()};
        return {};
    }
};

// The addresses of the last copies, which later copies are written against.
struct AddressCache {
    std::array<std::uint64_t, RECENT_SLOTS> recent{};
    std::array<std::uint64_t, EXACT_SLOTS * 256> exact{};
    int next = 0;

    std::uint64_t Decode(Cursor& addresses, std::uint64_t here, int mode) {
        std::uint64_t address = 0;
        if (mode == 0) {
            address = Number(addresses);
        } else if (mode == 1) {
            address = here - Number(addresses);
        } else if (mode < 2 + RECENT_SLOTS) {
            address = recent[mode - 2] + Number(addresses);
        } else {
            address = exact[(mode - 2 - RECENT_SLOTS) * 256 + addresses.U8()];
        }
        recent[next] = address;
        next = (next + 1) % RECENT_SLOTS;
        exact[address % exact.size()] = address;
        return address;
    }
};

} // namespace

Status InspectVcdiff(Cursor p, PatchInfo& info) {
    Header header;
    if (p.size < 5 || !ReadHeader(p, header)) {
        return Corrupt("xdelta");
    }
    std::uint64_t target = 0;
    while (p.Left() > 0) {
        Window window;
        if (!ReadWindow(p, window)) {
            return Corrupt("xdelta");
        }
        target += window.target_length;
    }
    info.target_size = target;
    return {};
}

Status ApplyVcdiff(Context& c) {
    Cursor p{c.patch.data(), c.patch.size()};
    Header header;
    if (p.size < 5 || !ReadHeader(p, header)) {
        return Corrupt("xdelta");
    }
    if (header.custom_table) {
        return Status::Fail("This xdelta patch uses its own instruction table, which is not supported");
    }
    static const std::array<Code, 256> table = StandardTable();
    std::vector<std::uint8_t> target;
    Unpacker unpacker;
    unpacker.compressor = header.compressor;
    while (p.Left() > 0) {
        Window w;
        if (!ReadWindow(p, w) || w.target_length > MAX_WINDOW) {
            return Corrupt("xdelta");
        }
        Cursor* const sections[3] = {&w.data, &w.instructions, &w.addresses};
        for (int kind = 0; kind < 3; ++kind) {
            if (w.packed & (1 << kind)) {
                if (const Status unpacked = unpacker.Unpack(kind, *sections[kind]); !unpacked.ok) {
                    return unpacked;
                }
            }
        }
        const bool from_target = (w.flags & FROM_TARGET) != 0;
        const std::uint64_t source_size = from_target ? c.out.Size() : c.source.Size();
        if (w.source_at > source_size || w.source_length > source_size - w.source_at) {
            return w.flags & FROM_SOURCE
                       ? Status::Fail("This patch was made for a larger ROM than this one (" +
                                      std::to_string(c.source.Size()) + " bytes)")
                       : Corrupt("xdelta");
        }
        target.resize(static_cast<std::size_t>(w.target_length));
        std::size_t at = 0;
        AddressCache cache;
        while (w.instructions.Left() > 0) {
            const Code& code = table[w.instructions.U8()];
            for (const Instruction& step : {code.first, code.second}) {
                if (step.type == NOOP) {
                    continue;
                }
                const std::uint64_t size = step.size != 0 ? step.size : Number(w.instructions);
                if (w.instructions.bad || size > target.size() - at) {
                    return Corrupt("xdelta");
                }
                const std::size_t count = static_cast<std::size_t>(size);
                if (step.type == ADD) {
                    const std::uint8_t* bytes = w.data.Take(count);
                    if (bytes == nullptr) {
                        return Corrupt("xdelta");
                    }
                    std::memcpy(target.data() + at, bytes, count);
                } else if (step.type == RUN) {
                    std::memset(target.data() + at, w.data.U8(), count);
                } else {
                    const std::uint64_t here = w.source_length + at;
                    std::uint64_t address = cache.Decode(w.addresses, here, step.mode);
                    if (w.addresses.bad || address >= here) {
                        return Corrupt("xdelta");
                    }
                    std::size_t left = count;
                    std::size_t to = at;
                    if (address < w.source_length) {
                        // From the window of the source (or of the result so far).
                        const std::size_t take =
                            static_cast<std::size_t>(std::min<std::uint64_t>(left, w.source_length - address));
                        const std::uint64_t from = w.source_at + address;
                        if (!(from_target ? c.out.ReadBack(from, target.data() + to, take)
                                          : c.source.Read(from, target.data() + to, take))) {
                            return Status::Fail("Cannot read " + PathToUtf8(c.source_path));
                        }
                        to += take;
                        left -= take;
                        address += take;
                    }
                    // From this window, possibly bytes this very copy writes.
                    const std::uint8_t* from = target.data() + (address - w.source_length);
                    for (std::size_t i = 0; i < left; ++i) {
                        target[to + i] = from[i];
                    }
                }
                at += count;
            }
            if (w.data.bad) {
                return Corrupt("xdelta");
            }
        }
        if (at != target.size()) {
            return Corrupt("xdelta");
        }
        if ((w.flags & HAS_ADLER) && Adler32(target.data(), target.size()) != w.adler) {
            return Status::Fail(w.flags & FROM_SOURCE
                                    ? "This patch was made for another ROM: the result fails the patch's check"
                                    : "This xdelta patch is damaged or incomplete");
        }
        c.out.Write(target.data(), target.size());
        if (!c.Progress(static_cast<float>(p.at) / static_cast<float>(p.size))) {
            return Cancelled();
        }
    }
    return {};
}

} // namespace den::patching
