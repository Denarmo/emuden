// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One running engine session, whatever protocol level drives it. The host only talks to
// this; a level-2 link (pipes, hello, launch message) is another class behind OpenLink.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "den/engine/host.h"

namespace den::engine {

class Link {
public:
    /// Ends the engine if it still runs.
    virtual ~Link() = default;
    /// The exit code once the engine has ended. Never blocks.
    virtual std::optional<int> Exited() = 0;
    /// Asks the engine to end the session and close. False when it could not be asked.
    virtual bool RequestStop() = 0;
    /// Called now and then after RequestStop: answers a "really close?" the engine raised.
    virtual void ConfirmStop() {}
    virtual void Kill() = 0;
    /// Session events beyond start and exit (none at level 0).
    virtual std::vector<Event> Drain() {
        return {};
    }
};

/// Starts `install` for the request at the install's protocol level.
Result<std::unique_ptr<Link>> OpenLink(const Install& install, const LaunchRequest& request,
                                       const std::vector<std::string>& args);

} // namespace den::engine
