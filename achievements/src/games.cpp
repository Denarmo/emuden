// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Console ids, the User-Agent and hashing through rc_hash, with a file reader that opens
// UTF-8 paths on every platform.

#include <cstdio>
#include <fstream>
#include <map>
#include <memory>

#include "den/achievements/games.h"
#include "den/base/files.h"
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_hash.h"
#include "rc_version.h"

namespace den::achievements {

namespace {

void* RC_CCONV OpenFile(const char* path) {
    auto file = std::make_unique<std::ifstream>(PathFromUtf8(path), std::ios::binary);
    if (!file->is_open()) {
        return nullptr;
    }
    return file.release();
}

void RC_CCONV SeekFile(void* handle, int64_t offset, int origin) {
    auto* file = static_cast<std::ifstream*>(handle);
    file->clear();
    const std::ios::seekdir dir = origin == SEEK_SET ? std::ios::beg : origin == SEEK_CUR ? std::ios::cur : std::ios::end;
    file->seekg(offset, dir);
}

int64_t RC_CCONV TellFile(void* handle) {
    auto* file = static_cast<std::ifstream*>(handle);
    const auto at = file->tellg();
    return at < 0 ? 0 : static_cast<int64_t>(at);
}

size_t RC_CCONV ReadFile(void* handle, void* buffer, size_t bytes) {
    auto* file = static_cast<std::ifstream*>(handle);
    file->read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
    const auto got = file->gcount();
    if (file->eof()) {
        file->clear();
    }
    return got < 0 ? 0 : static_cast<size_t>(got);
}

void RC_CCONV CloseFile(void* handle) {
    delete static_cast<std::ifstream*>(handle);
}

Result<std::string> Hash(std::uint32_t console, const std::string& path, std::span<const std::uint8_t> data) {
    if (console == 0) {
        return Status::Fail("RetroAchievements has no achievements for this console.");
    }
    rc_hash_iterator_t iterator{};
    rc_hash_initialize_iterator(&iterator, path.c_str(), data.empty() ? nullptr : data.data(), data.size());
    iterator.callbacks.filereader = {OpenFile, SeekFile, TellFile, ReadFile, CloseFile};
    if (!iterator.callbacks.cdreader.open_track && !iterator.callbacks.cdreader.open_track_iterator) {
        rc_hash_get_default_cdreader(&iterator.callbacks.cdreader);
    }
    char hash[33] = {};
    const int made = rc_hash_generate(hash, console, &iterator);
    rc_hash_destroy_iterator(&iterator);
    if (!made) {
        return Status::Fail("The game could not be read to identify it.");
    }
    return std::string(hash);
}

} // namespace

std::string UserAgent(std::string_view client_name, std::string_view client_version) {
    return std::string(client_name) + "/" + std::string(client_version) + " rcheevos/" + RCHEEVOS_VERSION_STRING;
}

Result<std::string> HashFile(std::uint32_t console, const std::filesystem::path& file) {
    return Hash(console, PathToUtf8(file), {});
}

Result<std::string> HashBuffer(std::uint32_t console, std::span<const std::uint8_t> data) {
    return Hash(console, "game.bin", data);
}

} // namespace den::achievements
