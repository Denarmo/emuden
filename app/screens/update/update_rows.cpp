// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Settings, Updates: this version, the check, the newer release's notes and its install.

#include <format>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "update_state.h"

namespace den::app {

namespace {

// The rows Settings and the notes page share for a newer release.
std::vector<ui::RowSpec> ReleaseRows(bool with_notes) {
    std::vector<ui::RowSpec> rows;
    const UpdateState& state = Updates();
    if (!state.newer || !state.release) {
        return rows;
    }
    if (with_notes) {
        rows.push_back(ActionRow("update.notes", std::format("What's new in {}", state.release->version),
                                 [] { Svc().stage->Push("update"); }));
    }
    ui::RowSpec install = ValueRow(
        "update.install", InstallLabel(), [] { return JobText(Updates().install_job, ""); },
        [] { DownloadAndInstall(); });
    install.description = state.target.method == pal::self_update::Method::ReleasePage
                              ? state.target.reason
                              : "Checked against the release's SHA-256 checksums before it is installed";
    rows.push_back(std::move(install));
    return rows;
}

std::vector<ui::RowSpec> UpdateRows() {
    std::vector<ui::RowSpec> rows;
    ui::RowSpec version = ValueRow("update.version", "This version", [] {
        return UpdatesOffered() ? Updates().running : std::string("Development build");
    });
    rows.push_back(std::move(version));

    ui::RowSpec check = ValueRow("update.check", "Check for updates", CheckStatusText, [] { CheckForUpdates(false); });
    check.description = LastCheckText();
    check.live_description = LastCheckText;
    rows.push_back(std::move(check));
    if (!UpdatesOffered()) {
        return rows;
    }
    for (ui::RowSpec& row : ReleaseRows(true)) {
        rows.push_back(std::move(row));
    }

    ui::RowSpec automatic = ToggleRow(
        "update.auto", "Check automatically", [] { return system::Store().Get(AUTO_CHECK_KEY); },
        [](bool on) { system::Store().Set(AUTO_CHECK_KEY, on); });
    automatic.description = "Once a day. Only GitHub's public list of releases is asked; nothing about you is sent";
    rows.push_back(std::move(automatic));

    ui::RowSpec beta = ToggleRow(
        "update.beta", "Beta updates", [] { return system::Store().Get(BETA_KEY); },
        [](bool on) {
            system::Store().Set(BETA_KEY, on);
            Updates().checked_at = 0; // the other list: ask again
            CheckForUpdates(false);
        });
    beta.description = "Also offer pre-releases, which may have more bugs";
    rows.push_back(std::move(beta));
    return rows;
}

} // namespace

std::vector<ui::RowSpec> UpdateInstallRows() {
    return ReleaseRows(false);
}

DEN_REGISTER_SETTINGS(99, "Updates", UpdateRows);

} // namespace den::app
