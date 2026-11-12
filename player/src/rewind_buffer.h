// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Rewind history: the newest save state in full, older ones as XOR deltas against their
// successor, run-length encoded.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace den::player {

class RewindBuffer {
public:
    /// `budgetbytes` bounds the memory of stored states, the newest full one included.
    explicit RewindBuffer(std::size_t budget_bytes);

    /// Adds the newest state, dropping the oldest ones past the budget. A state of a different
    /// size clears the history first.
    void Push(std::span<const std::uint8_t> state);

    /// Writes the state before the newest into `out` and makes it the newest. False when there
    /// is nothing older or `out` has the wrong size.
    bool StepBack(std::span<std::uint8_t> out);

    /// States that StepBack can still return.
    std::size_t Count() const {
        return deltas_.size();
    }
    /// At most this many states are kept behind the newest, whatever the budget allows; 0 is
    /// no limit. The oldest past it are dropped at once.
    void SetLimit(std::size_t states);
    std::size_t Limit() const {
        return limit_;
    }
    /// The number of the newest state: it goes up with each Push and down with each StepBack,
    /// so a caller can tell where in the history something it noted lies. Clear moves it on:
    /// nothing noted before lies in the history that follows.
    std::uint64_t Serial() const {
        return serial_;
    }
    /// What `states` states behind the newest would take, going by those held now; the
    /// newest alone while there is no history.
    std::size_t BytesFor(std::size_t states) const;
    std::size_t Bytes() const {
        return bytes_;
    }
    void Clear();

private:
    void Trim();

    std::size_t budget_;
    std::size_t limit_ = 0;
    std::uint64_t serial_ = 0;
    std::size_t bytes_ = 0;
    std::vector<std::uint8_t> head_;
    std::vector<std::uint8_t> scratch_;
    std::deque<std::vector<std::uint8_t>> deltas_; // oldest first
};

/// XOR of `a` and `b` (same size), run-length encoded: pairs of varints (equal bytes to skip,
/// literal bytes that follow).
void XorRleEncode(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b,
                  std::vector<std::uint8_t>& out);
/// XORs an encoded delta into `buffer` in place. False when the delta is malformed.
bool XorRleApply(std::span<std::uint8_t> buffer, std::span<const std::uint8_t> encoded);

} // namespace den::player
