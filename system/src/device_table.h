// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "den/system/device.h"

namespace den::system::detail {

struct KnownDevice {
    std::vector<const char*> needles;
    const char* name;
    FormFactor form;
    Tier tier;
    float inches; // the main screen, used when the system does not say
};

/// The table row of the device, if it is one the app knows by name.
const KnownDevice* FindKnown(const DeviceInfo& info);
/// The tier of a GPU by its family and model; nothing for a renderer the table cannot place.
std::optional<Tier> GpuTier(const std::string& renderer);

} // namespace den::system::detail
