// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A stand-in plugin host: a fixed list of consoles, nothing installed. Delete this file when
// the real host defines CreateHost.

#include <algorithm>

#include "den/plugins/host.h"

namespace den::plugins {

namespace {

class FakeHost final : public Host {
public:
    explicit FakeHost(const Config& config) : config_(config) {
        const auto add = [this](const char* id, const char* name, const char* short_name,
                                std::vector<std::string> extensions, Runner runner = Runner::Core) {
            Console console;
            console.id = id;
            console.name = name;
            console.short_name = short_name;
            console.plugin_id = std::string("fake.") + id;
            console.extensions = std::move(extensions);
            console.runner = runner;
            consoles_.push_back(std::move(console));
        };
        add("gb", "Game Boy", "GB", {"gb"});
        add("gbc", "Game Boy Color", "GBC", {"gbc"});
        add("gba", "Game Boy Advance", "GBA", {"gba"});
        add("nds", "Nintendo DS", "NDS", {"nds"});
        add("psx", "PlayStation", "PS1", {"cue", "chd", "pbp"});
        add("psp", "PlayStation Portable", "PSP", {"iso", "cso"});
        add("switch2", "Nintendo Switch 2", "Switch 2", {"nsp", "xci"}, Runner::Engine);
    }

    std::vector<Console> Consoles() const override {
        return consoles_;
    }
    std::optional<Console> FindConsole(const std::string& system) const override {
        const auto it = std::find_if(consoles_.begin(), consoles_.end(),
                                     [&](const Console& c) { return c.id == system; });
        return it == consoles_.end() ? std::nullopt : std::optional<Console>(*it);
    }
    std::optional<CoreInfo> CoreFor(const std::string&) const override {
        return std::nullopt;
    }
    bool Installed(const std::string&) const override {
        return false;
    }
    JobPtr Install(const std::string&) override {
        return Unavailable("install");
    }
    JobPtr Remove(const std::string&) override {
        return Unavailable("remove");
    }
    JobPtr Refresh() override {
        return Unavailable("refresh");
    }
    std::uint64_t Revision() const override {
        return 1;
    }

private:
    JobPtr Unavailable(const char* what) {
        return config_.jobs->Run(what, [](Job&) {
            return Status::Fail("The plugin host is not built into this version yet.");
        });
    }

    Config config_;
    std::vector<Console> consoles_;
};

} // namespace

std::unique_ptr<Host> CreateHost(const Config& config) {
    return std::make_unique<FakeHost>(config);
}

} // namespace den::plugins
