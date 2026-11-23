// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The tests' peers all live in this process, so their sockets stay on loopback. Listening on the
// network would make Windows Firewall ask for administrator rights for every new build folder.

#include "den/platform/net.h"

namespace {

const bool loopback_only = [] {
    den::pal::net::SetLoopbackOnly(true);
    return true;
}();

} // namespace
