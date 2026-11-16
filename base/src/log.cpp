// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

#include "den/base/log.h"

namespace den::log {

namespace {
std::mutex g_mutex;
std::ofstream g_file;
constexpr Level MINIMUM = Level::Info;
const auto g_start = std::chrono::steady_clock::now();
} // namespace

void SetFile(const std::filesystem::path& file) {
    std::lock_guard lock(g_mutex);
    g_file.close();
    g_file.open(file, std::ios::binary | std::ios::trunc);
}

void Write(Level level, std::string_view tag, std::string_view text) {
    if (level < MINIMUM) {
        return;
    }
    static constexpr const char* NAMES[] = {"debug", "info", "warn", "error"};
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    const std::string line =
        std::format("[{:9.3f}] {} {}: {}\n", seconds, NAMES[static_cast<int>(level)], tag, text);
    std::lock_guard lock(g_mutex);
    std::fwrite(line.data(), 1, line.size(), stderr);
    if (g_file.is_open()) {
        g_file << line;
        g_file.flush();
    }
}

} // namespace den::log
