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

namespace den::pal {

struct ChildDesc {
    std::filesystem::path executable;
    std::vector<std::string> args;
    /// Added to (or replacing in) the app's own environment.
    std::vector<std::pair<std::string, std::string>> env;
    /// Empty: the executable's folder.
    std::filesystem::path working_dir;
};

class Child {
public:
    /// Ends the child and everything it started.
    virtual ~Child() = default;
    /// The exit code once the child has ended. Never blocks.
    virtual std::optional<int> Poll() = 0;
    /// Asks it to close the way its user would (window close, SIGTERM). It may refuse.
    virtual void RequestClose() = 0;
    /// Ends it and everything it started, now.
    virtual void Kill() = 0;
};

Result<std::unique_ptr<Child>> StartChild(const ChildDesc& desc);

} // namespace den::pal
