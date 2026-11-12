// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/app/app.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

// The signed console list is fetched once per start, in the background; without network the
// bundled manifests stay in use.
void RefreshConsoles() {
    Services& svc = Svc();
    if (svc.settings->GetBool("plugins.refresh_on_start", true)) {
        svc.plugins->Refresh();
    }
}

} // namespace

DEN_REGISTER_STARTUP(RefreshConsoles);

} // namespace den::app
