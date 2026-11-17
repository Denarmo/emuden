// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <vector>

#include "den/patching/xz.h"
#include "feature/tar_xz.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t BLOCK = 512;
constexpr std::uint64_t MAX_NAME_DATA = 1u << 16; // a long name or a pax header

using Header = std::array<std::uint8_t, BLOCK>;

std::string Field(const Header& header, std::size_t at, std::size_t size) {
    const char* start = reinterpret_cast<const char*>(header.data() + at);
    return std::string(start, std::find(start, start + size, '\0'));
}

// Octal text, or the binary form GNU tar writes for sizes over 8 GiB.
std::uint64_t Number(const Header& header, std::size_t at, std::size_t size) {
    std::uint64_t value = 0;
    if ((header[at] & 0x80) != 0) {
        for (std::size_t i = size > 8 ? size - 8 : 1; i < size; ++i) {
            value = (value << 8) | header[at + i];
        }
        return value;
    }
    for (std::size_t i = 0; i < size; ++i) {
        const char c = static_cast<char>(header[at + i]);
        if (c >= '0' && c <= '7') {
            value = (value << 3) | static_cast<std::uint64_t>(c - '0');
        } else if (c != ' ' || value != 0) {
            break;
        }
    }
    return value;
}

bool ChecksumFits(const Header& header) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < BLOCK; ++i) {
        sum += i >= 148 && i < 156 ? std::uint8_t{' '} : header[i];
    }
    return sum == Number(header, 148, 8);
}

// The value of `key` in pax records ("<length> <key>=<value>\n"), or empty.
std::string PaxValue(const std::string& records, std::string_view key) {
    std::string found;
    std::size_t at = 0;
    while (at < records.size()) {
        const std::size_t space = records.find(' ', at);
        if (space == std::string::npos) {
            break;
        }
        const std::size_t length = static_cast<std::size_t>(std::strtoull(records.c_str() + at, nullptr, 10));
        if (length <= space - at + 1 || at + length > records.size()) {
            break;
        }
        const std::string_view record(records.data() + space + 1, at + length - space - 2);
        if (record.size() > key.size() && record.starts_with(key) && record[key.size()] == '=') {
            found = std::string(record.substr(key.size() + 1));
        }
        at += length;
    }
    return found;
}

class Walk {
public:
    Walk(const fs::path& archive, const std::function<bool()>& cancelled) : xz_(archive), cancelled_(cancelled) {}

    // False at the end of the data or when it is damaged (`damaged`).
    bool Block(Header& header, bool& damaged) {
        std::size_t got = 0;
        if (!xz_.Read(header.data(), BLOCK, got) || (got != 0 && got != BLOCK)) {
            damaged = true;
            return false;
        }
        return got == BLOCK;
    }

    // The member's data with its padding, into `text`, `file` or nowhere.
    bool Data(std::uint64_t size, std::string* text, std::ofstream* file) {
        std::vector<std::uint8_t> buffer(1u << 16);
        std::uint64_t left = (size + BLOCK - 1) / BLOCK * BLOCK;
        std::uint64_t data = size;
        while (left > 0) {
            if (cancelled_ && cancelled_()) {
                return false;
            }
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(left, buffer.size()));
            std::size_t got = 0;
            if (!xz_.Read(buffer.data(), want, got) || got != want) {
                return false;
            }
            const std::size_t part = static_cast<std::size_t>(std::min<std::uint64_t>(data, got));
            if (text != nullptr) {
                text->append(reinterpret_cast<const char*>(buffer.data()), part);
            } else if (file != nullptr) {
                file->write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(part));
            }
            data -= part;
            left -= got;
        }
        return true;
    }

private:
    patching::XzFileReader xz_;
    const std::function<bool()>& cancelled_;
};

} // namespace

Status ReadTarXz(const fs::path& archive, const std::function<Result<fs::path>(const TarMember&)>& take,
                 const std::function<bool()>& cancelled) {
    const Status damaged = Status::Fail("the archive is damaged or not a tar.xz");
    const auto stopped = [&] { return cancelled && cancelled() ? Status::Fail("cancelled") : damaged; };
    Walk walk(archive, cancelled);
    std::string long_name, pax_name, pax_size;
    Header header;
    bool bad = false;
    bool any = false;
    while (walk.Block(header, bad)) {
        if (std::all_of(header.begin(), header.end(), [](std::uint8_t b) { return b == 0; })) {
            break; // the end marker
        }
        if (!ChecksumFits(header)) {
            return damaged;
        }
        any = true;
        const char type = static_cast<char>(header[156]);
        const std::uint64_t size = Number(header, 124, 12);
        if (type == 'L' || type == 'x' || type == 'K' || type == 'g') {
            std::string text;
            if (size > MAX_NAME_DATA || !walk.Data(size, &text, nullptr)) {
                return stopped();
            }
            if (type == 'L') {
                long_name = text.substr(0, text.find('\0'));
            } else if (type == 'x') {
                pax_name = PaxValue(text, "path");
                pax_size = PaxValue(text, "size");
            }
            continue;
        }
        TarMember member;
        const std::string prefix = Field(header, 257, 5) == "ustar" ? Field(header, 345, 155) : std::string();
        member.name = !pax_name.empty()    ? pax_name
                      : !long_name.empty() ? long_name
                      : prefix.empty()     ? Field(header, 0, 100)
                                           : prefix + "/" + Field(header, 0, 100);
        while (member.name.starts_with("./")) {
            member.name.erase(0, 2);
        }
        member.size = pax_size.empty() ? size : std::strtoull(pax_size.c_str(), nullptr, 10);
        member.file = type == '0' || type == '\0' || type == '7';
        long_name.clear();
        pax_name.clear();
        pax_size.clear();
        // Links and folders carry no data, whatever their size field says.
        const bool empty = type == '1' || type == '2' || type == '5';
        const std::uint64_t data = member.file ? member.size : empty ? 0 : size;
        Result<fs::path> wanted = take(member);
        if (!wanted) {
            return wanted.status();
        }
        if (wanted.value().empty() || !member.file) {
            if (!walk.Data(data, nullptr, nullptr)) {
                return stopped();
            }
            continue;
        }
        fs::path part = wanted.value();
        part += ".part";
        std::error_code ec;
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Status::Fail("the file could not be written");
        }
        const bool read = walk.Data(data, nullptr, &out);
        out.close();
        if (!read || !out) {
            fs::remove(part, ec);
            return read ? Status::Fail("the file could not be written") : stopped();
        }
        fs::remove(wanted.value(), ec);
        fs::rename(part, wanted.value(), ec);
        if (ec) {
            fs::remove(part, ec);
            return Status::Fail("the file could not be written");
        }
    }
    return bad || !any ? damaged : Status();
}

} // namespace den::plugins::feature
