// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The feature host as the screens reach it, its pump, the rail entry, and the installs and
// removals that run while the user is elsewhere.

#include <format>
#include <map>
#include <memory>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/system/info.h"
#include "../achievements/achievements_glue.h"
#include "fake_host.h"
#include "feature_api.h"

namespace den::app {

namespace {

struct Tracked {
    JobPtr job;
    std::string name, done;
};

std::unique_ptr<plugins::FeatureHost>& HostSlot() {
    static std::unique_ptr<plugins::FeatureHost> host;
    return host;
}

std::map<std::string, Tracked>& TrackedJobs() {
    static std::map<std::string, Tracked> jobs;
    return jobs;
}

void CreateHost() {
    if (pal::GetEnv("EMUDEN_FAKE_PLUGINS") == "1") {
        HostSlot() = CreateSampleFeatureHost();
        return;
    }
    Services& svc = Svc();
    plugins::FeatureConfig config;
    config.data_dir = svc.data_dir;
    config.jobs = svc.jobs;
    config.settings = svc.settings;
    config.plugins = svc.plugins;
    config.library = svc.library;
    config.app_version = system::Version();
    config.tune = CreateTuneServices();
    config.achievements = AchievementServicesSvc();
    HostSlot() = plugins::CreateFeatureHost(config);
}

void EveryFrame() {
    if (plugins::FeatureHost* host = FeatureHostSvc()) {
        host->Pump();
    }
    auto& jobs = TrackedJobs();
    for (auto it = jobs.begin(); it != jobs.end();) {
        const Tracked& tracked = it->second;
        if (!tracked.job->Finished()) {
            ++it;
            continue;
        }
        if (tracked.job->State() == JobState::Done) {
            Svc().stage->Toast(std::format("{} {}", tracked.name, tracked.done));
        } else if (tracked.job->State() == JobState::Failed) {
            Svc().stage->Toast(std::format("{}: {}", tracked.name, tracked.job->Error()), 5.0f);
        }
        it = jobs.erase(it);
        RebuildTop();
    }
}

} // namespace

plugins::FeatureHost* FeatureHostSvc() {
    return HostSlot().get();
}

bool FindFeaturePlugin(const std::string& id, plugins::FeaturePlugin& out) {
    if (plugins::FeatureHost* host = FeatureHostSvc()) {
        for (plugins::FeaturePlugin& plugin : host->Plugins()) {
            if (plugin.id == id) {
                out = std::move(plugin);
                return true;
            }
        }
    }
    return false;
}

JobPtr FeaturePluginJob(const std::string& id) {
    const auto found = TrackedJobs().find(id);
    return found == TrackedJobs().end() ? nullptr : found->second.job;
}

void TrackFeaturePluginJob(const std::string& id, const std::string& name, const std::string& done, JobPtr job) {
    if (job) {
        TrackedJobs()[id] = Tracked{std::move(job), name, done};
        RebuildTop();
    }
}

void InstallFeaturePlugin(const std::string& id, bool consent) {
    plugins::FeaturePlugin plugin;
    if (FeaturePluginJob(id) || !FindFeaturePlugin(id, plugin)) {
        return;
    }
    TrackFeaturePluginJob(id, plugin.name, plugin.installed ? "was updated" : "was installed",
                          FeatureHostSvc()->Install(id, consent));
}

DEN_REGISTER_STARTUP(CreateHost);
DEN_REGISTER_FRAME(EveryFrame);
DEN_REGISTER_RAIL(60, "Plugins", "plugins");

} // namespace den::app
