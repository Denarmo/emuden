// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The launcher's texts by key, from assets/strings/en.json and, over it, the file of the
// language in "ui.language". A key without a text is shown as the key.

#pragma once

#include <format>
#include <string>
#include <string_view>

namespace den::app {

const std::string& Tr(std::string_view key);

/// A text with {} placeholders filled in.
template <typename... Args>
std::string TrF(std::string_view key, const Args&... args) {
    return std::vformat(Tr(key), std::make_format_args(args...));
}

} // namespace den::app
