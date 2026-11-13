// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the operating system says about the device the app runs on. Raw facts only: what
// they mean (form factor, performance tier) is decided in den/system/device.h.

#pragma once

#include <string>
#include <vector>

namespace den::pal {

struct DisplayFacts {
    std::string name;
    int width = 0, height = 0; // pixels
    float refresh_hz = 0;      // 0 when unknown
    float density = 1.0f;      // pixels per logical unit (1.0 at 96 dpi)
    float diagonal_inches = 0; // physical size; 0 when unknown
};

struct DeviceFacts {
    std::string manufacturer; // "Retroid", "Valve", "LENOVO"
    std::string model;        // Build.MODEL, the DMI product name
    std::string device_name;  // the name its owner gave it (Windows: the PC's name); may be empty
    std::string soc;          // Android: the chip's model; elsewhere the CPU's name
    std::string os_version;
    int api_level = 0;                  // Android SDK level; 0 elsewhere
    std::vector<DisplayFacts> displays; // the main one first
    bool touch = false;
    bool builtin_pad = false; // a gamepad that is part of the device
    int cpu_cores = 0;
    int ram_mb = 0;
};

/// Asks the system. Call after Init; cheap enough to call once per start.
DeviceFacts GetDeviceFacts();

} // namespace den::pal
