// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Battery saves and save states of the running game.

#include <algorithm>
#include <cstring>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/device.h"
#include "session_impl.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

std::string_view Bytes(std::span<const std::uint8_t> data) {
    return std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace

void PlayerSession::LoadBatterySave() {
    const std::span<std::uint8_t> ram = core_->SaveRam();
    if (!ram.empty()) {
        if (const auto data = ReadBinaryFile(paths_.BatterySave())) {
            std::memcpy(ram.data(), data->data(), std::min(ram.size(), data->size()));
        }
    }
    saved_ram_.assign(ram.begin(), ram.end());
}

void PlayerSession::FlushSaves() {
    if (!core_) {
        return;
    }
    const std::span<std::uint8_t> ram = core_->SaveRam();
    if (ram.empty() || (ram.size() == saved_ram_.size() &&
                        std::equal(ram.begin(), ram.end(), saved_ram_.begin()))) {
        return;
    }
    if (!WriteFileAtomic(paths_.BatterySave(), Bytes(ram))) {
        log::Error("player", "the save could not be written: {}",
                   PathToUtf8(paths_.BatterySave()));
        return;
    }
    saved_ram_.assign(ram.begin(), ram.end());
}

Status PlayerSession::WriteStateFile(const fs::path& file) {
    if (!core_) {
        return Status::Fail("No game is running.");
    }
    const std::size_t size = core_->StateSize();
    std::vector<std::uint8_t> state(size);
    if (size == 0 || !core_->SaveState(state)) {
        return Status::Fail("This game cannot save a state right now.");
    }
    const std::vector<std::uint8_t> wrapped = WrapState(core_->Info().name, state);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (!WriteFileAtomic(file, Bytes(wrapped))) {
        return Status::Fail("The save state could not be written.");
    }
    return {};
}

Status PlayerSession::ReadStateFile(const fs::path& file) {
    if (!core_) {
        return Status::Fail("No game is running.");
    }
    const auto data = ReadBinaryFile(file);
    if (!data) {
        return Status::Fail("There is no save state in this slot.");
    }
    const auto unwrapped = UnwrapState(core_->Info().name, *data);
    if (!unwrapped) {
        return unwrapped.status();
    }
    // Cores read as many bytes as their state has now: a state from another version of the
    // core may be shorter.
    std::span<const std::uint8_t> payload = unwrapped.value();
    const std::size_t size = core_->StateSize();
    if (payload.size() < size) {
        state_scratch_.assign(size, 0);
        std::memcpy(state_scratch_.data(), payload.data(), payload.size());
        payload = state_scratch_;
    }
    if (!core_->LoadState(payload)) {
        return Status::Fail("The save state does not fit this version of the core.");
    }
    if (rewind_) {
        rewind_->Clear();
    }
    audio_.Clear();
    return {};
}

void PlayerSession::WriteThumbnail(const fs::path& file) {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    if (core_->ReadPicture(&width, &height, &rgba) && width > 0 && height > 0) {
        gfx::SavePng(file, static_cast<int>(width), static_cast<int>(height), rgba.data());
    }
}

Status PlayerSession::SaveState(int slot) {
    if (slot < 0 || slot >= STATE_SLOTS) {
        return Status::Fail("There is no such slot.");
    }
    const Status status = WriteStateFile(paths_.State(slot));
    if (status) {
        WriteThumbnail(paths_.Thumbnail(slot));
    }
    return status;
}

Status PlayerSession::LoadState(int slot) {
    if (slot < 0 || slot >= STATE_SLOTS) {
        return Status::Fail("There is no such slot.");
    }
    if (hardcore_) {
        return Status::Fail("Hardcore mode is on: states cannot be loaded.");
    }
    pending_resume_ = false;
    const Status loaded = ReadStateFile(paths_.State(slot));
    if (loaded) {
        Notify(FrameEvent::StateLoaded);
    }
    return loaded;
}

bool PlayerSession::HasState(int slot) const {
    return core_ && slot >= 0 && slot < STATE_SLOTS && IsRegularFile(paths_.State(slot));
}

fs::path PlayerSession::StateThumbnail(int slot) const {
    if (!HasState(slot) || !IsRegularFile(paths_.Thumbnail(slot))) {
        return {};
    }
    return paths_.Thumbnail(slot);
}

} // namespace den::player
