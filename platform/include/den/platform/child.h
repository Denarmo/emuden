// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A child process that never outlives the app: a job object on Windows, the parent-death
// signal on Linux. Used for engines; pal::StartProcess stays for short helper commands.

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "den/base/result.h"
#include "den/platform/stream.h"

namespace den::pal {

struct ChildDesc {
    std::filesystem::path executable;
    std::vector<std::string> args;
    /// Added to (or replacing in) the app's own environment.
    std::vector<std::pair<std::string, std::string>> env;
    /// Empty: the executable's folder.
    std::filesystem::path working_dir;
    /// Its standard input and output become Child::Channel(); its standard error goes to
    /// `log_file` (nowhere when empty).
    bool channel = false;
    std::filesystem::path log_file;
};

class Child {
public:
    /// Ends the child and everything it started.
    virtual ~Child() = default;
    /// The exit code once the child has ended. Never blocks.
    virtual std::optional<int> Poll() = 0;
    /// Asks it to close the way its user would (window close, SIGTERM). It may refuse.
    /// False when nothing could be asked (it has ended, or has no window yet).
    virtual bool RequestClose() = 0;
    /// Answers the "really close?" question a RequestClose may have raised, the way its
    /// user would: Enter in the dialogs of the windows that were asked. Call it now and
    /// then after RequestClose; only the first such dialogs are ever answered. True when a
    /// key was sent. Where a close request raises no dialog (SIGTERM) it does nothing.
    virtual bool ConfirmClose() = 0;
    /// Ends it and everything it started, now.
    virtual void Kill() = 0;
    /// Its standard input and output, when started with ChildDesc::channel. Owned by the child.
    virtual Stream* Channel() {
        return nullptr;
    }
};

Result<std::unique_ptr<Child>> StartChild(const ChildDesc& desc);

} // namespace den::pal
