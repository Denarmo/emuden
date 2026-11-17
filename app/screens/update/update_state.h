// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The app updater's state, shared by its files in this folder (update_glue.cpp runs it;
// update_rows.cpp and update_page.cpp show it). Main thread only.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "den/base/jobs.h"
#include "den/platform/self_update.h"
#include "den/system/settings.h"
#include "den/ui/widgets.h"
#include "den/update/release.h"

namespace den::app {

struct UpdateState {
    std::string running;             // the version compared; empty in a development build
    pal::self_update::Target target; // how this copy can update itself
    std::string asset;               // the release's file for this system
    std::optional<update::Release> release; // the newest release the last check found
    bool newer = false;                     // ... and it is newer than `running`
    std::int64_t checked_at = 0;            // Unix seconds of the last answer
    std::string check_error;
    JobPtr check_job;
    JobPtr install_job;
    bool restart_pending = false; // installed (Windows: staged), waiting for the restart
};

UpdateState& Updates();

extern const system::Key<bool> AUTO_CHECK_KEY;
extern const system::Key<bool> BETA_KEY;

/// A release build that may be offered updates.
bool UpdatesOffered();
/// Asks GitHub now. `automatic`: the daily check, which tells about a newer version in a toast.
void CheckForUpdates(bool automatic);
/// Downloads, checks and installs the newer release the way this system does it.
void DownloadAndInstall();
/// What Download and install is called here ("Open the release page" where it cannot).
std::string InstallLabel();
/// The state of the last check, for a row's value.
std::string CheckStatusText();
/// When the last check was, for a row's description.
std::string LastCheckText();
/// The newer release's install row (update_rows.cpp), for the notes page.
std::vector<ui::RowSpec> UpdateInstallRows();

/// Test-only overrides (update_test.cpp; EMUDEN_DEV_TOOLS builds): a running version
/// (EMUDEN_TEST_UPDATE_VERSION) and a releases API address (EMUDEN_TEST_UPDATE_URL) whose
/// downloads may come from 127.0.0.1. Empty when unset.
struct UpdateTestOverrides {
    std::string version;
    std::string url;
};
UpdateTestOverrides TestOverrides();

} // namespace den::app
