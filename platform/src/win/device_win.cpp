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

// Board makers leave these in fields they do not fill.
bool Placeholder(const std::string& text) {
    for (const char* filler : {"O.E.M.", "Default string", "System Product Name", "Not Applicable"}) {
        if (text.find(filler) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

void FillNativeFacts(DeviceFacts& facts) {
    const wchar_t* bios = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    facts.manufacturer = RegistryText(bios, L"SystemManufacturer");
    facts.model = RegistryText(bios, L"SystemProductName");
    if (Placeholder(facts.model)) {
        facts.model.clear();
    }
    // Handheld makers put the name people know in the family field.
    if (const std::string family = RegistryText(bios, L"SystemFamily"); !family.empty() && !Placeholder(family)) {
        facts.model += (facts.model.empty() ? "" : " (") + family + (facts.model.empty() ? "" : ")");
    }
    wchar_t name[256];
    DWORD length = 256;
    if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, name, &length) && length > 0) {
        char utf8[1024];
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, name, static_cast<int>(length), utf8, sizeof(utf8),
                                              nullptr, nullptr);
        facts.device_name.assign(utf8, bytes > 0 ? bytes : 0);
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
