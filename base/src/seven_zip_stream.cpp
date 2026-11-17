// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A 7z block too large to hold in memory, decoded in pieces: the LZMA decoder writes into a
// circular window the size of the dictionary, and a filter after it runs on each piece.

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <Bra.h>
#include <CpuArch.h>
#include <Delta.h>

#include "seven_zip_stream.h"

namespace den {

namespace {

constexpr std::uint32_t COPY = 0x0, LZMA2 = 0x21, LZMA = 0x030101;
constexpr std::uint32_t DELTA = 0x3, ARM64 = 0xA, RISCV = 0xB, BCJ = 0x3030103, PPC = 0x3030205,
                        IA64 = 0x3030401, ARM = 0x3030501, ARMT = 0x3030701, SPARC = 0x3030805;
constexpr std::size_t IN_SIZE = 1u << 18;
constexpr std::size_t PIECE = 1u << 20;
// More than any filter's alignment plus look-ahead.
constexpr std::size_t FILTER_SLACK = 32;
constexpr std::uint64_t MIN_WINDOW = 1u << 12;

void* Allocate(ISzAllocPtr, std::size_t size) {
    return std::malloc(size);
}

void Release(ISzAllocPtr, void* address) {
    std::free(address);
}

const ISzAlloc ALLOC = {Allocate, Release};

bool IsFilter(std::uint32_t method) {
    for (const std::uint32_t id : {DELTA, ARM64, RISCV, BCJ, PPC, IA64, ARM, ARMT, SPARC}) {
        if (id == method) {
            return true;
        }
    }
    return false;
}

// As the SDK's decoder checks a filter's properties; the start address where one has it.
bool FilterProps(const CSzCoderInfo& coder, const Byte* props, UInt32& pc, unsigned& delta) {
    pc = 0;
    if (coder.MethodID == DELTA) {
        delta = coder.PropsSize == 1 ? props[coder.PropsOffset] + 1u : 0;
        return coder.PropsSize == 1;
    }
    if (coder.MethodID == ARM64 || coder.MethodID == RISCV) {
        if (coder.PropsSize == 4) {
            pc = GetUi32(props + coder.PropsOffset);
            return (pc & (coder.MethodID == ARM64 ? 3u : 1u)) == 0;
        }
    }
    return coder.PropsSize == 0;
}

z7_Func_BranchConv BranchDecoder(std::uint32_t method) {
    switch (method) {
    case ARM64: return Z7_BRANCH_CONV_DEC(ARM64);
    case RISCV: return Z7_BRANCH_CONV_DEC(RISCV);
    case PPC: return Z7_BRANCH_CONV_DEC(PPC);
    case IA64: return Z7_BRANCH_CONV_DEC(IA64);
    case ARM: return Z7_BRANCH_CONV_DEC(ARM);
    case ARMT: return Z7_BRANCH_CONV_DEC(ARMT);
    case SPARC: return Z7_BRANCH_CONV_DEC(SPARC);
    default: return nullptr;
    }
}

} // namespace

SevenZipChain ReadSevenZipChain(const CSzFolder& folder, const Byte* props, std::uint64_t unpack_size) {
    SevenZipChain chain;
    const CSzCoderInfo& main = folder.Coders[0];
    if (folder.NumCoders < 1 || folder.NumCoders > 2 || folder.NumPackStreams != 1 ||
        folder.PackStreams[0] != 0 || main.NumStreams != 1) {
        return chain;
    }
    if (folder.NumCoders == 2) {
        const CSzCoderInfo& filter = folder.Coders[1];
        UInt32 pc = 0;
        unsigned delta = 0;
        if (filter.NumStreams != 1 || folder.NumBonds != 1 || folder.Bonds[0].InIndex != 1 ||
            folder.Bonds[0].OutIndex != 0 || !IsFilter(filter.MethodID) ||
            !FilterProps(filter, props, pc, delta)) {
            return chain;
        }
    } else if (folder.NumBonds != 0) {
        return chain;
    }
    std::uint64_t dictionary = 0;
    if (main.MethodID == LZMA) {
        if (main.PropsSize != LZMA_PROPS_SIZE) {
            return chain;
        }
        dictionary = GetUi32(props + main.PropsOffset + 1);
    } else if (main.MethodID == LZMA2) {
        const unsigned bits = main.PropsSize == 1 ? props[main.PropsOffset] : 41;
        if (bits > 40) {
            return chain;
        }
        dictionary = bits == 40 ? 0xFFFFFFFFull : (2ull | (bits & 1)) << (bits / 2 + 11);
    } else if (main.MethodID != COPY) {
        return chain;
    }
    chain.streams = true;
    if (main.MethodID != COPY) {
        chain.window = std::max(std::min(dictionary, unpack_size), MIN_WINDOW);
    }
    return chain;
}

SevenZipStream::~SevenZipStream() {
    Free();
}

void SevenZipStream::Free() {
    if (probs_) {
        if (method_ == LZMA2) {
            Lzma2Dec_FreeProbs(&lzma2_, &ALLOC);
        } else {
            LzmaDec_FreeProbs(&lzma_, &ALLOC);
        }
        probs_ = false;
    }
}

SRes SevenZipStream::Open(const std::filesystem::path& archive, const CSzArEx& db, UInt32 folder,
                          std::uint64_t max_window) {
    CSzData data;
    const Byte* props = db.db.CodersData + db.db.FoCodersOffsets[folder];
    data.Data = props;
    data.Size = db.db.FoCodersOffsets[folder + 1] - db.db.FoCodersOffsets[folder];
    CSzFolder coders;
    if (SzGetNextFolderItem(&coders, &data) != SZ_OK) {
        return SZ_ERROR_UNSUPPORTED;
    }
    unpack_size_ = SzAr_GetFolderUnpackSize(&db.db, folder);
    const SevenZipChain chain = ReadSevenZipChain(coders, props, unpack_size_);
    if (!chain.streams) {
        return SZ_ERROR_UNSUPPORTED;
    }
    if (chain.window > max_window) {
        return SZ_ERROR_MEM;
    }
    folder_ = folder;
    method_ = coders.Coders[0].MethodID;
    filter_ = coders.NumCoders == 2 ? coders.Coders[1].MethodID : COPY;
    if (filter_ != COPY) {
        FilterProps(coders.Coders[1], props, pc_, delta_);
    }
    x86_state_ = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
    Delta_Init(delta_state_);

    const UInt32* pack = db.db.FoStartPackStreamIndex + folder;
    pack_left_ = db.db.PackPositions[pack[1]] - db.db.PackPositions[pack[0]];
    file_.open(archive, std::ios::binary);
    file_.seekg(static_cast<std::streamoff>(db.dataPos + db.db.PackPositions[pack[0]]));
    if (!file_) {
        return SZ_ERROR_READ;
    }
    in_.resize(IN_SIZE);
    out_.resize(PIECE + FILTER_SLACK);
    if (method_ == COPY) {
        return SZ_OK;
    }
    window_.reset(new (std::nothrow) Byte[chain.window]);
    if (!window_) {
        return SZ_ERROR_MEM;
    }
    const Byte* coder_props = props + coders.Coders[0].PropsOffset;
    CLzmaDec* decoder = &lzma_;
    if (method_ == LZMA2) {
        Lzma2Dec_CONSTRUCT(&lzma2_)
        RINOK(Lzma2Dec_AllocateProbs(&lzma2_, coder_props[0], &ALLOC))
        probs_ = true;
        Lzma2Dec_Init(&lzma2_);
        decoder = &lzma2_.decoder;
    } else {
        LzmaDec_CONSTRUCT(&lzma_)
        RINOK(LzmaDec_AllocateProbs(&lzma_, coder_props, LZMA_PROPS_SIZE, &ALLOC))
        probs_ = true;
        LzmaDec_Init(&lzma_);
    }
    decoder->dic = window_.get();
    decoder->dicBufSize = static_cast<SizeT>(chain.window);
    return SZ_OK;
}

SRes SevenZipStream::Refill() {
    const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(IN_SIZE, pack_left_));
    file_.read(reinterpret_cast<char*>(in_.data()), static_cast<std::streamsize>(want));
    const std::size_t got = static_cast<std::size_t>(file_.gcount());
    if (file_.bad()) {
        return SZ_ERROR_READ;
    }
    if (got == 0) {
        return SZ_ERROR_INPUT_EOF;
    }
    pack_left_ -= got;
    in_pos_ = 0;
    in_end_ = got;
    return SZ_OK;
}

