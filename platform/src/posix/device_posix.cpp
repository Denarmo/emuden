// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Device facts on Linux: the maker and model from DMI, the CPU's name, and the physical size
// of the first connected screen from its EDID. Android has its own file (src/android).

#ifndef __ANDROID__

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "../device_native.h"

namespace den::pal {

namespace {

std::string FirstLine(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    return line;
}

std::string CpuName() {
    std::ifstream in("/proc/cpuinfo");
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            const auto start = colon == std::string::npos ? colon : line.find_first_not_of(' ', colon + 1);
            return start == std::string::npos ? std::string() : line.substr(start);
        }
    }
    return {};
}

// Bytes 21 and 22 of an EDID are the picture's width and height in centimetres.
float EdidInches() {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
        if (FirstLine(entry.path() / "status") != "connected") {
            continue;
        }
        std::ifstream in(entry.path() / "edid", std::ios::binary);
        unsigned char edid[23] = {};
        if (in.read(reinterpret_cast<char*>(edid), sizeof(edid)) && edid[21] > 0 && edid[22] > 0) {
            return static_cast<float>(std::hypot(edid[21], edid[22]) / 2.54);
        }
    }
    return 0;
}

} // namespace

void FillNativeFacts(DeviceFacts& facts) {
    const std::filesystem::path dmi = "/sys/class/dmi/id";
    facts.manufacturer = FirstLine(dmi / "sys_vendor");
    facts.model = FirstLine(dmi / "product_name");
    if (const std::string family = FirstLine(dmi / "product_family"); !family.empty()) {
        facts.model += " (" + family + ")";
    }
    facts.soc = CpuName();
    facts.os_version = FirstLine("/proc/sys/kernel/osrelease");
    if (!facts.displays.empty() && facts.displays.front().diagonal_inches <= 0) {
        facts.displays.front().diagonal_inches = EdidInches();
    }
}

} // namespace den::pal

#endif
