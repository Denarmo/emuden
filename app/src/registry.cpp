// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/app/app.h"
#include "den/app/registry.h"

namespace den::app {

Registry& Registry::Get() {
    static Registry registry;
    return registry;
}

void Registry::AddScreen(std::string route, ui::Stage::Factory factory) {
    screens_.emplace_back(std::move(route), std::move(factory));
}

void Registry::AddRail(RailEntry entry) {
    rail_.push_back(std::move(entry));
}

void Registry::AddSettings(SettingsSection section) {
    settings_.push_back(std::move(section));
}

void Registry::AddStartup(std::function<void()> hook) {
    startup_.push_back(std::move(hook));
}

void Registry::AddFrame(std::function<void()> hook) {
    frame_.push_back(std::move(hook));
}

void Registry::AddQuitHold(std::function<bool()> hook) {
    quit_holds_.push_back(std::move(hook));
}

void Registry::AddOverlay(std::function<void(ui::Context&)> draw) {
    overlays_.push_back(std::move(draw));
}

void Registry::AddAfterDraw(std::function<void(int, int)> hook) {
    after_draw_.push_back(std::move(hook));
}

void Registry::AddEventFilter(std::function<bool(const input::Event&)> filter) {
    event_filters_.push_back(std::move(filter));
}

void Registry::AddShutdown(std::function<void()> hook) {
    shutdown_.push_back(std::move(hook));
}

std::vector<RailEntry> Registry::Rail() const {
    std::vector<RailEntry> out = rail_;
    std::stable_sort(out.begin(), out.end(),
                     [](const RailEntry& a, const RailEntry& b) { return a.order < b.order; });
    return out;
}

std::vector<SettingsSection> Registry::Settings() const {
    std::vector<SettingsSection> out = settings_;
    std::stable_sort(out.begin(), out.end(),
                     [](const SettingsSection& a, const SettingsSection& b) { return a.order < b.order; });
    return out;
}

} // namespace den::app
