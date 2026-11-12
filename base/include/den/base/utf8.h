// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace den {

/// Reads one code point at `at` and moves `at` past it. Bad bytes come back as U+FFFD, one
/// byte at a time. Returns 0 at the end.
std::uint32_t NextCodePoint(std::string_view text, std::size_t& at);
void AppendCodePoint(std::string& out, std::uint32_t code);
std::size_t CodePointCount(std::string_view text);

} // namespace den