SRes SevenZipStream::Decode(Byte* out, std::size_t want, std::size_t& produced) {
    produced = 0;
    while (produced < want) {
        if (in_pos_ == in_end_ && pack_left_ > 0) {
            RINOK(Refill())
        }
        if (method_ == COPY) {
            const std::size_t take = std::min(want - produced, in_end_ - in_pos_);
            if (take == 0) {
                return SZ_ERROR_INPUT_EOF;
            }
            std::memcpy(out + produced, in_.data() + in_pos_, take);
            in_pos_ += take;
            produced += take;
            continue;
        }
        CLzmaDec& decoder = method_ == LZMA2 ? lzma2_.decoder : lzma_;
        if (decoder.dicPos == decoder.dicBufSize) {
            decoder.dicPos = 0; // the window wraps; what it held has been handed out
        }
        const SizeT start = decoder.dicPos;
        const SizeT limit = start + std::min<SizeT>(want - produced, decoder.dicBufSize - start);
        SizeT used = in_end_ - in_pos_;
        ELzmaStatus status;
        const SRes code =
            method_ == LZMA2
                ? Lzma2Dec_DecodeToDic(&lzma2_, limit, in_.data() + in_pos_, &used, LZMA_FINISH_ANY, &status)
                : LzmaDec_DecodeToDic(&lzma_, limit, in_.data() + in_pos_, &used, LZMA_FINISH_ANY, &status);
        in_pos_ += used;
        RINOK(code)
        const SizeT got = decoder.dicPos - start;
        std::memcpy(out + produced, decoder.dic + start, got);
        produced += got;
        if (got == 0 && used == 0 && (in_pos_ < in_end_ || pack_left_ == 0)) {
            // The stream ended before the block did.
            return status == LZMA_STATUS_NEEDS_MORE_INPUT ? SZ_ERROR_INPUT_EOF : SZ_ERROR_DATA;
        }
    }
    return SZ_OK;
}

