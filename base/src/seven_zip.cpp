// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 7z archives through the LZMA SDK's C decoder (public domain; cmake/deps.cmake). The SDK
// unpacks a whole solid block into memory and hands out the file's part of it.

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>

#include <7z.h>
#include <7zCrc.h>

#include "seven_zip.h"

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
    return size == 0 || size > SEVEN_ZIP_MAX_BLOCK ? nullptr : std::malloc(size);
}

void Release(ISzAllocPtr, void* address) {
    std::free(address);
}

const ISzAlloc ALLOC = {Allocate, Release};

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
    explicit SevenZip(const fs::path& archive) : archive_(archive), file_(archive, std::ios::binary) {
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
        if (!CanExtractArchiveEntry(entry)) {
            return Status::Fail(Refusal(entry, shown));
        }
        if (cancelled && cancelled()) {
            return Status::Fail("cancelled");
        }
        std::size_t offset = 0;
        std::size_t size = 0;
        // An empty file is part of no block.
        if (db_.FileToFolder[entry.index] != NO_BLOCK) {
            const SRes code =
                SzArEx_Extract(&db_, &look_.vt, static_cast<UInt32>(entry.index), &block_index_, &block_,
                               &block_size_, &offset, &size, &ALLOC, &ALLOC);
            if (code != SZ_OK) {
                // The SDK leaves a block it could not finish; it is not a cache.
                ISzAlloc_Free(&ALLOC, block_);
                block_ = nullptr;
                block_index_ = NO_BLOCK;
                block_size_ = 0;
                return Status::Fail(code == SZ_ERROR_MEM           ? "not enough memory to unpack " + shown
                                    : code == SZ_ERROR_UNSUPPORTED ? Refusal(entry, shown)
                                                                   : PathToUtf8(archive_) + " is damaged (" +
                                                                         entry.name + ")");
            }
        }
        return Write(block_ + offset, size, destination, cancelled);
    }

private:
    // Marks a file whose block uses a password or a method the SDK's decoder does not read.
    void Methods(UInt32 folder, ZipEntry& entry) const {
        CSzData data;
        data.Data = db_.db.CodersData + db_.db.FoCodersOffsets[folder];
        data.Size = db_.db.FoCodersOffsets[folder + 1] - db_.db.FoCodersOffsets[folder];
        CSzFolder coders;
        if (SzGetNextFolderItem(&coders, &data) != SZ_OK) {
            entry.unsupported = true;
            return;
        }
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

    static std::string Refusal(const ZipEntry& entry, const std::string& shown) {
        if (entry.encrypted) {
            return shown + " needs a password, which EmuDEN does not ask for";
        }
        if (entry.block_size > SEVEN_ZIP_MAX_BLOCK) {
            return shown + " packs its files as one block of " + std::to_string(entry.block_size >> 20) +
                   " MiB, more than EmuDEN unpacks from a 7z (1024 MiB)";
        }
        return entry.name + " is packed in a way this build cannot read";
    }

    static Status Write(const Byte* data, std::size_t size, const fs::path& destination,
                        const std::function<bool()>& cancelled) {
        fs::path partial = destination;
        partial += ".part";
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        std::string error = out ? std::string() : "cannot write " + PathToUtf8(destination);
        for (std::size_t done = 0; done < size && error.empty();) {
            if (cancelled && cancelled()) {
                error = "cancelled";
                break;
            }
            const std::size_t part = std::min(WRITE_BLOCK, size - done);
            out.write(reinterpret_cast<const char*>(data + done), static_cast<std::streamsize>(part));
            done += part;
            if (!out) {
                error = "cannot write " + PathToUtf8(destination) + " (is the disk full?)";
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
    std::ifstream file_;
    FileIn in_{};
    CLookToRead2 look_{};
    CSzArEx db_{};
    std::vector<ZipEntry> entries_;
    // The SDK's cache: the solid block unpacked last.
    UInt32 block_index_ = NO_BLOCK;
    Byte* block_ = nullptr;
    std::size_t block_size_ = 0;
};

} // namespace

std::unique_ptr<ArchiveReader> OpenSevenZip(const fs::path& archive) {
    auto reader = std::make_unique<SevenZip>(archive);
    if (!reader->Load()) {
        return nullptr;
    }
    return reader;
}

} // namespace den
