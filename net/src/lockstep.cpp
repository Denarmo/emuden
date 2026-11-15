// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/net/lockstep.h"

#include <algorithm>

namespace den::net {

void Lockstep::Start(std::uint64_t frame, int local_player, int delay) {
    started_ = true;
    local_ = std::clamp(local_player, 0, PLAYERS - 1);
    delay_ = std::clamp(delay, 1, 30);
    next_ = frame;
    local_inputs_.clear();
    // The other side's buttons may arrive before its state was taken.
    std::erase_if(remote_inputs_, [frame](const auto& item) { return item.first < frame; });
    local_crc_.clear();
    remote_crc_.clear();
    desync_.reset();
    agreed_ = frame;
    for (std::uint64_t f = frame; f < frame + static_cast<std::uint64_t>(delay_); ++f) {
        local_inputs_[f] = FrameInput{};
        remote_inputs_[f] = FrameInput{};
    }
    local_through_ = frame + static_cast<std::uint64_t>(delay_);
}

bool Lockstep::NeedsLocal() const {
    return started_ && local_through_ <= next_ + static_cast<std::uint64_t>(delay_);
}

std::uint64_t Lockstep::AddLocal(const FrameInput& input) {
    const std::uint64_t frame = local_through_++;
    local_inputs_[frame] = input;
    return frame;
}

void Lockstep::AddRemote(std::uint64_t frame, const FrameInput& input) {
    if (frame + HISTORY >= next_) {
        remote_inputs_[frame] = input;
    }
}

bool Lockstep::Ready() const {
    return started_ && local_inputs_.contains(next_) && remote_inputs_.contains(next_);
}

std::array<FrameInput, Lockstep::PLAYERS> Lockstep::Inputs() const {
    std::array<FrameInput, PLAYERS> out{};
    out[static_cast<std::size_t>(local_)] = local_inputs_.at(next_);
    out[static_cast<std::size_t>(1 - local_)] = remote_inputs_.at(next_);
    return out;
}

void Lockstep::Advance() {
    ++next_;
    Prune();
}

void Lockstep::Jump(std::uint64_t frame) {
    next_ = frame;
    // Checksums from before the jump say nothing about the state from now on.
    std::erase_if(local_crc_, [frame](const auto& item) { return item.first >= frame; });
    std::erase_if(remote_crc_, [frame](const auto& item) { return item.first >= frame; });
    agreed_ = frame;
    desync_.reset();
    // Buttons this device has not given yet for the frames it skipped count as none.
    while (local_through_ < frame) {
        local_inputs_[local_through_++] = FrameInput{};
    }
    Prune();
}

bool Lockstep::CrcDue() const {
    return started_ && next_ > 0 && (next_ - 1) % CRC_FRAMES == 0;
}

void Lockstep::LocalCrc(std::uint64_t frame, std::uint32_t crc) {
    local_crc_[frame] = crc;
    Compare(frame);
}

void Lockstep::RemoteCrc(std::uint64_t frame, std::uint32_t crc) {
    if (frame < agreed_) {
        return;
    }
    remote_crc_[frame] = crc;
    Compare(frame);
}

void Lockstep::Compare(std::uint64_t frame) {
    const auto local = local_crc_.find(frame);
    const auto remote = remote_crc_.find(frame);
    if (local == local_crc_.end() || remote == remote_crc_.end()) {
        return;
    }
    if (local->second != remote->second) {
        if (!desync_) {
            desync_ = frame;
        }
    } else {
        agreed_ = std::max(agreed_, frame);
    }
    local_crc_.erase(local);
    remote_crc_.erase(remote);
}

std::optional<std::uint64_t> Lockstep::TakeDesync() {
    return std::exchange(desync_, std::nullopt);
}

void Lockstep::Prune() {
    if (next_ <= HISTORY) {
        return;
    }
    const std::uint64_t keep = next_ - HISTORY;
    local_inputs_.erase(local_inputs_.begin(), local_inputs_.lower_bound(keep));
    remote_inputs_.erase(remote_inputs_.begin(), remote_inputs_.lower_bound(keep));
    local_crc_.erase(local_crc_.begin(), local_crc_.lower_bound(keep));
    remote_crc_.erase(remote_crc_.begin(), remote_crc_.lower_bound(keep));
}

} // namespace den::net
