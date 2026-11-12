// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "rewind_buffer.h"

namespace den::player {

namespace {

// Shorter runs of equal bytes stay inside a literal.
constexpr std::size_t MIN_ZERO_RUN = 4;

void PutVarint(std::vector<std::uint8_t>& out, std::size_t value) {
    do {
        const std::uint8_t low = value & 0x7F;
        value >>= 7;
        out.push_back(static_cast<std::uint8_t>(low | (value ? 0x80 : 0)));
    } while (value);
}

bool GetVarint(std::span<const std::uint8_t> in, std::size_t& pos, std::size_t& value) {
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        if (pos >= in.size()) {
            return false;
        }
        const std::uint8_t byte = in[pos++];
        value |= static_cast<std::size_t>(byte & 0x7F) << shift;
        if (!(byte & 0x80)) {
            return true;
        }
    }
    return false;
}

} // namespace

void XorRleEncode(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b,
                  std::vector<std::uint8_t>& out) {
    out.clear();
    const std::size_t size = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < size) {
        std::size_t equal = 0;
        while (i < size && a[i] == b[i]) {
            ++equal;
            ++i;
        }
        const std::size_t start = i;
        while (i < size) {
            if (a[i] != b[i]) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < size && a[j] == b[j] && j - i < MIN_ZERO_RUN) {
                ++j;
            }
            if (j - i >= MIN_ZERO_RUN || j == size) {
                break;
            }
            i = j;
        }
        PutVarint(out, equal);
        PutVarint(out, i - start);
        for (std::size_t k = start; k < i; ++k) {
            out.push_back(a[k] ^ b[k]);
        }
    }
}

bool XorRleApply(std::span<std::uint8_t> buffer, std::span<const std::uint8_t> encoded) {
    std::size_t in = 0;
    std::size_t pos = 0;
    while (in < encoded.size()) {
        std::size_t equal = 0;
        std::size_t literal = 0;
        if (!GetVarint(encoded, in, equal) || !GetVarint(encoded, in, literal)) {
            return false;
        }
        if (equal > buffer.size() - pos) {
            return false;
        }
        pos += equal;
        if (literal > buffer.size() - pos || literal > encoded.size() - in) {
            return false;
        }
        for (std::size_t k = 0; k < literal; ++k) {
            buffer[pos + k] ^= encoded[in + k];
        }
        pos += literal;
        in += literal;
    }
    return true;
}

RewindBuffer::RewindBuffer(std::size_t budget_bytes) : budget_{budget_bytes} {}

void RewindBuffer::Clear() {
    deltas_.clear();
    head_.clear();
    bytes_ = 0;
    ++serial_;
}

void RewindBuffer::Push(std::span<const std::uint8_t> state) {
    if (state.size() != head_.size()) {
        Clear();
        head_.assign(state.begin(), state.end());
        bytes_ = head_.size();
        return;
    }
    // The delta that turns the new state back into the current head.
    XorRleEncode(state, head_, scratch_);
    bytes_ += scratch_.size();
    deltas_.emplace_back(scratch_.begin(), scratch_.end());
    std::copy(state.begin(), state.end(), head_.begin());
    ++serial_;
    Trim();
}

void RewindBuffer::Trim() {
    while (!deltas_.empty() && (bytes_ > budget_ || (limit_ > 0 && deltas_.size() > limit_))) {
        bytes_ -= deltas_.front().size();
        deltas_.pop_front();
    }
}

void RewindBuffer::SetLimit(std::size_t states) {
    limit_ = states;
    Trim();
}

std::size_t RewindBuffer::BytesFor(std::size_t states) const {
    if (deltas_.empty()) {
        return head_.size();
    }
    const double each = static_cast<double>(bytes_ - head_.size()) / static_cast<double>(deltas_.size());
    return head_.size() + static_cast<std::size_t>(each * static_cast<double>(states));
}

bool RewindBuffer::StepBack(std::span<std::uint8_t> out) {
    if (deltas_.empty() || out.size() != head_.size()) {
        return false;
    }
    if (!XorRleApply(head_, deltas_.back())) {
        Clear();
        return false;
    }
    bytes_ -= deltas_.back().size();
    deltas_.pop_back();
    --serial_;
    std::copy(head_.begin(), head_.end(), out.begin());
    return true;
}

} // namespace den::player
