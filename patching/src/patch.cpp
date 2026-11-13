// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/patching/patch.h"
#include "io.h"

namespace den::patching {

namespace fs = std::filesystem;

namespace {

// Patches are small next to what they change; a file larger than this is not one.
constexpr std::uint64_t MAX_PATCH = 1ull << 30;

struct Kind {
    Format format;
    const char* name;
    const char* magic;
    std::size_t magic_size;
};

constexpr Kind KINDS[] = {
    {Format::Ips, "ips", "PATCH", 5},        {Format::Ups, "ups", "UPS1", 4},
    {Format::Bps, "bps", "BPS1", 4},         {Format::ApsGba, "aps", "APS1", 4},
    {Format::Vcdiff, "xdelta", "\xD6\xC3\xC4", 3}, {Format::Ppf, "ppf", "PPF", 3},
};

const Kind* KindOf(const std::vector<std::uint8_t>& data) {
    for (const Kind& kind : KINDS) {
        if (data.size() >= kind.magic_size && std::memcmp(data.data(), kind.magic, kind.magic_size) == 0) {
            // "APS10" is the N64 flavour, another format.
            if (kind.format == Format::ApsGba && data.size() > 4 && data[4] == '0') {
                continue;
            }
            return &kind;
        }
    }
    return nullptr;
}

Result<std::vector<std::uint8_t>> ReadPatch(const fs::path& patch) {
    std::error_code ec;
    const std::uint64_t size = fs::file_size(patch, ec);
    if (ec) {
        return Status::Fail("Cannot read " + PathToUtf8(patch));
    }
    if (size > MAX_PATCH) {
        return Status::Fail(PathToUtf8(patch.filename()) + " is too large to be a patch");
    }
    auto data = ReadBinaryFile(patch);
    if (!data) {
        return Status::Fail("Cannot read " + PathToUtf8(patch));
    }
    return std::move(*data);
}

Status NotAPatch(const fs::path& patch) {
    return Status::Fail(PathToUtf8(patch.filename()) +
                        " is not a patch this app knows (IPS, UPS, BPS, APS, PPF, xdelta)");
}

} // namespace

std::string CrcText(std::uint32_t crc) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string text(8, '0');
    for (int i = 7; i >= 0; --i) {
        text[i] = HEX[crc & 0xF];
        crc >>= 4;
    }
    return text;
}

bool IsPatchExtension(std::string_view extension) {
    static constexpr std::string_view KNOWN[] = {"ips", "ups", "bps", "aps", "ppf", "xdelta", "xdelta3", "vcdiff"};
    const std::string lower = ToLower(extension);
    return std::find(std::begin(KNOWN), std::end(KNOWN), lower) != std::end(KNOWN);
}

Result<PatchInfo> Inspect(const fs::path& patch) {
    auto data = ReadPatch(patch);
    if (!data) {
        return data.status();
    }
    const Kind* kind = KindOf(*data);
    if (kind == nullptr) {
        return NotAPatch(patch);
    }
    PatchInfo info;
    info.format = kind->format;
    info.format_name = kind->name;
    const Cursor p{data->data(), data->size()};
    Status status;
    switch (kind->format) {
    case Format::Ups:
        status = InspectUps(p, info);
        break;
    case Format::Bps:
        status = InspectBps(p, info);
        break;
    case Format::ApsGba:
        status = InspectAps(p, info);
        break;
    case Format::Ppf:
        status = InspectPpf(p, info);
        break;
    case Format::Vcdiff:
        status = InspectVcdiff(p, info);
        break;
    default:
        break;
    }
    if (!status) {
        return status;
    }
    return info;
}

Status Apply(const fs::path& patch, const fs::path& source, const fs::path& output,
             const ApplyOptions& options) {
    auto data = ReadPatch(patch);
    if (!data) {
        return data.status();
    }
    const Kind* kind = KindOf(*data);
    if (kind == nullptr) {
        return NotAPatch(patch);
    }
    std::error_code ec;
    if (fs::equivalent(source, output, ec)) {
        return Status::Fail("The patched game cannot replace the file it is made from");
    }
    Reader reader;
    if (!reader.Open(source)) {
        return Status::Fail("Cannot read " + PathToUtf8(source));
    }
    fs::path partial = output;
    partial += ".part";
    Sink sink;
    if (Status opened = sink.Open(partial); !opened) {
        return opened;
    }
    Context context{*data, reader, sink, options, source};
    Status status;
    switch (context.Progress(0.0f) ? kind->format : Format::Unknown) {
    case Format::Unknown:
        status = Cancelled();
        break;
    case Format::Ips:
        status = ApplyIps(context);
        break;
    case Format::Ups:
        status = ApplyUps(context);
        break;
    case Format::Bps:
        status = ApplyBps(context);
        break;
    case Format::ApsGba:
        status = ApplyAps(context);
        break;
    case Format::Ppf:
        status = ApplyPpf(context);
        break;
    default:
        status = ApplyVcdiff(context);
        break;
    }
    const Status closed = sink.Close(status ? context.final_size : std::nullopt);
    if (status && !closed) {
        status = closed;
    }
    if (status && context.target_crc) {
        const auto crc = Crc32File(partial);
        if (!crc || *crc != *context.target_crc) {
            status = Status::Fail("The patched game fails the patch's check (CRC32 " +
                                  (crc ? CrcText(*crc) : std::string("unreadable")) + ", expected " +
                                  CrcText(*context.target_crc) + ")");
        }
    }
    if (status) {
        fs::remove(output, ec);
        fs::rename(partial, output, ec);
        if (ec) {
            status = Status::Fail("Cannot write " + PathToUtf8(output));
        }
    }
    if (!status) {
        fs::remove(partial, ec);
        return status;
    }
    if (options.progress) {
        options.progress(1.0f);
    }
    return {};
}

} // namespace den::patching
