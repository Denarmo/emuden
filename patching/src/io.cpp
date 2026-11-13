// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "io.h"

namespace den::patching {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t PAGE = 256u << 10;
constexpr std::size_t PAGES = 16;
constexpr std::size_t WRITE_BUFFER = 1u << 20;
constexpr std::size_t CHUNK = 64u << 10;

} // namespace

bool Reader::Open(const fs::path& file) {
    std::error_code ec;
    size_ = fs::file_size(file, ec);
    if (ec) {
        return false;
    }
    file_.open(file, std::ios::binary);
    return file_.is_open();
}

const Reader::Page* Reader::PageAt(std::uint64_t index) {
    for (Page& page : pages_) {
        if (page.index == index && page.used != 0) {
            page.used = ++clock_;
            return &page;
        }
    }
    Page* page = nullptr;
    if (pages_.size() < PAGES) {
        pages_.emplace_back();
        pages_.back().data.resize(PAGE);
        page = &pages_.back();
    } else {
        page = &*std::min_element(pages_.begin(), pages_.end(),
                                  [](const Page& a, const Page& b) { return a.used < b.used; });
    }
    const std::uint64_t start = index * PAGE;
    const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(PAGE, size_ - start));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(start));
    file_.read(reinterpret_cast<char*>(page->data.data()), static_cast<std::streamsize>(want));
    if (static_cast<std::size_t>(file_.gcount()) != want) {
        page->used = 0;
        return nullptr;
    }
    page->index = index;
    page->used = ++clock_;
    return page;
}

bool Reader::Read(std::uint64_t offset, void* into, std::size_t size) {
    if (offset > size_ || size > size_ - offset) {
        return false;
    }
    auto* out = static_cast<std::uint8_t*>(into);
    while (size > 0) {
        const Page* page = PageAt(offset / PAGE);
        if (page == nullptr) {
            return false;
        }
        const std::size_t in_page = static_cast<std::size_t>(offset % PAGE);
        const std::size_t take = std::min(size, PAGE - in_page);
        std::memcpy(out, page->data.data() + in_page, take);
        out += take;
        offset += take;
        size -= take;
    }
    return true;
}

bool Reader::ReadPadded(std::uint64_t offset, void* into, std::size_t size) {
    const std::size_t there =
        offset >= size_ ? 0 : static_cast<std::size_t>(std::min<std::uint64_t>(size, size_ - offset));
    std::memset(static_cast<std::uint8_t*>(into) + there, 0, size - there);
    return there == 0 || Read(offset, into, there);
}

Status Sink::Open(const fs::path& file) {
    path_ = file;
    file_.open(file, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        return Status::Fail("Cannot write " + PathToUtf8(file));
    }
    pending_.reserve(WRITE_BUFFER + CHUNK);
    return {};
}

void Sink::Flush() {
    if (pending_.empty()) {
        return;
    }
    file_.seekp(static_cast<std::streamoff>(flushed_));
    file_.write(reinterpret_cast<const char*>(pending_.data()),
                static_cast<std::streamsize>(pending_.size()));
    flushed_ += pending_.size();
    pending_.clear();
}

void Sink::Write(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    pending_.insert(pending_.end(), p, p + size);
    if (pending_.size() >= WRITE_BUFFER) {
        Flush();
    }
}

void Sink::Fill(std::uint8_t value, std::uint64_t count) {
    std::uint8_t block[4096];
    std::memset(block, value, sizeof(block));
    while (count > 0) {
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(count, sizeof(block)));
        Write(block, take);
        count -= take;
    }
}

bool Sink::ReadBack(std::uint64_t offset, void* into, std::size_t size) {
    if (offset > Size() || size > Size() - offset) {
        return false;
    }
    auto* out = static_cast<std::uint8_t*>(into);
    if (offset < flushed_) {
        const std::size_t on_disk =
            static_cast<std::size_t>(std::min<std::uint64_t>(size, flushed_ - offset));
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(on_disk));
        if (static_cast<std::size_t>(file_.gcount()) != on_disk) {
            return false;
        }
        out += on_disk;
        offset += on_disk;
        size -= on_disk;
    }
    if (size > 0) {
        std::memcpy(out, pending_.data() + (offset - flushed_), size);
    }
    return true;
}

void Sink::WriteAt(std::uint64_t offset, const void* data, std::size_t size) {
    if (offset >= Size()) {
        Fill(0, offset - Size());
        Write(data, size);
        return;
    }
    Flush();
    file_.seekp(static_cast<std::streamoff>(offset));
    file_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    flushed_ = std::max<std::uint64_t>(flushed_, offset + size);
}

