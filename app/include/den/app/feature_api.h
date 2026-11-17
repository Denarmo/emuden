// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the Plugins screens share: the feature host the app runs and the installs and removals
// that go on while the user is elsewhere.

#pragma once

#include <memory>
#include <string>

#include "den/base/jobs.h"
#include "den/plugins/feature_host.h"
#include "den/plugins/feature_mods.h"
#include "den/plugins/feature_tune.h"
#include "den/plugins/feature_ui.h"

namespace den::app {

/// The feature host, or null when this build has none (see plugins_glue.cpp).
plugins::FeatureHost* FeatureHostSvc();
/// The plugin with that id as the host lists it now.
bool FindFeaturePlugin(const std::string& id, plugins::FeaturePlugin& out);
/// Starts an install (or update) and reports its end with a toast.
void InstallFeaturePlugin(const std::string& id, bool consent);
/// The running install or removal of a plugin, or null.
JobPtr FeaturePluginJob(const std::string& id);
/// What den.device.info, den.game and den.steam of feature plugins reach (plugin_tune.cpp).
std::shared_ptr<plugins::TuneServices> CreateTuneServices();
/// What den.mods of feature plugins reaches (plugin_mods.cpp).
std::shared_ptr<plugins::ModServices> CreateModServices();
void TrackFeaturePluginJob(const std::string& id, const std::string& name, const std::string& done,
                           JobPtr job);

} // namespace den::app
