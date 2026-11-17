// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/update/version.h"

#include <algorithm>
#include <cctype>

namespace den::update {

namespace {

bool AllDigits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// A numeric part: digits without a leading zero, small enough to hold.
std::optional<unsigned long long> Number(std::string_view text) {
    if (!AllDigits(text) || (text.size() > 1 && text[0] == '0') || text.size() > 18) {
        return std::nullopt;
    }
    unsigned long long value = 0;
    for (const char c : text) {
        value = value * 10 + static_cast<unsigned long long>(c - '0');
    }
    return value;
}

std::vector<std::string_view> Split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            return parts;
        }
        start = end + 1;
    }
}

bool Identifier(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-';
    });
}

} // namespace

std::optional<Version> ParseVersion(std::string_view text) {
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) {
        text.remove_prefix(1);
    }
    if (const std::size_t plus = text.find('+'); plus != std::string_view::npos) {
        const std::string_view build = text.substr(plus + 1);
        for (const std::string_view part : Split(build, '.')) {
            if (!Identifier(part)) {
                return std::nullopt;
            }
        }
        text = text.substr(0, plus);
    }
    std::string_view pre;
    if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
        pre = text.substr(dash + 1);
        text = text.substr(0, dash);
        if (pre.empty()) {
            return std::nullopt;
        }
    }
    const std::vector<std::string_view> core = Split(text, '.');
    if (core.size() != 3) {
        return std::nullopt;
    }
    Version version;
    const auto major = Number(core[0]), minor = Number(core[1]), patch = Number(core[2]);
    if (!major || !minor || !patch) {
        return std::nullopt;
    }
    version.major = *major;
    version.minor = *minor;
    version.patch = *patch;
    if (!pre.empty()) {
        for (const std::string_view part : Split(pre, '.')) {
            if (!Identifier(part) || (AllDigits(part) && !Number(part))) {
                return std::nullopt;
            }
            version.pre.emplace_back(part);
        }
    }
    return version;
}

int CompareVersions(const Version& a, const Version& b) {
    if (a.major != b.major) {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor) {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch) {
        return a.patch < b.patch ? -1 : 1;
    }
    // A release comes after its pre-releases.
    if (a.pre.empty() || b.pre.empty()) {
        return a.pre.empty() == b.pre.empty() ? 0 : a.pre.empty() ? 1 : -1;
    }
    for (std::size_t i = 0; i < std::min(a.pre.size(), b.pre.size()); ++i) {
        const std::string& x = a.pre[i];
        const std::string& y = b.pre[i];
        if (x == y) {
            continue;
        }
        const bool x_number = AllDigits(x), y_number = AllDigits(y);
        if (x_number && y_number) {
            // No leading zeros: the longer number is the larger.
            if (x.size() != y.size()) {
                return x.size() < y.size() ? -1 : 1;
            }
            return x < y ? -1 : 1;
        }
        if (x_number != y_number) {
            return x_number ? -1 : 1; // numbers sort before words
        }
        return x < y ? -1 : 1;
    }
    return a.pre.size() == b.pre.size() ? 0 : a.pre.size() < b.pre.size() ? -1 : 1;
}

bool IsNewer(std::string_view candidate, std::string_view current) {
    const auto a = ParseVersion(candidate);
    const auto b = ParseVersion(current);
    return a && b && CompareVersions(*a, *b) > 0;
}

bool CanUpdateFrom(std::string_view running) {
    const auto version = ParseVersion(running);
    return version && (version->major != 0 || version->minor != 0 || version->patch != 0);
}

} // namespace den::update
