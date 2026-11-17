// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <mutex>

#include <xz.h>

#include "den/patching/xz.h"
#include "secondary.h"

namespace den::patching {

namespace {

constexpr std::uint32_t MAX_DICTIONARY = 64u << 20;
constexpr unsigned char MAGIC[6] = {0xfd, '7', 'z', 'X', 'Z', 0x00};

} // namespace

void PrepareXz() {
    static std::once_flag tables;
    std::call_once(tables, [] {
        xz_crc32_init();
        xz_crc64_init();
    });
}

bool IsXzFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    char head[sizeof(MAGIC)] = {};
    in.read(head, sizeof(head));
    return in.gcount() == static_cast<std::streamsize>(sizeof(head)) && std::memcmp(head, MAGIC, sizeof(MAGIC)) == 0;
}

XzFileReader::XzFileReader(const std::filesystem::path& file) : in_(file, std::ios::binary), buffer_(1u << 16) {
    PrepareXz();
    state_ = in_ ? xz_dec_init(XZ_DYNALLOC, MAX_DICTIONARY) : nullptr;
    failed_ = state_ == nullptr;
}

XzFileReader::~XzFileReader() {
    xz_dec_end(state_);
}

bool XzFileReader::Read(std::uint8_t* out, std::size_t size, std::size_t& got) {
    got = 0;
    if (failed_) {
        return false;
    }
    xz_buf buffer{};
    buffer.out = out;
    buffer.out_size = size;
    while (!ended_ && buffer.out_pos < size) {
        if (at_ == filled_) {
            in_.read(reinterpret_cast<char*>(buffer_.data()), static_cast<std::streamsize>(buffer_.size()));
            filled_ = static_cast<std::size_t>(in_.gcount());
            at_ = 0;
        }
        buffer.in = buffer_.data();
        buffer.in_pos = at_;
        buffer.in_size = filled_;
        const xz_ret result = xz_dec_run(state_, &buffer);
        at_ = buffer.in_pos;
        if (result == XZ_STREAM_END) {
            ended_ = true;
        } else if (result != XZ_OK && result != XZ_UNSUPPORTED_CHECK) {
            // XZ_BUF_ERROR: the file ends inside the stream.
            failed_ = true;
            return false;
        }
    }
    got = buffer.out_pos;
    return true;
}

} // namespace den::patching
