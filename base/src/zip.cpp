// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>

#ifdef DEN_HAVE_ZLIB
#include <zlib.h>
#endif

#include "den/base/zip.h"

namespace den {

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t END_SIGNATURE = 0x06054B50;
constexpr std::uint32_t END64_LOCATOR_SIGNATURE = 0x07064B50;
constexpr std::uint32_t END64_SIGNATURE = 0x06064B50;
constexpr std::uint32_t CENTRAL_SIGNATURE = 0x02014B50;
constexpr std::uint32_t LOCAL_SIGNATURE = 0x04034B50;
constexpr std::size_t END_SIZE = 22;
constexpr std::size_t CENTRAL_SIZE = 46;
constexpr std::size_t LOCAL_SIZE = 30;
constexpr std::uint64_t MAX_DIRECTORY = 64u << 20;
constexpr std::size_t BLOCK = 1u << 20;

std::uint64_t Read(const std::uint8_t* data, int bytes) {
    std::uint64_t value = 0;
    for (int i = bytes - 1; i >= 0; --i) {
        value = (value << 8) | data[i];
    }
    return value;
}

bool ReadAt(std::ifstream& file, std::uint64_t offset, std::uint8_t* out, std::size_t size) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(file.gcount()) == size;
}

std::uint32_t Checksum(std::uint32_t crc, const std::uint8_t* data, std::size_t size) {
#ifdef DEN_HAVE_ZLIB
    return static_cast<std::uint32_t>(crc32(crc, data, static_cast<uInt>(size)));
#else
    static const auto table = [] {
        std::array<std::uint32_t, 256> out{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            out[i] = c;
        }
        return out;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
#endif
}

} // namespace

