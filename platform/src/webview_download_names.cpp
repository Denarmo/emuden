// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "webview_download_names.h"

#include <algorithm>

#include "den/base/files.h"
#include "den/platform/webview.h"

namespace den::pal {

namespace {

int Hex(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

std::string PercentDecode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && Hex(text[i + 1]) >= 0 && Hex(text[i + 2]) >= 0) {
            out.push_back(static_cast<char>(Hex(text[i + 1]) * 16 + Hex(text[i + 2])));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

/// The value of `key=` among the header's parameters, without its quotes; empty when absent.
std::string Parameter(const std::string& header, std::string_view key) {
    const std::string lower = ToLower(header);
    std::size_t at = 0;
    while ((at = lower.find(key, at)) != std::string::npos) {
        const bool starts = at == 0 || lower[at - 1] == ';' || lower[at - 1] == ' ' || lower[at - 1] == '\t';
        std::size_t value = at + key.size();
        while (value < header.size() && header[value] == ' ') {
            ++value;
        }
        if (!starts || value >= header.size() || header[value] != '=') {
            at += key.size();
            continue;
        }
        ++value;
        while (value < header.size() && header[value] == ' ') {
            ++value;
        }
        if (value < header.size() && header[value] == '"') {
            std::string out;
            for (std::size_t i = value + 1; i < header.size() && header[i] != '"'; ++i) {
                if (header[i] == '\\' && i + 1 < header.size()) {
                    ++i;
                }
                out.push_back(header[i]);
            }
            return out;
        }
        const std::size_t end = std::min(header.find(';', value), header.size());
        return Trim(header.substr(value, end - value));
    }
    return {};
}

} // namespace

std::string WebDownloadName(const std::string& disposition, const std::string& url) {
    // filename*=UTF-8''percent-encoded (RFC 5987) wins over filename=.
    if (std::string extended = Parameter(disposition, "filename*"); !extended.empty()) {
        if (const auto quote = extended.rfind('\''); quote != std::string::npos) {
            extended.erase(0, quote + 1);
        }
        if (const std::string name = PercentDecode(extended); !name.empty()) {
            return WebSafeFileName(name);
        }
    }
    if (const std::string name = Parameter(disposition, "filename"); !name.empty()) {
        return WebSafeFileName(name);
    }
    std::string path = url.substr(0, url.find_first_of("?#"));
    if (const auto scheme = path.find("://"); scheme != std::string::npos) {
        const auto slash = path.find('/', scheme + 3);
        path = slash == std::string::npos ? std::string() : path.substr(slash);
    }
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    return WebSafeFileName(PercentDecode(path.substr(path.rfind('/') + 1)));
}

std::string WebPatchExtension(std::string_view head) {
    struct Magic {
        std::string_view bytes;
        const char* extension;
    };
    static constexpr Magic KNOWN[] = {
        {"BPS1", ".bps"},  {"PATCH", ".ips"},          {"UPS1", ".ups"},           {"\xD6\xC3\xC4", ".xdelta"},
        {"APS1", ".aps"},  {"PPF", ".ppf"},            {"PK\x03\x04", ".zip"},     {"7z\xBC\xAF\x27\x1C", ".7z"},
    };
    for (const Magic& magic : KNOWN) {
        if (head.starts_with(magic.bytes)) {
            return magic.extension;
        }
    }
    return {};
}

} // namespace den::pal
