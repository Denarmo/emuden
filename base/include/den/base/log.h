// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace den::log {

enum class Level { Debug, Info, Warning, Error };

/// Lines go to stderr and, once set, to this file too (truncated when opened).
void SetFile(const std::filesystem::path& file);
void SetMinimum(Level level);
void Write(Level level, std::string_view tag, std::string_view text);

template <typename... Args>
void Debug(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Debug, tag, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void Info(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Info, tag, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void Warn(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Warning, tag, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void Error(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Error, tag, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace den::log