std::optional<std::vector<ZipEntry>> ListZip(const fs::path& archive) {
    std::error_code ec;
    const std::uint64_t file_size = fs::file_size(archive, ec);
    if (ec || file_size < END_SIZE) {
        return std::nullopt;
    }
    std::ifstream file(archive, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    // The end record is the last thing in the file, before a comment of at most 64 KiB.
    const std::uint64_t tail_size = std::min<std::uint64_t>(file_size, END_SIZE + 0xFFFF);
    std::vector<std::uint8_t> tail(static_cast<std::size_t>(tail_size));
    if (!ReadAt(file, file_size - tail_size, tail.data(), tail.size())) {
        return std::nullopt;
    }
    std::size_t end = tail.size();
    for (std::size_t at = tail.size() - END_SIZE + 1; at-- > 0;) {
        if (Read(&tail[at], 4) == END_SIGNATURE) {
            end = at;
            break;
        }
    }
    if (end == tail.size()) {
        return std::nullopt;
    }
    std::uint64_t count = Read(&tail[end + 10], 2);
    std::uint64_t directory_size = Read(&tail[end + 12], 4);
    std::uint64_t directory_offset = Read(&tail[end + 16], 4);

    const std::uint64_t end_offset = file_size - tail_size + end;
    std::uint8_t locator[20];
    if (end_offset >= sizeof(locator) &&
        ReadAt(file, end_offset - sizeof(locator), locator, sizeof(locator)) &&
        Read(locator, 4) == END64_LOCATOR_SIGNATURE) {
        std::uint8_t end64[56];
        if (!ReadAt(file, Read(locator + 8, 8), end64, sizeof(end64)) ||
            Read(end64, 4) != END64_SIGNATURE) {
            return std::nullopt;
        }
        count = Read(end64 + 32, 8);
        directory_size = Read(end64 + 40, 8);
        directory_offset = Read(end64 + 48, 8);
    }
    if (directory_size > MAX_DIRECTORY || directory_offset > file_size ||
        directory_size > file_size - directory_offset) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> directory(static_cast<std::size_t>(directory_size));
    if (!directory.empty() && !ReadAt(file, directory_offset, directory.data(), directory.size())) {
        return std::nullopt;
    }
    std::vector<ZipEntry> entries;
    std::size_t at = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (at + CENTRAL_SIZE > directory.size() || Read(&directory[at], 4) != CENTRAL_SIGNATURE) {
            return std::nullopt;
        }
        const std::uint8_t* record = &directory[at];
        const std::size_t name_size = static_cast<std::size_t>(Read(record + 28, 2));
        const std::size_t extra_size = static_cast<std::size_t>(Read(record + 30, 2));
        const std::size_t comment_size = static_cast<std::size_t>(Read(record + 32, 2));
        if (at + CENTRAL_SIZE + name_size + extra_size + comment_size > directory.size()) {
            return std::nullopt;
        }
        ZipEntry entry;
        entry.encrypted = (Read(record + 8, 2) & 1) != 0;
        entry.method = static_cast<std::uint16_t>(Read(record + 10, 2));
        entry.crc = static_cast<std::uint32_t>(Read(record + 16, 4));
        entry.compressed_size = Read(record + 20, 4);
        entry.size = Read(record + 24, 4);
        entry.header_offset = Read(record + 42, 4);
        // Made on Unix (host 3): the high word of the external attributes is the file mode.
        entry.symlink = Read(record + 5, 1) == 3 && ((Read(record + 38, 4) >> 16) & 0xF000) == 0xA000;
        entry.name.assign(reinterpret_cast<const char*>(record + CENTRAL_SIZE), name_size);
        std::replace(entry.name.begin(), entry.name.end(), '\\', '/');

        // Zip64: the fields that did not fit follow in this order.
        const std::uint8_t* extra = record + CENTRAL_SIZE + name_size;
        for (std::size_t e = 0; e + 4 <= extra_size;) {
            const std::uint64_t id = Read(extra + e, 2);
            const std::size_t size = static_cast<std::size_t>(Read(extra + e + 2, 2));
            if (e + 4 + size > extra_size) {
                break;
            }
            if (id == 1) {
                std::size_t field = e + 4;
                for (std::uint64_t* value :
                     {&entry.size, &entry.compressed_size, &entry.header_offset}) {
                    if (*value == 0xFFFFFFFFu && field + 8 <= e + 4 + size) {
                        *value = Read(extra + field, 8);
                        field += 8;
                    }
                }
            }
            e += 4 + size;
        }
        at += CENTRAL_SIZE + name_size + extra_size + comment_size;
        if (!entry.name.empty() && entry.name.back() != '/') {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

bool CanExtractZipEntry(const ZipEntry& entry) {
    if (entry.encrypted || entry.symlink) {
        return false;
    }
#ifdef DEN_HAVE_ZLIB
    return entry.method == 0 || entry.method == 8;
#else
    return entry.method == 0;
#endif
}

Status ExtractZipEntry(const fs::path& archive, const ZipEntry& entry, const fs::path& destination,
                         const std::function<bool()>& cancelled) {
    if (!CanExtractZipEntry(entry)) {
        return Status::Fail(entry.name + " is packed in a way this build cannot read");
    }
    std::ifstream file(archive, std::ios::binary);
    std::uint8_t local[LOCAL_SIZE];
    if (!file || !ReadAt(file, entry.header_offset, local, sizeof(local)) ||
        Read(local, 4) != LOCAL_SIGNATURE) {
        return Status::Fail("cannot read " + PathToUtf8(archive));
    }
    file.clear();
    file.seekg(static_cast<std::streamoff>(entry.header_offset + LOCAL_SIZE + Read(local + 26, 2) +
                                           Read(local + 28, 2)));

    fs::path partial = destination;
    partial += ".part";
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Status::Fail("cannot write " + PathToUtf8(destination));
    }
    const auto fail = [&](std::string message) {
        out.close();
        std::error_code ec;
        fs::remove(partial, ec);
        return Status::Fail(std::move(message));
    };

    std::vector<std::uint8_t> input(BLOCK);
    std::vector<std::uint8_t> output(BLOCK);
    std::uint64_t remaining = entry.compressed_size;
    std::uint64_t written = 0;
    std::uint32_t crc = 0;
    bool too_large = entry.method == 0 && entry.compressed_size != entry.size;
    const auto write = [&](const std::uint8_t* data, std::size_t size) {
        // Never more than the archive announced: callers cap sizes by that number.
        if (size > entry.size - written) {
            too_large = true;
            return false;
        }
        crc = Checksum(crc, data, size);
        written += size;
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        return static_cast<bool>(out);
    };
#ifdef DEN_HAVE_ZLIB
    z_stream stream{};
    if (entry.method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return fail("cannot unpack " + entry.name);
    }
    bool finished = entry.method != 8;
#endif
    const std::string damaged = PathToUtf8(archive) + " is damaged (" + entry.name + ")";
    std::string error = too_large ? damaged : std::string();
    while (remaining > 0 && error.empty()) {
        if (cancelled && cancelled()) {
            error = "cancelled";
            break;
        }
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, BLOCK));
        file.read(reinterpret_cast<char*>(input.data()), static_cast<std::streamsize>(want));
        if (static_cast<std::size_t>(file.gcount()) != want) {
            error = damaged;
            break;
        }
        remaining -= want;
        if (entry.method == 0) {
            if (!write(input.data(), want)) {
                error = too_large ? damaged
                                  : "cannot write " + PathToUtf8(destination) + " (is the disk full?)";
            }
            continue;
        }
#ifdef DEN_HAVE_ZLIB
        stream.next_in = input.data();
        stream.avail_in = static_cast<uInt>(want);
        while (stream.avail_in > 0 && !finished && error.empty()) {
            stream.next_out = output.data();
            stream.avail_out = static_cast<uInt>(output.size());
            const int code = inflate(&stream, Z_NO_FLUSH);
            if (code != Z_OK && code != Z_STREAM_END) {
                error = damaged;
                break;
            }
            if (!write(output.data(), output.size() - stream.avail_out)) {
                error = too_large ? damaged
                                  : "cannot write " + PathToUtf8(destination) + " (is the disk full?)";
            }
            finished = code == Z_STREAM_END;
        }
#endif
    }
#ifdef DEN_HAVE_ZLIB
    if (entry.method == 8) {
        inflateEnd(&stream);
        if (error.empty() && !finished) {
            error = damaged;
        }
    }
#endif
    if (error.empty() && (written != entry.size || crc != entry.crc)) {
        error = damaged;
    }
    out.close();
    if (!error.empty() || !out) {
        return fail(error.empty() ? "cannot write " + PathToUtf8(destination) : error);
    }
    std::error_code ec;
    fs::rename(partial, destination, ec);
    if (ec) {
        return fail("cannot write " + PathToUtf8(destination));
    }
    return {};
}

} // namespace den
