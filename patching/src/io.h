// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the formats share: the source read in pages, the result written through a buffer that
// can be read back and written into, a cursor over the patch, and the state of one Apply.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"
#include "den/patching/patch.h"

namespace den::patching {

/// The source ROM, read where the patch asks. A few pages are kept, never the file.
class Reader {
public:
    bool Open(const std::filesystem::path& file);
    std::uint64_t Size() const {
        return size_;
    }
    /// False when the range is not inside the file or cannot be read.
    bool Read(std::uint64_t offset, void* into, std::size_t size);
    /// Bytes past the end are zero.
    bool ReadPadded(std::uint64_t offset, void* into, std::size_t size);

private:
    struct Page {
        std::uint64_t index = 0;
        std::uint64_t used = 0;
        std::vector<std::uint8_t> data;
    };
    const Page* PageAt(std::uint64_t index);

    std::ifstream file_;
    std::uint64_t size_ = 0;
    std::uint64_t clock_ = 0;
    std::vector<Page> pages_;
};

/// The result, in a file. Appended to, read back (copies from what was written) and written
/// into (formats that change bytes of a copy).
class Sink {
public:
    Status Open(const std::filesystem::path& file);
    std::uint64_t Size() const {
        return flushed_ + pending_.size();
    }
    void Write(const void* data, std::size_t size);
    void Fill(std::uint8_t value, std::uint64_t count);
    bool ReadBack(std::uint64_t offset, void* into, std::size_t size);
    /// Writing past the end fills the gap with zeros.
    void WriteAt(std::uint64_t offset, const void* data, std::size_t size);
    /// Closes the file at `final_size` when given. Fails when any write failed.
    Status Close(std::optional<std::uint64_t> final_size = {});

private:
    void Flush();

    std::filesystem::path path_;
    std::fstream file_;
    std::uint64_t flushed_ = 0;
    std::vector<std::uint8_t> pending_;
};

/// Reads the patch. `bad` is set by a read past the end and stays.
struct Cursor {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t at = 0;
    bool bad = false;

    std::size_t Left() const {
        return size - at;
    }
    std::uint8_t U8() {
        if (at >= size) {
            bad = true;
            return 0;
        }
        return data[at++];
    }
    std::uint64_t Le(int bytes) {
        std::uint64_t value = 0;
        for (int i = 0; i < bytes; ++i) {
            value |= static_cast<std::uint64_t>(U8()) << (8 * i);
        }
        return value;
    }
    std::uint64_t Be(int bytes) {
        std::uint64_t value = 0;
        for (int i = 0; i < bytes; ++i) {
            value = (value << 8) | U8();
        }
        return value;
    }
    const std::uint8_t* Take(std::size_t count) {
        if (count > Left()) {
            bad = true;
            return nullptr;
        }
        const std::uint8_t* p = data + at;
        at += count;
        return p;
    }
};

/// The number encoding of UPS and BPS.
std::uint64_t ReadBeatNumber(Cursor& p);

struct Context {
    const std::vector<std::uint8_t>& patch;
    Reader& source;
    Sink& out;
    const ApplyOptions& options;
    std::filesystem::path source_path;
    /// Set by the format: the size the result is closed at, the CRC-32 it must have.
    std::optional<std::uint64_t> final_size;
    std::optional<std::uint32_t> target_crc;
    float last_progress = -1.0f;
    bool cancelled = false;

    /// False once the caller cancelled.
    bool Progress(float fraction);
    /// Appends source bytes to the result; bytes past the source's end are zero.
    bool CopySource(std::uint64_t offset, std::uint64_t count);
    /// Appends the whole source, reporting progress up to `share`.
    Status CopyWholeSource(float share);
    /// Appends bytes the result already has; the ranges may overlap (a repeating run).
    bool CopyTarget(std::uint64_t offset, std::uint64_t count);
    /// The CRC-32 of the source file.
    std::optional<std::uint32_t> SourceCrc() const;
};

Status Cancelled();
Status Corrupt(std::string_view format);
/// "This patch needs a ROM with CRC32 ... (n bytes); this file has ...".
Status WrongSource(std::optional<std::uint32_t> want_crc, std::optional<std::uint64_t> want_size,
                   std::optional<std::uint32_t> have_crc, std::uint64_t have_size);

// One per format. The Inspect functions read the header only.
Status ApplyIps(Context& c);
Status ApplyUps(Context& c);
Status ApplyBps(Context& c);
Status ApplyAps(Context& c);
Status ApplyPpf(Context& c);
Status ApplyVcdiff(Context& c);
Status InspectUps(Cursor p, PatchInfo& info);
Status InspectBps(Cursor p, PatchInfo& info);
Status InspectAps(Cursor p, PatchInfo& info);
Status InspectPpf(Cursor p, PatchInfo& info);
Status InspectVcdiff(Cursor p, PatchInfo& info);

} // namespace den::patching
