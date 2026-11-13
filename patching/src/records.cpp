// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The formats that are a list of places to change in a copy of the source: IPS, APS for the
// GBA and PPF.

#include <algorithm>
#include <cstring>

#include "den/base/files.h"
#include "io.h"

namespace den::patching {

namespace {

constexpr float COPY_SHARE = 0.6f;

bool AtIpsEnd(const Cursor& p) {
    // "EOF" ends the patch, with three bytes of a size to cut the result to after it or
    // nothing. Anywhere else those bytes are the offset 0x454F46 of a record.
    return std::memcmp(p.data + p.at, "EOF", 3) == 0 && (p.Left() == 3 || p.Left() == 6);
}

std::uint16_t Crc16(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size; ++i) {
        crc = static_cast<std::uint16_t>(crc ^ (data[i] << 8));
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<std::uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

constexpr std::size_t APS_BLOCK = 0x10000;
constexpr std::size_t APS_RECORD = 4 + 2 + 2 + APS_BLOCK;

constexpr std::size_t PPF_CHECK = 1024;

struct PpfLayout {
    int version = 0;
    std::size_t records = 0; // where they start
    std::size_t end = 0;     // where they end (a description block may follow)
    bool has_check = false;
    std::uint64_t check_at = 0x9320;
    bool undo = false;
    std::optional<std::uint64_t> source_size;
};

bool ReadPpf(const Cursor& p, PpfLayout& layout) {
    if (p.size < 56 || p.data[4] != '0' || p.data[3] < '1' || p.data[3] > '3') {
        return false;
    }
    layout.version = p.data[3] - '0';
    layout.end = p.size;
    const auto tail = [&](std::size_t length_bytes) {
        // "@BEGIN_FILE_ID.DIZ" text "@END_FILE_ID.DIZ" and the text's length.
        const std::size_t mark = 4 + length_bytes;
        if (p.size < mark || std::memcmp(p.data + p.size - mark, ".DIZ", 4) != 0) {
            return true;
        }
        Cursor length{p.data, p.size, p.size - length_bytes};
        const std::uint64_t whole = length.Le(static_cast<int>(length_bytes)) + 18 + 16 + length_bytes;
        if (whole > p.size) {
            return false;
        }
        layout.end = p.size - static_cast<std::size_t>(whole);
        return true;
    };
    if (layout.version == 1) {
        layout.records = 56;
    } else if (layout.version == 2) {
        layout.records = 60 + PPF_CHECK;
        layout.has_check = true;
        if (p.size < layout.records || !tail(4)) {
            return false;
        }
        Cursor size{p.data, p.size, 56};
        layout.source_size = size.Le(4);
    } else {
        if (p.size < 60) {
            return false;
        }
        layout.check_at = p.data[56] == 1 ? 0x80A0 : 0x9320;
        layout.has_check = p.data[57] != 0;
        layout.undo = p.data[58] != 0;
        layout.records = layout.has_check ? 60 + PPF_CHECK : 60;
        if (p.size < layout.records || !tail(2)) {
            return false;
        }
    }
    return layout.end >= layout.records;
}

} // namespace

Status ApplyIps(Context& c) {
    if (Status status = c.CopyWholeSource(COPY_SHARE); !status) {
        return status;
    }
    Cursor p{c.patch.data(), c.patch.size(), 5};
    std::vector<std::uint8_t> run;
    for (;;) {
        if (p.Left() < 3) {
            return Status::Fail("This IPS patch is incomplete: its end mark is missing");
        }
        if (AtIpsEnd(p)) {
            p.at += 3;
            if (p.Left() == 3) {
                const std::uint64_t cut = p.Be(3);
                if (cut < c.out.Size()) {
                    c.final_size = cut;
                }
            }
            return {};
        }
        const std::uint64_t offset = p.Be(3);
        const std::size_t size = static_cast<std::size_t>(p.Be(2));
        if (size == 0) {
            const std::size_t count = static_cast<std::size_t>(p.Be(2));
            const std::uint8_t value = p.U8();
            run.assign(count, value);
            if (!p.bad) {
                c.out.WriteAt(offset, run.data(), run.size());
            }
        } else if (const std::uint8_t* bytes = p.Take(size)) {
            c.out.WriteAt(offset, bytes, size);
        }
        if (p.bad) {
            return Corrupt("IPS");
        }
        if (!c.Progress(COPY_SHARE + (1 - COPY_SHARE) * static_cast<float>(p.at) / static_cast<float>(p.size))) {
            return Cancelled();
        }
    }
}

Status InspectAps(Cursor p, PatchInfo& info) {
    if (p.size < 12 || (p.size - 12) % APS_RECORD != 0) {
        return Corrupt("APS");
    }
    p.at = 4;
    info.source_size = p.Le(4);
    info.target_size = p.Le(4);
    return {};
}

Status ApplyAps(Context& c) {
    PatchInfo info;
    Cursor p{c.patch.data(), c.patch.size()};
    if (Status status = InspectAps(p, info); !status) {
        return status;
    }
    if (c.options.verify_source && c.source.Size() != *info.source_size) {
        if (c.source.Size() == *info.target_size && *info.target_size != *info.source_size) {
            return Status::Fail("This game already has this patch applied");
        }
        return WrongSource({}, info.source_size, {}, c.source.Size());
    }
    if (Status status = c.CopyWholeSource(COPY_SHARE); !status) {
        return status;
    }
    p.at = 12;
    std::vector<std::uint8_t> block(APS_BLOCK);
    while (p.Left() >= APS_RECORD) {
        const std::uint64_t offset = p.Le(4);
        const auto source_crc = static_cast<std::uint16_t>(p.Le(2));
        const auto target_crc = static_cast<std::uint16_t>(p.Le(2));
        const std::uint8_t* change = p.Take(APS_BLOCK);
        if (!c.source.ReadPadded(offset, block.data(), APS_BLOCK)) {
            return Status::Fail("Cannot read " + PathToUtf8(c.source_path));
        }
        const std::uint16_t have = Crc16(block.data(), APS_BLOCK);
        if (c.options.verify_source && have != source_crc) {
            return Status::Fail(have == target_crc
                                    ? "This game already has this patch applied"
                                    : "This patch was made for another ROM: the block at " +
                                          std::to_string(offset) + " differs");
        }
        for (std::size_t i = 0; i < APS_BLOCK; ++i) {
            block[i] ^= change[i];
        }
        if (c.options.verify_source && Crc16(block.data(), APS_BLOCK) != target_crc) {
            return Corrupt("APS");
        }
        c.out.WriteAt(offset, block.data(), APS_BLOCK);
        if (!c.Progress(COPY_SHARE + (1 - COPY_SHARE) * static_cast<float>(p.at) / static_cast<float>(p.size))) {
            return Cancelled();
        }
    }
    c.final_size = *info.target_size;
    return {};
}

Status InspectPpf(Cursor p, PatchInfo& info) {
    PpfLayout layout;
    if (!ReadPpf(p, layout)) {
        return Corrupt("PPF");
    }
    info.source_size = layout.source_size;
    return {};
}

Status ApplyPpf(Context& c) {
    PpfLayout layout;
    Cursor p{c.patch.data(), c.patch.size()};
    if (!ReadPpf(p, layout)) {
        return Corrupt("PPF");
    }
    if (c.options.verify_source) {
        if (layout.source_size && *layout.source_size != c.source.Size()) {
            return WrongSource({}, layout.source_size, {}, c.source.Size());
        }
        if (layout.has_check) {
            // 1024 bytes of the disc image the patch was made for.
            std::uint8_t there[PPF_CHECK];
            if (!c.source.Read(layout.check_at, there, PPF_CHECK) ||
                std::memcmp(there, p.data + 60, PPF_CHECK) != 0) {
                return Status::Fail("This patch was made for another disc image: its check block differs");
            }
        }
    }
    if (Status status = c.CopyWholeSource(COPY_SHARE); !status) {
        return status;
    }
    p.size = layout.end;
    p.at = layout.records;
    while (p.Left() > 0) {
        const std::uint64_t offset = p.Le(layout.version == 3 ? 8 : 4);
        const std::size_t size = p.U8();
        const std::uint8_t* bytes = p.Take(size);
        if (layout.undo) {
            p.Take(size);
        }
        if (p.bad) {
            return Corrupt("PPF");
        }
        c.out.WriteAt(offset, bytes, size);
        if (!c.Progress(COPY_SHARE + (1 - COPY_SHARE) * static_cast<float>(p.at) / static_cast<float>(p.size))) {
            return Cancelled();
        }
    }
    return {};
}

} // namespace den::patching
