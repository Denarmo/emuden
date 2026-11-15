// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Features register themselves from their own source files with the macros below. The build
// globs app/src and app/screens, so adding a screen, a rail entry, settings rows or a hook is
// adding a file: no shared file is edited.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "den/ui/page.h"
#include "den/ui/widgets.h"

namespace den::app {

struct RailEntry {
    int order = 0; // lower is higher up
    std::string label;
    std::string route;
};

struct SettingsSection {
    int order = 0;
    std::string title;
    /// Called whenever the settings page is built, so rows can depend on the current state.
    std::function<std::vector<ui::RowSpec>()> rows;
};

class Registry {
public:
    static Registry& Get();

    void AddScreen(std::string route, ui::Stage::Factory factory);
    void AddRail(RailEntry entry);
    void AddSettings(SettingsSection section);
    /// Runs once, after the services exist and before the first screen is shown.
    void AddStartup(std::function<void()> hook);
    /// Runs every frame on the main thread, before the screens update.
    void AddFrame(std::function<void()> hook);
    /// Asked every frame once the app is to close: true while it should wait a little (work
    /// that must not be lost). The app closes when none does, after a few seconds at most, or
    /// at once when the user asks to close again.
    void AddQuitHold(std::function<bool()> hook);

    const std::vector<std::pair<std::string, ui::Stage::Factory>>& Screens() const {
        return screens_;
    }
    /// Sorted by order.
    std::vector<RailEntry> Rail() const;
    std::vector<SettingsSection> Settings() const;
    const std::vector<std::function<void()>>& Startup() const {
        return startup_;
    }
    const std::vector<std::function<void()>>& Frame() const {
        return frame_;
    }
    const std::vector<std::function<bool()>>& QuitHolds() const {
        return quit_holds_;
    }

private:
    std::vector<std::pair<std::string, ui::Stage::Factory>> screens_;
    std::vector<RailEntry> rail_;
    std::vector<SettingsSection> settings_;
    std::vector<std::function<void()>> startup_, frame_;
    std::vector<std::function<bool()>> quit_holds_;
};

struct Registrar {
    explicit Registrar(const std::function<void(Registry&)>& fn) {
        fn(Registry::Get());
    }
};

} // namespace den::app

#define DEN_CONCAT_(a, b) a##b
#define DEN_CONCAT(a, b) DEN_CONCAT_(a, b)
#define DEN_REGISTRAR static const ::den::app::Registrar DEN_CONCAT(den_registrar_, __LINE__)

/// DEN_REGISTER_SCREEN("details", DetailsScreen); the class derives from ui::Page.
#define DEN_REGISTER_SCREEN(route, Class)                                                         \
    DEN_REGISTRAR([](::den::app::Registry& r) {                                                   \
        r.AddScreen(route, [] { return std::unique_ptr<::den::ui::Page>(new Class()); });         \
    })
/// DEN_REGISTER_RAIL(20, "Library", "library");
#define DEN_REGISTER_RAIL(order, label, route)                                                    \
    DEN_REGISTRAR([](::den::app::Registry& r) { r.AddRail({order, label, route}); })
/// DEN_REGISTER_SETTINGS(30, "Controllers", [] { return std::vector<ui::RowSpec>{...}; });
#define DEN_REGISTER_SETTINGS(order, title, ...)                                                  \
    DEN_REGISTRAR([](::den::app::Registry& r) { r.AddSettings({order, title, __VA_ARGS__}); })
/// DEN_REGISTER_STARTUP([] { ... }); and DEN_REGISTER_FRAME([] { ... });
#define DEN_REGISTER_STARTUP(...)                                                                 \
    DEN_REGISTRAR([](::den::app::Registry& r) { r.AddStartup(__VA_ARGS__); })
#define DEN_REGISTER_FRAME(...)                                                                   \
    DEN_REGISTRAR([](::den::app::Registry& r) { r.AddFrame(__VA_ARGS__); })
#define DEN_REGISTER_QUIT_HOLD(...)                                                               \
    DEN_REGISTRAR([](::den::app::Registry& r) { r.AddQuitHold(__VA_ARGS__); })
