// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Device facts on Windows: the maker and model the firmware reports, the CPU's name and the
// physical size of the main screen.

#include <cmath>
#include <string>

#include <windows.h>

#include "../device_native.h"

namespace den::pal {

namespace {

std::string RegistryText(const wchar_t* path, const wchar_t* name) {
    wchar_t wide[256];
    DWORD bytes = sizeof(wide);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path, name, RRF_RT_REG_SZ, nullptr, wide, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    char utf8[512];
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return length > 1 ? std::string(utf8, static_cast<std::size_t>(length - 1)) : std::string();
}

} // namespace

void FillNativeFacts(DeviceFacts& facts) {
    const wchar_t* bios = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    facts.manufacturer = RegistryText(bios, L"SystemManufacturer");
    facts.model = RegistryText(bios, L"SystemProductName");
    // Handheld makers put the name people know in the family field.
    if (const std::string family = RegistryText(bios, L"SystemFamily"); !family.empty()) {
        facts.model += " (" + family + ")";
    }
    facts.soc = RegistryText(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    facts.touch = facts.touch || (GetSystemMetrics(SM_DIGITIZER) & NID_INTEGRATED_TOUCH) != 0;

    if (!facts.displays.empty() && facts.displays.front().diagonal_inches <= 0) {
        if (const HDC screen = GetDC(nullptr)) {
            const double width = GetDeviceCaps(screen, HORZSIZE), height = GetDeviceCaps(screen, VERTSIZE);
            ReleaseDC(nullptr, screen);
            if (width > 0 && height > 0) {
                facts.displays.front().diagonal_inches = static_cast<float>(std::hypot(width, height) / 25.4);
            }
        }
    }
}

} // namespace den::pal
