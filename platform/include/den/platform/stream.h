// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A byte stream to another program: a loopback TCP connection or a child's standard input
// and output. The engine channel (den::engine) runs on it.

#pragma once

#include <cstddef>
#include <memory>

#include "den/base/result.h"

namespace den::pal {

class Stream {
public:
    /// Closes the stream.
    virtual ~Stream() = default;
    /// Blocks until all `size` bytes arrived. False once the other side closed, or on Close.
    virtual bool Read(void* data, std::size_t size) = 0;
    /// Blocks until all of it is written. False when the other side is gone.
    virtual bool Write(const void* data, std::size_t size) = 0;
    /// Ends the stream from any thread: the other side sees it closed, and a Read blocked on
    /// another thread returns false (for a child's pipes, once the child let go of them).
    virtual void Close() = 0;
};

/// A TCP listener on 127.0.0.1 only, on a port the system picks.
class Listener {
public:
    virtual ~Listener() = default;
    virtual int Port() const = 0;
    /// Waits up to `seconds` for the next connection.
    virtual Result<std::unique_ptr<Stream>> Accept(double seconds) = 0;
    /// Wakes an Accept blocked on another thread, which then fails.
    virtual void Close() = 0;
};

Result<std::unique_ptr<Listener>> ListenLoopback();
/// A connection to 127.0.0.1:port (tests and tools play the other side with it).
Result<std::unique_ptr<Stream>> ConnectLoopback(int port);

} // namespace den::pal
