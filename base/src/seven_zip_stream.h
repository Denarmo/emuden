// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <vector>

#include <7z.h>
#include <Lzma2Dec.h>
#include <LzmaDec.h>

namespace den {

/// A block's coders when it can be decoded front to back: copy, LZMA or LZMA2, alone or
/// behind one delta or branch (BCJ, ARM, ...) filter. `window` is the memory that takes.
struct SevenZipChain {
    bool streams = false;
    std::uint64_t window = 0;
};

/// `props` is the block's coder data (CodersData + FoCodersOffsets[folder]).
SevenZipChain ReadSevenZipChain(const CSzFolder& folder, const Byte* props, std::uint64_t unpack_size);

/// One solid block decoded in order through a window no larger than its dictionary.
class SevenZipStream {
public:
    using Sink = std::function<bool(const Byte*, std::size_t)>;

    SevenZipStream() = default;
    SevenZipStream(const SevenZipStream&) = delete;
    SevenZipStream& operator=(const SevenZipStream&) = delete;
    ~SevenZipStream();

    /// SZ_ERROR_UNSUPPORTED for a chain that does not stream, SZ_ERROR_MEM for a window over
    /// `max_window`.
    SRes Open(const std::filesystem::path& archive, const CSzArEx& db, UInt32 folder,
              std::uint64_t max_window);
    UInt32 Folder() const {
        return folder_;
    }
    /// Bytes of the block handed out so far.
    std::uint64_t Position() const {
        return position_;
    }
    /// Hands the next `size` bytes to `sink` in pieces of at most 1 MiB. A sink that returns
    /// false stops it with SZ_ERROR_PROGRESS.
    SRes Read(std::uint64_t size, const Sink& sink);

private:
    SRes Fill();
    SRes Decode(Byte* out, std::size_t want, std::size_t& produced);
    SRes Refill();
    void Filter();
    void Free();

    std::ifstream file_;
    UInt32 folder_ = 0xFFFFFFFF;
    std::uint32_t method_ = 0;
    std::uint32_t filter_ = 0;
    unsigned delta_ = 0;
    std::uint64_t pack_left_ = 0;
    std::uint64_t unpack_size_ = 0;
    std::uint64_t decoded_ = 0;
    std::uint64_t position_ = 0;
    CLzmaDec lzma_{};
    CLzma2Dec lzma2_{};
    bool probs_ = false;
    std::unique_ptr<Byte[]> window_;
    std::vector<Byte> in_;
    std::size_t in_pos_ = 0, in_end_ = 0;
    // Decoded bytes: [begin_, ready_) are filtered and not yet handed out, [ready_, end_)
    // wait for the filter's look-ahead.
    std::vector<Byte> out_;
    std::size_t begin_ = 0, ready_ = 0, end_ = 0;
    UInt32 pc_ = 0;
    UInt32 x86_state_ = 0;
    Byte delta_state_[256] = {};
};

} // namespace den
