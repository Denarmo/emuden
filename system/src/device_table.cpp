// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The devices the app knows by name, the tier of a GPU family, and made-up devices for tests.

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "device_table.h"

namespace den::system::detail {

namespace {

using enum FormFactor;
using enum Tier;

// Needles are matched against "maker model" in lower case without spaces; the first row whose
// needles are all found wins, so the more specific rows come first.
const KnownDevice KNOWN[] = {
    {{"retroid", "pocketduo"}, "Retroid Pocket Duo", HandheldDual, Mid, 5.5f},
    {{"retroid", "flip"}, "Retroid Pocket Flip 2", HandheldSmall, Mid, 5.5f},
    {{"retroid", "pocketmini"}, "Retroid Pocket Mini", HandheldSmall, Mid, 3.9f},
    {{"retroid", "pocket5"}, "Retroid Pocket 5", HandheldSmall, Mid, 5.5f},
    {{"ayn", "thor"}, "AYN Thor", HandheldDual, High, 6.0f},
    {{"odin3"}, "AYN Odin 3", HandheldSmall, VeryHigh, 6.0f},
    {{"odin2"}, "AYN Odin 2", HandheldSmall, High, 6.0f},
    {{"ayaneo", "pocketds"}, "Ayaneo Pocket DS", HandheldDual, High, 7.0f},
    {{"ayaneo", "pocketmicro"}, "Ayaneo Pocket Micro", HandheldSmall, Low, 3.5f},
    {{"ayaneo", "pocketair"}, "Ayaneo Pocket Air", HandheldSmall, Mid, 5.5f},
    {{"ayaneo", "pocket"}, "Ayaneo Pocket", HandheldSmall, High, 6.0f},
    {{"valve", "galileo"}, "Steam Deck OLED", HandheldSmall, Mid, 7.4f},
    {{"valve", "jupiter"}, "Steam Deck", HandheldSmall, Mid, 7.0f},
    {{"legiongos"}, "Legion Go S", HandheldSmall, Mid, 8.0f},
    {{"legiongo"}, "Legion Go", HandheldSmall, High, 8.8f},
    {{"rog", "ally"}, "ROG Ally", HandheldSmall, High, 7.0f},
};

std::string Squash(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

// The first number in the text at or after `from`; 0 when there is none.
int NumberAfter(const std::string& text, std::size_t from) {
    while (from < text.size() && !std::isdigit(static_cast<unsigned char>(text[from]))) {
        ++from;
    }
    return from < text.size() ? std::atoi(text.c_str() + from) : 0;
}

bool Has(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

Screen MakeScreen(int width, int height, float hz, float inches, float density = 1.0f) {
    Screen screen;
    screen.width = width;
    screen.height = height;
    screen.refresh_hz = hz;
    screen.inches = inches;
    screen.density = density;
    return screen;
}

DeviceInfo Android(const char* maker, const char* model, const char* soc, const char* gpu, int ram_mb,
                   std::vector<Screen> screens, bool pad = true) {
    DeviceInfo info;
    info.manufacturer = maker;
    info.model = model;
    info.soc = soc;
    info.os = "android";
    info.os_version = "15";
    info.api_level = 35;
    info.screens = std::move(screens);
    info.touch = true;
    info.builtin_pad = pad;
    info.handheld = true;
    info.cpu_cores = 8;
    info.ram_mb = ram_mb;
    info.gpu = gpu;
    info.gpu_api = "OpenGL ES 3.2";
    return info;
}

DeviceInfo Pc(const char* os, const char* maker, const char* model, const char* soc, const char* gpu,
              int cores, int ram_mb, Screen screen, bool handheld) {
    DeviceInfo info;
    info.manufacturer = maker;
    info.model = model;
    info.soc = soc;
    info.os = os;
    info.screens = {screen};
    info.touch = handheld;
    info.builtin_pad = handheld;
    info.handheld = handheld;
    info.cpu_cores = cores;
    info.ram_mb = ram_mb;
    info.gpu = gpu;
    info.gpu_api = "4.6";
    return info;
}

} // namespace

const KnownDevice* FindKnown(const DeviceInfo& info) {
    const std::string text = Squash(info.manufacturer + " " + info.model);
    for (const KnownDevice& device : KNOWN) {
        const bool all = std::all_of(device.needles.begin(), device.needles.end(),
                                     [&](const char* needle) { return Has(text, needle); });
        if (all && !device.needles.empty()) {
            return &device;
        }
    }
    return nullptr;
}

std::optional<Tier> GpuTier(const std::string& renderer) {
    std::string gpu;
    for (const char c : renderer) {
        gpu.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (gpu.empty()) {
        return std::nullopt;
    }
    if (Has(gpu, "llvmpipe") || Has(gpu, "swiftshader") || Has(gpu, "softpipe") || Has(gpu, "basic render")) {
        return Low;
    }
    if (const auto at = gpu.find("adreno"); at != std::string::npos) {
        const int model = NumberAfter(gpu, at);
        return model >= 800 ? VeryHigh : model >= 730 ? High : model >= 650 ? Mid : Low;
    }
    if (Has(gpu, "immortalis")) {
        return High;
    }
    if (const auto at = gpu.find("mali-g"); at != std::string::npos) {
        const int model = NumberAfter(gpu, at);
        return model >= 700 ? High : model >= 600 || (model >= 76 && model < 100) ? Mid : Low;
    }
    if (Has(gpu, "mali") || Has(gpu, "powervr")) {
        return Low;
    }
    if (const auto at = gpu.find("xclipse"); at != std::string::npos) {
        return NumberAfter(gpu, at) >= 940 ? High : Mid;
    }
    if (Has(gpu, "apple m") || Has(gpu, "apple a1")) {
        return Has(gpu, "apple m") ? VeryHigh : High;
    }
    if (Has(gpu, "nvidia") || Has(gpu, "geforce") || Has(gpu, "quadro")) {
        return Has(gpu, "rtx") ? VeryHigh : Has(gpu, "gtx") ? High : Mid;
    }
    if (Has(gpu, "radeon") || Has(gpu, "amd")) {
        if (const auto at = gpu.find(" rx "); at != std::string::npos) {
            return NumberAfter(gpu, at) >= 6000 ? VeryHigh : High;
        }
        // Integrated: the 680M, 780M and 890M class is fast, the rest (Vega, the Deck) is not.
        for (const char* fast : {"680m", "760m", "780m", "880m", "890m", "8050s", "8060s"}) {
            if (Has(gpu, fast)) {
                return High;
            }
        }
        return Mid;
    }
    if (Has(gpu, "intel")) {
        return Has(gpu, "arc") ? High : Has(gpu, "iris") ? Mid : Low;
    }
    return std::nullopt;
}

} // namespace den::system::detail

namespace den::system {

std::optional<DeviceInfo> FakeDevice(std::string_view id) {
    using detail::Android;
    using detail::MakeScreen;
    using detail::Pc;
    if (id == "retroid-pocket-duo") {
        return Android("Retroid", "Retroid Pocket Duo", "SM7635", "Adreno (TM) 722", 11400,
                       {MakeScreen(1920, 1080, 120, 5.5f, 2.6f), MakeScreen(1240, 1080, 60, 3.9f, 2.6f)});
    }
    if (id == "ayn-thor") {
        return Android("AYN", "Thor", "SM8550", "Adreno (TM) 740", 15300,
                       {MakeScreen(1920, 1080, 120, 6.0f, 2.6f), MakeScreen(1240, 1080, 60, 3.9f, 2.6f)});
    }
    if (id == "retroid-pocket-5") {
        return Android("Retroid", "Retroid Pocket 5", "SM8250", "Adreno (TM) 650", 7600,
                       {MakeScreen(1920, 1080, 60, 5.5f, 2.6f)});
    }
    if (id == "phone-low") {
        return Android("Generic", "Phone", "MT6765", "PowerVR Rogue GE8320", 2900,
                       {MakeScreen(1600, 720, 60, 6.5f, 2.0f)}, false);
    }
    if (id == "tablet") {
        return Android("Generic", "Tab 11", "SM8475", "Adreno (TM) 730", 7600,
                       {MakeScreen(2560, 1600, 120, 11.0f, 2.0f)}, false);
    }
    if (id == "legion-go-s") {
        return Pc("linux", "LENOVO", "83L3 (Legion Go S 8APU1)", "AMD Ryzen Z2 Go", "AMD Radeon Graphics (radeonsi)",
                  8, 15200, MakeScreen(1920, 1200, 120, 8.0f, 1.5f), true);
    }
    if (id == "steam-deck") {
        return Pc("linux", "Valve", "Jupiter", "AMD Custom APU 0405", "AMD Custom GPU 0405 (radeonsi, vangogh)",
                  8, 14800, MakeScreen(1280, 800, 60, 7.0f), true);
    }
    if (id == "rog-ally") {
        return Pc("windows", "ASUSTeK COMPUTER INC.", "ROG Ally RC71L", "AMD Ryzen Z1 Extreme",
                  "AMD Radeon 780M Graphics", 16, 15600, MakeScreen(1920, 1080, 120, 7.0f, 1.5f), true);
    }
    if (id == "desktop") {
        return Pc("windows", "Gigabyte Technology Co., Ltd.", "B550 AORUS", "AMD Ryzen 9 5900X",
                  "NVIDIA GeForce RTX 4070/PCIe/SSE2", 24, 32700, MakeScreen(2560, 1440, 144, 27.0f), false);
    }
    if (id == "tv") {
        DeviceInfo info = Pc("linux", "Valve", "Jupiter", "AMD Custom APU 0405",
                             "AMD Custom GPU 0405 (radeonsi, vangogh)", 8, 14800,
                             MakeScreen(3840, 2160, 60, 55.0f), true);
        info.touch = false;
        return info;
    }
    return std::nullopt;
}

} // namespace den::system
