// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>

#include "den/app/console_state.h"
#include "den/app/app.h"
#include "den/app/engine_setup.h"
#include "den/app/pages.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

struct Tracked {
    JobPtr job;
    std::string name;
    std::string done;
};
std::map<std::string, Tracked> g_jobs;

void WatchJobs() {
    for (auto it = g_jobs.begin(); it != g_jobs.end();) {
        const Tracked& tracked = it->second;
        if (!tracked.job->Finished()) {
            ++it;
            continue;
        }
        ui::Stage& stage = *Svc().stage;
        switch (tracked.job->State()) {
        case JobState::Done:
            stage.Toast(tracked.name + " " + tracked.done);
            break;
        case JobState::Cancelled:
            stage.Toast(tracked.name + ": cancelled");
            break;
        default: {
            const std::string error = tracked.job->Error();
            stage.Toast(tracked.name + ": " + (error.empty() ? "that did not work" : error), 6.0f);
            break;
        }
        }
        it = g_jobs.erase(it);
        RebuildTop();
    }
}

} // namespace

JobPtr ConsoleJob(const std::string& system) {
    const auto it = g_jobs.find(system);
    return it == g_jobs.end() ? nullptr : it->second.job;
}

void TrackConsoleJob(const std::string& system, const std::string& name, const std::string& done,
                     JobPtr job) {
    if (job) {
        g_jobs[system] = Tracked{std::move(job), name, done};
        RebuildTop();
    }
}

ConsoleState StateOf(const plugins::Console& console) {
    Services& svc = Svc();
    if (ConsoleJob(console.id)) {
        return ConsoleState::Busy;
    }
    if (console.runner == plugins::Runner::Engine) {
        if (svc.engine->Available(console.id) || FoundEngineCore(console.id)) {
            return console.update_available ? ConsoleState::UpdateAvailable : ConsoleState::Ready;
        }
        // An engine EmuDEN downloads is installed like a core; another one the player brings.
        return console.engine_download && !console.engine_downloaded ? ConsoleState::NotInstalled
                                                                     : ConsoleState::EngineMissing;
    }
    if (!console.installed) {
        return console.available ? ConsoleState::NotInstalled : ConsoleState::NotAvailable;
    }
    if (console.update_available) {
        return ConsoleState::UpdateAvailable;
    }
    const auto core = svc.plugins->CoreFor(console.id);
    if (core && !core->missing_bios.empty()) {
        return ConsoleState::MissingBios;
    }
    return ConsoleState::Ready;
}

std::string StateText(const plugins::Console& console) {
    switch (StateOf(console)) {
    case ConsoleState::Busy:
        return JobText(ConsoleJob(console.id), "Working");
    case ConsoleState::NotInstalled:
        return "Core not installed";
    case ConsoleState::NotAvailable:
        return "Not for this device";
    case ConsoleState::UpdateAvailable:
        return "Update available";
    case ConsoleState::MissingBios:
        return "BIOS file missing";
    case ConsoleState::EngineMissing:
        return "Engine not set up";
    case ConsoleState::Ready:
        break;
    }
    return "Ready";
}

int ReadyCount() {
    int ready = 0;
    for (const plugins::Console& console : Svc().plugins->Consoles()) {
        const ConsoleState state = StateOf(console);
        ready += state == ConsoleState::Ready || state == ConsoleState::UpdateAvailable ||
                 state == ConsoleState::MissingBios;
    }
    return ready;
}

DEN_REGISTER_FRAME(WatchJobs);

} // namespace den::app
