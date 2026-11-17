// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

#include "den/engine/host.h"
#include "den/plugins/host.h"

namespace den::app {

/// What an engine app is set up with (ECP configure): the BIOS files the console's manifest
/// names for the core, the library folders that hold the system's games, and the console's
/// option values for the core.
engine::EngineSetup EngineSetupFor(const plugins::CoreInfo& core);

/// The engine install that starts the core.
engine::Install EngineInstallFor(const plugins::CoreInfo& core);

/// A console whose engine the player brings (Switch 2): the engine found on this device, as a
/// core the player screen drives, when it speaks the protocol (level 1 or more).
std::optional<plugins::CoreInfo> FoundEngineCore(const std::string& system);

} // namespace den::app