Status Sink::Close(std::optional<std::uint64_t> final_size) {
    Flush();
    file_.flush();
    const bool good = file_.good();
    const std::uint64_t size = flushed_;
    file_.close();
    if (!good) {
        return Status::Fail("Cannot write " + PathToUtf8(path_) + " (is the disk full?)");
    }
    if (final_size && *final_size != size) {
        std::error_code ec;
        fs::resize_file(path_, *final_size, ec);
        if (ec) {
            return Status::Fail("Cannot write " + PathToUtf8(path_));
        }
    }
    return {};
}

std::uint64_t ReadBeatNumber(Cursor& p) {
    std::uint64_t value = 0;
    std::uint64_t shift = 1;
    for (int i = 0; i < 9; ++i) {
        const std::uint8_t x = p.U8();
        if (p.bad) {
            return 0;
        }
        value += (x & 0x7F) * shift;
        if (x & 0x80) {
            return value;
        }
        shift <<= 7;
        value += shift;
    }
    p.bad = true;
    return 0;
}

bool Context::Progress(float fraction) {
    if (cancelled) {
        return false;
    }
    if (options.progress && (fraction - last_progress >= 0.01f || fraction >= 1.0f)) {
        last_progress = fraction;
        cancelled = !options.progress(std::clamp(fraction, 0.0f, 1.0f));
    }
    return !cancelled;
}

bool Context::CopySource(std::uint64_t offset, std::uint64_t count) {
    std::uint8_t block[CHUNK];
    while (count > 0) {
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(count, CHUNK));
        if (!source.ReadPadded(offset, block, take)) {
            return false;
        }
        out.Write(block, take);
        offset += take;
        count -= take;
    }
    return true;
}

Status Context::CopyWholeSource(float share) {
    const std::uint64_t size = source.Size();
    constexpr std::uint64_t STEP = 4u << 20;
    for (std::uint64_t at = 0; at < size; at += STEP) {
        if (!CopySource(at, std::min(STEP, size - at))) {
            return Status::Fail("Cannot read " + PathToUtf8(source_path));
        }
        if (!Progress(share * static_cast<float>(at) / static_cast<float>(size))) {
            return Cancelled();
        }
    }
    return {};
}

bool Context::CopyTarget(std::uint64_t offset, std::uint64_t count) {
    if (offset >= out.Size()) {
        return false;
    }
    std::uint8_t block[CHUNK];
    const std::uint64_t distance = out.Size() - offset;
    if (distance < count && distance <= CHUNK / 2) {
        // A run: the last `distance` bytes repeat.
        const std::size_t period = static_cast<std::size_t>(distance);
        if (!out.ReadBack(offset, block, period)) {
            return false;
        }
        std::size_t filled = period;
        while (filled + period <= CHUNK) {
            std::memcpy(block + filled, block, period);
            filled += period;
        }
        while (count > 0) {
            const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(count, filled));
            out.Write(block, take);
            count -= take;
        }
        return true;
    }
    while (count > 0) {
        const std::size_t take =
            static_cast<std::size_t>(std::min({count, distance, std::uint64_t{CHUNK}}));
        if (!out.ReadBack(offset, block, take)) {
            return false;
        }
        out.Write(block, take);
        offset += take;
        count -= take;
    }
    return true;
}

std::optional<std::uint32_t> Context::SourceCrc() const {
    return Crc32File(source_path);
}

Status Cancelled() {
    return Status::Fail("Cancelled");
}

Status Corrupt(std::string_view format) {
    return Status::Fail("This " + std::string(format) + " patch is damaged or incomplete");
}

Status WrongSource(std::optional<std::uint32_t> want_crc, std::optional<std::uint64_t> want_size,
                   std::optional<std::uint32_t> have_crc, std::uint64_t have_size) {
    std::string text = "This patch needs a ROM";
    if (want_crc) {
        text += " with CRC32 " + CrcText(*want_crc);
    }
    if (want_size) {
        text += std::string(want_crc ? " (" : " of ") + std::to_string(*want_size) + " bytes" +
                (want_crc ? ")" : "");
    }
    text += "; this file ";
    if (have_crc) {
        text += "has CRC32 " + CrcText(*have_crc) + " (" + std::to_string(have_size) + " bytes)";
    } else {
        text += "has " + std::to_string(have_size) + " bytes";
    }
    return Status::Fail(text);
}

} // namespace den::patching
