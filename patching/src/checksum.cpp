// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <fstream>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/patching/checksum.h"

namespace den::patching {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t BLOCK = 1u << 20; // a multiple of 4: N64 words never straddle blocks

std::uint32_t Rotl(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

enum class Order { Same, SwapPairs, ReverseWords };

// What the system's other form of this file is, if it has one.
struct Shape {
    std::string form;
    std::uint64_t header = 0;
    Order order = Order::Same;
};

Shape ShapeOf(std::string_view system, const std::uint8_t* head, std::size_t head_size,
              std::uint64_t size) {
    const auto starts = [&](std::string_view magic) {
        return head_size >= magic.size() && std::memcmp(head, magic.data(), magic.size()) == 0;
    };
    if (system == "nes" && size > 16 && (starts("NES\x1A") || starts("FDS\x1A"))) {
        return {"headerless", 16};
    }
    if (system == "snes" && size > 512 && size % 1024 == 512) {
        return {"headerless", 512};
    }
    if (system == "lynx" && size > 64 && starts("LYNX")) {
        return {"headerless", 64};
    }
    if (system == "n64" && size % 4 == 0) {
        if (starts("\x37\x80\x40\x12")) {
            return {"z64", 0, Order::SwapPairs};
        }
        if (starts("\x40\x12\x37\x80")) {
            return {"z64", 0, Order::ReverseWords};
        }
    }
    return {};
}

void Reorder(std::uint8_t* data, std::size_t size, Order order) {
    if (order == Order::SwapPairs) {
        for (std::size_t i = 0; i + 1 < size; i += 2) {
            std::swap(data[i], data[i + 1]);
        }
    } else if (order == Order::ReverseWords) {
        for (std::size_t i = 0; i + 3 < size; i += 4) {
            std::swap(data[i], data[i + 3]);
            std::swap(data[i + 1], data[i + 2]);
        }
    }
}

struct Sums {
    std::uint32_t crc = 0;
    Md5 md5;
    Sha1 sha1;
    std::uint64_t size = 0;

    void Update(const std::uint8_t* data, std::size_t count) {
        crc = Crc32Update(crc, data, count);
        md5.Update(data, count);
        sha1.Update(data, count);
        size += count;
    }
    Checksums Finish() {
        return {crc, md5.FinishHex(), sha1.FinishHex(), size};
    }
};

// Feeds the file to `take` in blocks; false when it cannot be read or `cancelled` said so.
template <typename Take>
bool ReadBlocks(const fs::path& file, const std::function<bool()>& cancelled, Take&& take) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<std::uint8_t> block(BLOCK);
    for (;;) {
        in.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()));
        const std::size_t got = static_cast<std::size_t>(in.gcount());
        if (got == 0) {
            return in.eof();
        }
        take(block.data(), got);
        if (cancelled && cancelled()) {
            return false;
        }
    }
}

Shape ShapeOfFile(const fs::path& rom, std::string_view system) {
    std::uint8_t head[8] = {};
    std::ifstream in(rom, std::ios::binary);
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    const std::size_t got = static_cast<std::size_t>(in.gcount());
    std::error_code ec;
    return ShapeOf(system, head, got, fs::file_size(rom, ec));
}

Status CannotRead(const fs::path& file) {
    return Status::Fail("Cannot read " + PathToUtf8(file));
}

} // namespace

Sha1::Sha1() : h{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u} {}

void Sha1::Block(const std::uint8_t* p) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = static_cast<std::uint32_t>(p[i * 4]) << 24 | static_cast<std::uint32_t>(p[i * 4 + 1]) << 16 |
               static_cast<std::uint32_t>(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f = 0, k = 0;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const std::uint32_t next = Rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rotl(b, 30);
        b = a;
        a = next;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void Sha1::Update(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    total += size;
    while (size > 0) {
        if (pending_size == 0 && size >= 64) {
            Block(p);
            p += 64;
            size -= 64;
            continue;
        }
        const std::size_t take = std::min(size, 64 - pending_size);
        std::memcpy(pending.data() + pending_size, p, take);
        pending_size += take;
        p += take;
        size -= take;
        if (pending_size == 64) {
            Block(pending.data());
            pending_size = 0;
        }
    }
}

std::string Sha1::FinishHex() {
    const std::uint64_t bits = total * 8;
    const std::uint8_t one = 0x80;
    Update(&one, 1);
    const std::uint8_t zero = 0;
    while (pending_size != 56) {
        Update(&zero, 1);
    }
    std::uint8_t length[8];
    for (int i = 0; i < 8; ++i) {
        length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    }
    Update(length, 8);
    static constexpr char HEX[] = "0123456789abcdef";
    std::string text;
    for (const std::uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            text.push_back(HEX[(word >> shift) & 0xF]);
        }
    }
    return text;
}

