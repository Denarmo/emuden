// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/base/names.h"

#include <algorithm>

#include "den/base/files.h"

namespace den {

namespace {

int HexValue(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

void TrimEnd(std::string& text) {
    while (!text.empty() && (text.back() == '.' || text.back() == ' ')) {
        text.pop_back();
    }
}

} // namespace

std::string PathKey(const std::filesystem::path& path) {
    std::string key = ToLower(PathToUtf8(path.lexically_normal()));
    std::replace(key.begin(), key.end(), '\\', '/');
    while (key.size() > 1 && key.back() == '/' && !(key.size() == 3 && key[1] == ':')) {
        key.pop_back();
    }
    return key;
}

bool IsUnderKey(const std::filesystem::path& file, std::string_view folder_key) {
    const std::string key = PathKey(file);
    if (key.size() <= folder_key.size() || !key.starts_with(folder_key)) {
        return false;
    }
    return folder_key.ends_with('/') || key[folder_key.size()] == '/';
}

bool IsRefusedNameChar(char c, bool allow_slash) {
    if (static_cast<unsigned char>(c) < 0x20) {
        return true;
    }
    if (c == '/') {
        return !allow_slash;
    }
    return std::string_view("<>:\"\\|?*").find(c) != std::string_view::npos;
}

std::string SafeFileName(std::string_view name, const FileNameRules& rules) {
    std::string out;
    for (char c : name) {
        if (IsRefusedNameChar(c, rules.allow_slash)) {
            if (rules.replacement == '\0') {
                continue;
            }
            c = rules.replacement;
        }
        if (rules.collapse_spaces && c == ' ') {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
            continue;
        }
        out.push_back(c);
    }
    const std::size_t front = out.find_first_not_of(rules.trim_front);
    out.erase(0, rules.trim_front.empty() ? 0 : std::min(front, out.size()));
    if (rules.byte_cut) {
        TrimEnd(out);
        if (rules.max_bytes != 0 && out.size() > rules.max_bytes) {
            out.resize(rules.max_bytes);
        }
    } else {
        if (rules.max_bytes != 0 && out.size() > rules.max_bytes) {
            std::size_t cut = rules.max_bytes;
            // Not in the middle of a UTF-8 character.
            while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
                --cut;
            }
            out.resize(cut);
        }
        TrimEnd(out);
    }
    return out.empty() ? std::string(rules.fallback) : out;
}

std::string PercentDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && HexValue(text[i + 1]) >= 0 && HexValue(text[i + 2]) >= 0) {
            out.push_back(static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2])));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

} // namespace den