void SevenZipStream::Filter() {
    Byte* data = out_.data() + ready_;
    const SizeT size = end_ - ready_;
    if (filter_ == DELTA) {
        Delta_Decode(delta_state_, delta_, data, size);
    } else if (filter_ != COPY) {
        Byte* const done = filter_ == BCJ ? z7_BranchConvSt_X86_Dec(data, size, pc_, &x86_state_)
                                          : BranchDecoder(filter_)(data, size, pc_);
        pc_ += static_cast<UInt32>(done - data);
        ready_ += static_cast<std::size_t>(done - data);
    }
    if (filter_ == COPY || filter_ == DELTA || decoded_ == unpack_size_) {
        ready_ = end_; // the last few bytes stay as they are, as when the block is filtered whole
    }
}

SRes SevenZipStream::Fill() {
    // Only called once everything filtered is handed out: what is left waits for the filter.
    std::memmove(out_.data(), out_.data() + ready_, end_ - ready_);
    end_ -= ready_;
    begin_ = ready_ = 0;
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(PIECE, unpack_size_ - decoded_));
    std::size_t produced = 0;
    RINOK(Decode(out_.data() + end_, want, produced))
    end_ += produced;
    decoded_ += produced;
    Filter();
    return SZ_OK;
}

SRes SevenZipStream::Read(std::uint64_t size, const Sink& sink) {
    while (size > 0) {
        if (begin_ == ready_) {
            if (decoded_ == unpack_size_ && ready_ == end_) {
                return SZ_ERROR_DATA; // more asked for than the block holds
            }
            RINOK(Fill())
            continue;
        }
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(size, ready_ - begin_));
        if (sink && !sink(out_.data() + begin_, take)) {
            return SZ_ERROR_PROGRESS;
        }
        begin_ += take;
        position_ += take;
        size -= take;
    }
    return SZ_OK;
}

} // namespace den
