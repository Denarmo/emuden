// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Input-delay lockstep for two players. Both devices run the same core from the same state;
// a frame runs only once both players' buttons for it are known. Each device's buttons are
// given `delay` frames ahead, so on a local network the frame is never waited for. Every
// CRC_FRAMES the two states' checksums are compared; a mismatch is a desync, which the host
// mends by sending its state again.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

namespace den::net {

struct FrameInput {
    std::uint16_t buttons = 0;     // RetroPad bits
    std::array<std::int16_t, 4> axes{}; // left x, y, right x, y
    bool operator==(const FrameInput&) const = default;
};

class Lockstep {
public:
    static constexpr int PLAYERS = 2;
    static constexpr std::uint64_t CRC_FRAMES = 60;
    static constexpr std::uint64_t HISTORY = 600;

    /// `frame` is the next frame to run, the same on both devices. The first `delay` frames
    /// have no buttons pressed.
    void Start(std::uint64_t frame, int local_player, int delay);
    bool Started() const {
        return started_;
    }
    int Delay() const {
        return delay_;
    }
    std::uint64_t Next() const {
        return next_;
    }

    /// Whether this device's buttons are due (call before each frame that is to run).
    bool NeedsLocal() const;
    /// The buttons of now, for the frame they are given for (returned; send it to the peer).
    std::uint64_t AddLocal(const FrameInput& input);
    void AddRemote(std::uint64_t frame, const FrameInput& input);
    /// Both players' buttons for Next() are known.
    bool Ready() const;
    /// The buttons per player for Next(); only when Ready.
    std::array<FrameInput, PLAYERS> Inputs() const;
    /// Next() was run.
    void Advance();
    /// A state of the end of `frame - 1` was loaded (a resync): `frame` runs next. The
    /// buttons already known are kept.
    void Jump(std::uint64_t frame);

    /// Whether a checksum is taken after the frame that just ran (Next() - 1).
    bool CrcDue() const;
    void LocalCrc(std::uint64_t frame, std::uint32_t crc);
    void RemoteCrc(std::uint64_t frame, std::uint32_t crc);
    /// The first frame whose checksums differ, once; nullopt when none did.
    std::optional<std::uint64_t> TakeDesync();
    /// The newest frame both checksums agreed on.
    std::uint64_t Agreed() const {
        return agreed_;
    }

private:
    void Compare(std::uint64_t frame);
    void Prune();

    bool started_ = false;
    int local_ = 0;
    int delay_ = 2;
    std::uint64_t next_ = 0;
    std::uint64_t local_through_ = 0; // local buttons known up to here (exclusive)
    std::map<std::uint64_t, FrameInput> local_inputs_, remote_inputs_;
    std::map<std::uint64_t, std::uint32_t> local_crc_, remote_crc_;
    std::optional<std::uint64_t> desync_;
    std::uint64_t agreed_ = 0;
};

} // namespace den::net