std::string Sha1Hex(const void* data, std::size_t size) {
    Sha1 sha;
    sha.Update(data, size);
    return sha.FinishHex();
}

Result<std::vector<RomForm>> RomChecksums(const fs::path& rom, std::string_view system,
                                          const std::function<bool()>& cancelled) {
    const Shape shape = ShapeOfFile(rom, system);
    Sums raw, other;
    std::uint64_t skip = shape.header;
    const bool read = ReadBlocks(rom, cancelled, [&](std::uint8_t* data, std::size_t count) {
        raw.Update(data, count);
        if (shape.form.empty()) {
            return;
        }
        const std::size_t skipped = static_cast<std::size_t>(std::min<std::uint64_t>(skip, count));
        skip -= skipped;
        Reorder(data, count, shape.order);
        other.Update(data + skipped, count - skipped);
    });
    if (!read) {
        return cancelled && cancelled() ? Status::Fail("Cancelled") : CannotRead(rom);
    }
    std::vector<RomForm> forms;
    forms.push_back({"raw", raw.Finish(), 0, false});
    if (!shape.form.empty()) {
        forms.push_back({shape.form, other.Finish(), shape.header, shape.order != Order::Same});
    }
    return forms;
}

Status WriteForm(const fs::path& rom, std::string_view system, std::string_view form,
                 const fs::path& out) {
    Shape shape;
    if (form != "raw") {
        shape = ShapeOfFile(rom, system);
        if (shape.form != form) {
            return Status::Fail(PathToUtf8(rom.filename()) + " has no " + std::string(form) + " form");
        }
    }
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    std::uint64_t skip = shape.header;
    const bool read = ReadBlocks(rom, {}, [&](std::uint8_t* data, std::size_t count) {
        const std::size_t skipped = static_cast<std::size_t>(std::min<std::uint64_t>(skip, count));
        skip -= skipped;
        Reorder(data, count, shape.order);
        file.write(reinterpret_cast<const char*>(data + skipped), static_cast<std::streamsize>(count - skipped));
    });
    file.flush();
    if (!read) {
        return CannotRead(rom);
    }
    return file.good() ? Status{} : Status::Fail("Cannot write " + PathToUtf8(out));
}

Status RestoreHeader(const fs::path& original, const RomForm& form, const fs::path& patched) {
    if (form.header_bytes == 0) {
        return {};
    }
    std::vector<char> header(static_cast<std::size_t>(form.header_bytes));
    std::ifstream first(original, std::ios::binary);
    if (!first.read(header.data(), static_cast<std::streamsize>(header.size()))) {
        return CannotRead(original);
    }
    fs::path joined = patched;
    joined += ".header";
    std::ofstream file(joined, std::ios::binary | std::ios::trunc);
    file.write(header.data(), static_cast<std::streamsize>(header.size()));
    const bool read = ReadBlocks(patched, {}, [&](std::uint8_t* data, std::size_t count) {
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(count));
    });
    file.flush();
    const bool written = file.good();
    file.close();
    std::error_code ec;
    if (!read || !written) {
        fs::remove(joined, ec);
        return read ? Status::Fail("Cannot write " + PathToUtf8(joined)) : CannotRead(patched);
    }
    fs::remove(patched, ec);
    fs::rename(joined, patched, ec);
    return ec ? Status::Fail("Cannot write " + PathToUtf8(patched)) : Status{};
}

} // namespace den::patching
