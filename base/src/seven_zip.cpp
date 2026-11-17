// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 7z archives through the LZMA SDK's C decoder (public domain; cmake/deps.cmake). A small
// solid block is unpacked whole into memory and the file's part of it written out; a large
// one is decoded in pieces (seven_zip_stream.cpp) straight into the file.

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>

#include <7z.h>
#include <7zCrc.h>

#include "seven_zip.h"
#include "seven_zip_stream.h"

namespace den {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t LOOK_BUFFER = 1u << 16;
constexpr std::size_t WRITE_BLOCK = 1u << 20;
constexpr std::uint32_t NO_BLOCK = 0xFFFFFFFF;
constexpr std::uint32_t METHOD_AES = 0x06F10701;
// Copy, delta, LZMA2, ARM64, RISC-V, LZMA and the BCJ filters: what the SDK's decoder reads.
constexpr std::uint32_t METHODS[] = {0x0,       0x3,       0x21,      0xA,       0xB,       0x030101,
                                     0x3030103, 0x303011B, 0x3030205, 0x3030401, 0x3030501, 0x3030701,
                                     0x3030805};

void* Allocate(ISzAllocPtr, std::size_t size) {
    return size == 0 || size > SevenZipLimits{}.max_memory_block ? nullptr : std::malloc(size);
}

void Release(ISzAllocPtr, void* address) {
    std::free(address);
}

const ISzAlloc ALLOC = {Allocate, Release};

using Sink = SevenZipStream::Sink;

// The SDK's seekable input over a file. `vt` is first, so the SDK's pointer is this object.
struct FileIn {
    ISeekInStream vt;
    std::ifstream* file;
};

SRes ReadFile(ISeekInStreamPtr p, void* buffer, std::size_t* size) {
    std::ifstream& file = *reinterpret_cast<const FileIn*>(p)->file;
    file.clear();
    file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(*size));
    *size = static_cast<std::size_t>(file.gcount());
    return file.bad() ? SZ_ERROR_READ : SZ_OK;
}

SRes SeekFile(ISeekInStreamPtr p, Int64* position, ESzSeek origin) {
    std::ifstream& file = *reinterpret_cast<const FileIn*>(p)->file;
    file.clear();
    const std::ios::seekdir from =
        origin == SZ_SEEK_SET ? std::ios::beg : origin == SZ_SEEK_CUR ? std::ios::cur : std::ios::end;
    file.seekg(static_cast<std::streamoff>(*position), from);
    const std::streamoff now = file.tellg();
    if (!file || now < 0) {
        return SZ_ERROR_READ;
    }
    *position = now;
    return SZ_OK;
}

void AppendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// Forward slashes, UTF-8. A lone surrogate becomes U+FFFD.
std::string NameFromUtf16(const std::vector<UInt16>& name) {
    std::string out;
    for (std::size_t i = 0; i < name.size() && name[i] != 0; ++i) {
        std::uint32_t code = name[i];
        if (code >= 0xD800 && code < 0xDC00 && i + 1 < name.size() && name[i + 1] >= 0xDC00 &&
            name[i + 1] < 0xE000) {
            code = 0x10000 + ((code - 0xD800) << 10) + (name[++i] - 0xDC00);
        } else if (code >= 0xD800 && code < 0xE000) {
            code = 0xFFFD;
        }
        AppendUtf8(out, code == '\\' ? '/' : code);
    }
    return out;
}

class SevenZip final : public ArchiveReader {
public:
    SevenZip(const fs::path& archive, const SevenZipLimits& limits)
        : archive_(archive), limits_(limits), file_(archive, std::ios::binary) {
        static std::once_flag crc_table;
        std::call_once(crc_table, [] { CrcGenerateTable(); });
        in_.vt.Read = ReadFile;
        in_.vt.Seek = SeekFile;
        in_.file = &file_;
        LookToRead2_CreateVTable(&look_, 0);
        look_.buf = static_cast<Byte*>(std::malloc(LOOK_BUFFER));
        look_.bufSize = LOOK_BUFFER;
        look_.realStream = &in_.vt;
        LookToRead2_INIT(&look_)
        SzArEx_Init(&db_);
    }

    ~SevenZip() override {
        ISzAlloc_Free(&ALLOC, block_);
        SzArEx_Free(&db_, &ALLOC);
        std::free(look_.buf);
    }

