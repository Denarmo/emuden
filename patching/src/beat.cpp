// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// UPS and BPS: both end with the CRC-32 of the source, the target and the patch itself.

#include <algorithm>
#include <cstring>

#include "den/base/files.h"
#include "io.h"

namespace den::patching {

namespace {

constexpr std::size_t FOOTER = 12;

struct Footer {
    std::uint32_t source = 0, target = 0, patch = 0;
};

Footer ReadFooter(const std::uint8_t* data, std::size_t size) {
    Cursor end{data, size, size - FOOTER};
    Footer footer;
    footer.source = static_cast<std::uint32_t>(end.Le(4));
    footer.target = static_cast<std::uint32_t>(end.Le(4));
    footer.patch = static_cast<std::uint32_t>(end.Le(4));
    return footer;
}

Status InspectBeat(Cursor p, PatchInfo& info, std::string_view name) {
    if (p.size < 4 + 2 + FOOTER) {
        return Corrupt(name);
    }
    p.at = 4;
    info.source_size = ReadBeatNumber(p);
    info.target_size = ReadBeatNumber(p);
    if (p.bad) {
        return Corrupt(name);
    }
    const Footer footer = ReadFooter(p.data, p.size);
    info.source_crc32 = footer.source;
    info.target_crc32 = footer.target;
    return {};
}

// The header both formats share, the patch's own checksum and the source's.
Status Begin(Context& c, std::string_view name, PatchInfo& info) {
    const Cursor whole{c.patch.data(), c.patch.size()};
    if (Status status = InspectBeat(whole, info, name); !status) {
        return status;
    }
    const Footer footer = ReadFooter(c.patch.data(), c.patch.size());
    if (Crc32(c.patch.data(), c.patch.size() - 4) != footer.patch) {
        return Corrupt(name);
    }
    if (c.options.verify_source) {
        const auto crc = c.SourceCrc();
        if (c.source.Size() != *info.source_size || !crc || *crc != footer.source) {
            if (crc && *crc == footer.target && c.source.Size() == *info.target_size) {
                return Status::Fail("This game already has this patch applied");
            }
            return WrongSource(footer.source, info.source_size, crc, c.source.Size());
        }
    }
    c.target_crc = footer.target;
    c.final_size = info.target_size;
    return {};
}

} // namespace

Status InspectUps(Cursor p, PatchInfo& info) {
    return InspectBeat(p, info, "UPS");
}

Status InspectBps(Cursor p, PatchInfo& info) {
    return InspectBeat(p, info, "BPS");
}

Status ApplyUps(Context& c) {
    PatchInfo info;
    if (Status status = Begin(c, "UPS", info); !status) {
        return status;
    }
    Cursor p{c.patch.data(), c.patch.size() - FOOTER, 4};
    ReadBeatNumber(p);
    ReadBeatNumber(p);
    const std::uint64_t target = *info.target_size;
    std::uint64_t at = 0;
    std::vector<std::uint8_t> run;
    while (p.Left() > 0 && at < target) {
        const std::uint64_t skip = std::min(ReadBeatNumber(p), target - at);
        if (p.bad || !c.CopySource(at, skip)) {
            return Corrupt("UPS");
        }
        at += skip;
        // Bytes to XOR with the source, up to and with a zero.
        const std::uint8_t* start = p.data + p.at;
        const auto* zero = static_cast<const std::uint8_t*>(std::memchr(start, 0, p.Left()));
        const std::size_t length = zero != nullptr ? static_cast<std::size_t>(zero - start) + 1 : p.Left();
        p.at += length;
        const std::size_t kept = static_cast<std::size_t>(std::min<std::uint64_t>(length, target - at));
        run.resize(kept);
        if (kept > 0 && !c.source.ReadPadded(at, run.data(), kept)) {
            return Status::Fail("Cannot read " + PathToUtf8(c.source_path));
        }
        for (std::size_t i = 0; i < kept; ++i) {
            run[i] ^= start[i];
        }
        c.out.Write(run.data(), kept);
        at += kept;
        if (!c.Progress(static_cast<float>(at) / static_cast<float>(target))) {
            return Cancelled();
        }
    }
    if (!c.CopySource(at, target - at)) {
        return Status::Fail("Cannot read " + PathToUtf8(c.source_path));
    }
    return {};
}

Status ApplyBps(Context& c) {
    PatchInfo info;
    if (Status status = Begin(c, "BPS", info); !status) {
        return status;
    }
    Cursor p{c.patch.data(), c.patch.size() - FOOTER, 4};
    ReadBeatNumber(p);
    ReadBeatNumber(p);
    const std::uint64_t metadata = ReadBeatNumber(p);
    if (p.bad || p.Take(static_cast<std::size_t>(std::min<std::uint64_t>(metadata, p.Left() + 1))) == nullptr) {
        return Corrupt("BPS");
    }
    const std::uint64_t source_size = c.source.Size();
    const std::uint64_t target = *info.target_size;
    std::uint64_t at = 0;
    std::uint64_t source_at = 0;
    std::uint64_t target_at = 0;
    // A signed step from where the last copy of that kind ended.
    const auto move = [&p](std::uint64_t& position) {
        const std::uint64_t step = ReadBeatNumber(p);
        const std::uint64_t distance = step >> 1;
        if (step & 1) {
            if (distance > position) {
                return false;
            }
            position -= distance;
        } else {
            position += distance;
        }
        return !p.bad;
    };
    std::uint64_t reported = 0;
    while (p.Left() > 0) {
        const std::uint64_t word = ReadBeatNumber(p);
        const std::uint64_t length = (word >> 2) + 1;
        if (p.bad || length > target - at) {
            return Corrupt("BPS");
        }
        bool good = true;
        switch (word & 3) {
        case 0: // the same bytes of the source
            good = at + length <= source_size && c.CopySource(at, length);
            break;
        case 1: { // bytes of the patch
            const std::uint8_t* bytes = p.Take(static_cast<std::size_t>(length));
            good = bytes != nullptr;
            if (good) {
                c.out.Write(bytes, static_cast<std::size_t>(length));
            }
            break;
        }
        case 2: // bytes from elsewhere in the source
            good = move(source_at) && source_at <= source_size && length <= source_size - source_at &&
                   c.CopySource(source_at, length);
            source_at += length;
            break;
        default: // bytes the result already has
            good = move(target_at) && target_at < at && c.CopyTarget(target_at, length);
            target_at += length;
            break;
        }
        if (!good) {
            return Corrupt("BPS");
        }
        at += length;
        if (at - reported >= (1u << 20)) {
            reported = at;
            if (!c.Progress(static_cast<float>(at) / static_cast<float>(target))) {
                return Cancelled();
            }
        }
    }
    return at == target ? Status{} : Corrupt("BPS");
}

} // namespace den::patching
