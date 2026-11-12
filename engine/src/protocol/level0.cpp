// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Level 0: the engine is an ordinary program started with the game on its command line;
// the session is over when the process ends.

#include "den/platform/child.h"
#include "protocol/link.h"

namespace den::engine {

namespace {

class ExitWaitLink final : public Link {
public:
    explicit ExitWaitLink(std::unique_ptr<pal::Child> child) : child_(std::move(child)) {}

    std::optional<int> Exited() override {
        return child_->Poll();
    }
    void RequestStop() override {
        child_->RequestClose();
    }
    void Kill() override {
        child_->Kill();
    }

private:
    std::unique_ptr<pal::Child> child_;
};

} // namespace

Result<std::unique_ptr<Link>> OpenLink(const Install& install, const LaunchRequest&,
                                       const std::vector<std::string>& args) {
    // Every install is driven at level 0 until the level-2 link exists.
    pal::ChildDesc desc;
    desc.executable = install.executable;
    desc.args = args;
    auto child = pal::StartChild(desc);
    if (!child) {
        return child.status();
    }
    return std::unique_ptr<Link>(new ExitWaitLink(child.take()));
}

} // namespace den::engine
