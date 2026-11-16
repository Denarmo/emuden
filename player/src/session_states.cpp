// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Battery saves and save states of the running game.

#include <algorithm>
#include <cstring>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/device.h"
#include "den/states/gba.h"
#include "session_impl.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

std::string_view Bytes(std::span<const std::uint8_t> data) {
    return std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
}

// GBA battery saves are kept in the layout every GBA core reads (docs/gba-states.md), so a
// save made with one core plays with another.
bool IsGbaGame(const fs::path& content) {
    return EqualsNoCase(PathToUtf8(content.extension()), ".gba");
}

} // namespace

void PlayerSession::LoadBatterySave() {
    const std::span<std::uint8_t> ram = core_->SaveRam();
    if (!ram.empty()) {
        if (const auto data = ReadBinaryFile(paths_.BatterySave())) {
            if (IsGbaGame(content_path_)) {
                // Only VBA Next needs to know whether a large save belongs to an EEPROM.
                const bool eeprom = EqualsNoCase(core_->Info().name, "VBA Next") &&
                                    states::RomUsesEeprom(ReadBinaryFile(content_path_).value_or(
                                        std::vector<std::uint8_t>()));
                states::LoadGbaSave(core_->Info().name, *data, eeprom, ram);
            } else {
                std::memcpy(ram.data(), data->data(), std::min(ram.size(), data->size()));
            }
        }
    }
    saved_ram_.assign(ram.begin(), ram.end());
}

void PlayerSession::FlushSaves() {
    if (!core_ || !together_.write_saves) {
        return;
    }
    const std::span<std::uint8_t> ram = core_->SaveRam();
    if (ram.empty() || (ram.size() == saved_ram_.size() &&
                        std::equal(ram.begin(), ram.end(), saved_ram_.begin()))) {
        return;
    }
    const std::vector<std::uint8_t> portable =
        IsGbaGame(content_path_) ? states::PortableGbaSave(ram) : std::vector<std::uint8_t>(ram.begin(), ram.end());
    if (!WriteFileAtomic(paths_.BatterySave(), Bytes(portable))) {
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
    const std::string& own = core_->Info().name;
    const auto unwrapped = UnwrapState(own, *data);
    std::span<const std::uint8_t> payload;
    std::vector<std::uint8_t> converted;
    if (unwrapped) {
        payload = unwrapped.value();
    } else {
        // Another GBA core's state is rewritten for this one, from a state of this one.
        const std::string wrote = StateCoreName(*data);
        if (!states::CanConvertGbaState(wrote, own)) {
            return unwrapped.status();
        }
        std::vector<std::uint8_t> current(core_->StateSize());
        if (current.empty() || !core_->SaveState(current)) {
            return Status::Fail("This game cannot save a state right now.");
        }
        auto result = states::ConvertGbaState(wrote, StatePayload(*data), own, current);
        if (!result) {
            log::Warn("player", "the {} state could not be converted for {}: {}", wrote, own, result.error());
            return Status::Fail("The save state was made with " + wrote + " and could not be converted for " + own +
                                ".");
        }
        log::Info("player", "converted a {} state for {}", wrote, own);
        converted = result.take();
        payload = converted;
    }
    // Cores read as many bytes as their state has now: a state from another version of the
    // core may be shorter.
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

void PlayerSession::SettleForOtherCores() {
    // A state is taken at the end of a frame, where a game often waits in the BIOS: a wait
    // converts, but a DMA or a save chip command midway, or other BIOS calls, do not (and
    // another device may play the game with another core). A few frames on, they are done.
    constexpr int MOST_FRAMES = 30;
    const std::string& name = core_->Info().name;
    if (!IsGbaGame(content_path_) || together_.party || !states::CanConvertGbaState(name, name == "gpSP" ? "mGBA" : "gpSP")) {
        return;
    }
    std::vector<std::uint8_t> state(core_->StateSize());
    int ran = 0;
    for (; ran < MOST_FRAMES; ++ran) {
        state.resize(core_->StateSize());
        if (state.empty() || !core_->SaveState(state)) {
            break;
        }
        const std::string why = states::GbaStateNotPortable(name, state);
        if (why.empty()) {
            break;
        }
        if (ran == 0) {
            log::Info("player", "the resume state would not convert ({}): running on a little", why);
        }
        core_->SetAudioMuted(true);
        core_->RunFrame();
    }
    core_->SetAudioMuted(false);
    if (ran > 0) {
        FlushSaves();
    }
}

void PlayerSession::WriteThumbnail(const fs::path& file) {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    if (core_->ReadPicture(&width, &height, &rgba) && width > 0 && height > 0) {
        if (const Status saved = gfx::SavePng(file, static_cast<int>(width), static_cast<int>(height), rgba.data());
            !saved) {
            log::Warn("player", "state picture: {}", saved.error);
        }
    }
}

Status PlayerSession::SaveState(int slot) {
    if (slot < 0 || slot >= STATE_SLOTS) {
        return Status::Fail("There is no such slot.");
    }
    if (!allowed_.save_states) {
        return Status::Fail("This console has no save states.");
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
    if (!allowed_.save_states) {
        return Status::Fail("This console has no save states.");
    }
    if (together_.party) {
        return Status::Fail("Not while playing together: it would leave the other device behind.");
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