    bool Load() {
        if (!file_ || look_.buf == nullptr || SzArEx_Open(&db_, &look_.vt, &ALLOC, &ALLOC) != SZ_OK) {
            return false;
        }
        std::vector<UInt16> name;
        for (UInt32 i = 0; i < db_.NumFiles; ++i) {
            if (SzArEx_IsDir(&db_, i)) {
                continue;
            }
            name.assign(SzArEx_GetFileNameUtf16(&db_, i, nullptr), 0);
            SzArEx_GetFileNameUtf16(&db_, i, name.data());
            ZipEntry entry;
            entry.name = NameFromUtf16(name);
            entry.index = static_cast<std::int32_t>(i);
            entry.size = SzArEx_GetFileSize(&db_, i);
            entry.crc_known = SzBitWithVals_Check(&db_.CRCs, i);
            entry.crc = entry.crc_known ? db_.CRCs.Vals[i] : 0;
            // The high half of the attributes is the Unix mode when bit 15 says so.
            if (SzBitWithVals_Check(&db_.Attribs, i) && (db_.Attribs.Vals[i] & 0x8000) != 0) {
                entry.symlink = ((db_.Attribs.Vals[i] >> 16) & 0xF000) == 0xA000;
            }
            const UInt32 folder = db_.FileToFolder[i];
            if (folder != NO_BLOCK) {
                entry.block_size = SzAr_GetFolderUnpackSize(&db_.db, folder);
                // The whole block's packed size: a file has none of its own.
                const UInt32* first = db_.db.FoStartPackStreamIndex + folder;
                entry.compressed_size = db_.db.PackPositions[first[1]] - db_.db.PackPositions[first[0]];
                Methods(folder, entry);
            }
            entries_.push_back(std::move(entry));
        }
        return true;
    }

    const std::vector<ZipEntry>& Entries() const override {
        return entries_;
    }

    Status Extract(const ZipEntry& entry, const fs::path& destination,
                   const std::function<bool()>& cancelled) override {
        const std::string shown = PathToUtf8(archive_.filename());
        if (entry.index < 0 || static_cast<UInt32>(entry.index) >= db_.NumFiles ||
            SzArEx_IsDir(&db_, entry.index) || SzArEx_GetFileSize(&db_, entry.index) != entry.size) {
            return Status::Fail(entry.name + " is not a file of " + shown);
        }
        if (const std::string refusal = SevenZipRefusal(entry, shown, limits_); !refusal.empty()) {
            return Status::Fail(refusal);
        }
        if (cancelled && cancelled()) {
            return Status::Fail("cancelled");
        }
        const UInt32 folder = db_.FileToFolder[entry.index];
        // An empty file is part of no block.
        if (folder == NO_BLOCK) {
            return WritePart(destination, cancelled, [](const Sink&) { return std::string(); });
        }
        if (SevenZipStreams(entry, limits_)) {
            return Stream(entry, folder, destination, cancelled);
        }
        std::size_t offset = 0;
        std::size_t size = 0;
        const SRes code = SzArEx_Extract(&db_, &look_.vt, static_cast<UInt32>(entry.index), &block_index_,
                                         &block_, &block_size_, &offset, &size, &ALLOC, &ALLOC);
        if (code != SZ_OK) {
            // The SDK leaves a block it could not finish; it is not a cache.
            ISzAlloc_Free(&ALLOC, block_);
            block_ = nullptr;
            block_index_ = NO_BLOCK;
            block_size_ = 0;
            return Status::Fail(Failure(code, entry));
        }
        const Byte* data = block_ + offset;
        return WritePart(destination, cancelled, [&](const Sink& sink) {
            for (std::size_t done = 0; done < size;) {
                const std::size_t part = std::min(WRITE_BLOCK, size - done);
                if (!sink(data + done, part)) {
                    break;
                }
                done += part;
            }
            return std::string();
        });
    }

private:
    // Marks a file whose block uses a password or a method the SDK's decoder does not read.
    void Methods(UInt32 folder, ZipEntry& entry) const {
        CSzData data;
        const Byte* const props = db_.db.CodersData + db_.db.FoCodersOffsets[folder];
        data.Data = props;
        data.Size = db_.db.FoCodersOffsets[folder + 1] - db_.db.FoCodersOffsets[folder];
        CSzFolder coders;
        if (SzGetNextFolderItem(&coders, &data) != SZ_OK) {
            entry.unsupported = true;
            return;
        }
        const SevenZipChain chain = ReadSevenZipChain(coders, props, entry.block_size);
        entry.streams = chain.streams;
        entry.window = chain.window;
        for (UInt32 i = 0; i < coders.NumCoders; ++i) {
            const std::uint32_t method = coders.Coders[i].MethodID;
            entry.encrypted = entry.encrypted || method == METHOD_AES;
            bool known = false;
            for (const std::uint32_t id : METHODS) {
                known = known || id == method;
            }
            entry.unsupported = entry.unsupported || !known;
        }
    }

    std::string Failure(SRes code, const ZipEntry& entry) const {
        switch (code) {
        case SZ_ERROR_MEM: return "not enough memory to unpack " + PathToUtf8(archive_.filename());
        case SZ_ERROR_UNSUPPORTED: return entry.name + " is packed in a way this build cannot read";
        case SZ_ERROR_READ: return "cannot read " + PathToUtf8(archive_);
        default: return PathToUtf8(archive_) + " is damaged (" + entry.name + ")";
        }
    }

