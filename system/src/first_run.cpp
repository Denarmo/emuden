// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>

#include "den/store/settings.h"
#include "den/system/first_run.h"
#include "den/system/info.h"

namespace den::system {

namespace {

constexpr const char* FIRST_START_KEY = "app.first_start_unix";
constexpr const char* LAST_VERSION_KEY = "app.last_version";
constexpr const char* SETUP_DONE_KEY = "setup.done";

StartInfo g_start;

} // namespace

StartInfo NoteStart(store::Settings& settings) {
    StartInfo info;
    info.first_run = !settings.Has(FIRST_START_KEY);
    info.setup_done = SetupDone(settings);
    info.previous_version = settings.GetString(LAST_VERSION_KEY);
    info.upgraded = !info.first_run && info.previous_version != Version();
    if (info.first_run) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch());
        settings.SetNumber(FIRST_START_KEY, static_cast<double>(now.count()));
    }
    if (info.previous_version != Version()) {
        settings.SetString(LAST_VERSION_KEY, Version());
    }
    g_start = info;
    return info;
}

const StartInfo& ThisStart() {
    return g_start;
}

bool SetupDone(const store::Settings& settings) {
    return settings.GetBool(SETUP_DONE_KEY, false);
}

void MarkSetupDone(store::Settings& settings) {
    settings.SetBool(SETUP_DONE_KEY, true);
}

} // namespace den::system
