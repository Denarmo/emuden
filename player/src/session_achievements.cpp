// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What an achievements runtime needs of the running game: its memory, a hook after every
// frame, hardcore, and a screenshot.

#include "den/gfx/device.h"
#include "session_impl.h"

namespace den::player {

const retro_memory_map* PlayerSession::MemoryMap() const {
    return core_ ? core_->MemoryMap() : nullptr;
}

std::span<std::uint8_t> PlayerSession::MemoryRegion(unsigned id) const {
    return core_ ? core_->Memory(id) : std::span<std::uint8_t>();
}

void PlayerSession::SetHardcore(bool on) {
    const bool changed = hardcore_ != on;
    hardcore_ = on;
    snapshot_.hardcore = on;
    snapshot_.can_rewind = rewind_.has_value() && !on;
    if (on) {
        SetRewind(false);
        pending_resume_ = false;
    }
    if (changed) {
        ApplyCheats();
    }
}

Status PlayerSession::SaveScreenshot(const std::filesystem::path& png) {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    if (!core_ || !core_->ReadPicture(&width, &height, &rgba) || width == 0 || height == 0) {
        return Status::Fail("There is no picture to save.");
    }
    std::error_code ec;
    std::filesystem::create_directories(png.parent_path(), ec);
    return gfx::SavePng(png, static_cast<int>(width), static_cast<int>(height), rgba.data());
}

} // namespace den::player