    // Decodes the block from its start, or on from the file written from it last.
    Status Stream(const ZipEntry& entry, UInt32 folder, const fs::path& destination,
                  const std::function<bool()>& cancelled) {
        const std::uint64_t offset =
            db_.UnpackPositions[entry.index] - db_.UnpackPositions[db_.FolderToFile[folder]];
        if (!stream_ || stream_->Folder() != folder || stream_->Position() > offset) {
            stream_ = std::make_unique<SevenZipStream>();
            if (const SRes code = stream_->Open(archive_, db_, folder, limits_.max_window); code != SZ_OK) {
                stream_.reset();
                return Status::Fail(Failure(code, entry));
            }
        }
        // The files before this one in the block are decoded and dropped.
        bool stopped = false;
        SRes code = stream_->Read(offset - stream_->Position(), [&](const Byte*, std::size_t) {
            stopped = cancelled && cancelled();
            return !stopped;
        });
        if (code != SZ_OK) {
            stream_.reset();
            return Status::Fail(stopped ? "cancelled" : Failure(code, entry));
        }
        // A block of one file may carry the block's checksum instead of the file's.
        const bool whole = entry.size == entry.block_size && SzBitWithVals_Check(&db_.db.FolderCRCs, folder);
        UInt32 crc = CRC_INIT_VAL;
        const Status written = WritePart(destination, cancelled, [&](const Sink& sink) {
            code = stream_->Read(entry.size, [&](const Byte* data, std::size_t size) {
                crc = CrcUpdate(crc, data, size);
                return sink(data, size);
            });
            if (code == SZ_ERROR_PROGRESS) {
                return std::string(); // the sink said why
            }
            if (code != SZ_OK) {
                return Failure(code, entry);
            }
            const UInt32 expected = entry.crc_known ? entry.crc : whole ? db_.db.FolderCRCs.Vals[folder] : 0;
            if ((entry.crc_known || whole) && CRC_GET_DIGEST(crc) != expected) {
                return PathToUtf8(archive_) + " is damaged (" + entry.name + ")";
            }
            return std::string();
        });
        if (!written.ok) {
            stream_.reset();
        }
        return written;
    }

    // Writes what `fill` hands its sink to "<destination>.part", renamed once complete.
    // `fill` returns an error, or nothing when done or when the sink refused (it says why).
    static Status WritePart(const fs::path& destination, const std::function<bool()>& cancelled,
                            const std::function<std::string(const Sink&)>& fill) {
        fs::path partial = destination;
        partial += ".part";
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        std::string error = out ? std::string() : "cannot write " + PathToUtf8(destination);
        if (error.empty()) {
            const Sink sink = [&](const Byte* data, std::size_t size) {
                if (cancelled && cancelled()) {
                    error = "cancelled";
                    return false;
                }
                out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
                if (!out) {
                    error = "cannot write " + PathToUtf8(destination) + " (is the disk full?)";
                    return false;
                }
                return true;
            };
            if (std::string failed = fill(sink); error.empty()) {
                error = std::move(failed);
            }
        }
        out.close();
        std::error_code ec;
        if (error.empty() && !out) {
            error = "cannot write " + PathToUtf8(destination);
        }
        if (error.empty()) {
            fs::rename(partial, destination, ec);
            if (ec) {
                error = "cannot write " + PathToUtf8(destination);
            }
        }
        if (!error.empty()) {
            fs::remove(partial, ec);
            return Status::Fail(std::move(error));
        }
        return {};
    }

    fs::path archive_;
    SevenZipLimits limits_;
    std::ifstream file_;
    FileIn in_{};
    CLookToRead2 look_{};
    CSzArEx db_{};
    std::vector<ZipEntry> entries_;
    // The SDK's cache: the solid block unpacked last.
    UInt32 block_index_ = NO_BLOCK;
    Byte* block_ = nullptr;
    std::size_t block_size_ = 0;
    // A large block being decoded, positioned after the file written from it last.
    std::unique_ptr<SevenZipStream> stream_;
};

} // namespace

std::string SevenZipRefusal(const ZipEntry& entry, const std::string& archive_name,
                            const SevenZipLimits& limits) {
    if (entry.encrypted) {
        return archive_name + " needs a password, which EmuDEN does not ask for";
    }
    if (entry.symlink) {
        return entry.name + " is a symbolic link, which EmuDEN does not unpack";
    }
    if (entry.unsupported) {
        return entry.name + " is packed in a way this build cannot read";
    }
    if (SevenZipStreams(entry, limits) || entry.block_size <= limits.max_memory_block) {
        return {};
    }
    if (entry.streams) {
        return archive_name + " was packed with a dictionary of " + std::to_string(entry.window >> 20) +
               " MiB, more than EmuDEN unpacks with (" + std::to_string(limits.max_window >> 20) + " MiB)";
    }
    return archive_name + " packs its files as one block of " + std::to_string(entry.block_size >> 20) +
           " MiB with BCJ2 or several filters, more than EmuDEN unpacks in memory (" +
           std::to_string(limits.max_memory_block >> 20) + " MiB)";
}

std::unique_ptr<ArchiveReader> OpenSevenZip(const fs::path& archive, const SevenZipLimits& limits) {
    auto reader = std::make_unique<SevenZip>(archive, limits);
    if (!reader->Load()) {
        return nullptr;
    }
    return reader;
}

} // namespace den
