// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/base/language.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace den {

namespace {

bool IsLetter(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

} // namespace

std::string NormalizeLanguage(std::string_view text) {
    // Drop "@modifier" and ".encoding".
    text = text.substr(0, text.find_first_of(".@"));
    std::string out;
    std::size_t part = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find_first_of("-_", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view piece = text.substr(start, end - start);
        if (part == 0) {
            if (piece.size() < 2 || piece.size() > 3 || !std::all_of(piece.begin(), piece.end(), IsLetter)) {
                return {};
            }
            for (char c : piece) {
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        } else if (part == 1) {
            // A script ("Hans") is skipped; a region is two letters or three digits.
            const bool region = (piece.size() == 2 && std::all_of(piece.begin(), piece.end(), IsLetter)) ||
                                (piece.size() == 3 && std::all_of(piece.begin(), piece.end(), [](char c) {
                                     return std::isdigit(static_cast<unsigned char>(c)) != 0;
                                 }));
            if (region) {
                out += '-';
                for (char c : piece) {
                    out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                break;
            }
        } else if (part == 2 && piece.size() == 2) {
            out += '-';
            for (char c : piece) {
                out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            break;
        }
        ++part;
        start = end + 1;
    }
    return out;
}

std::string LanguageBase(std::string_view code) {
    return std::string(code.substr(0, code.find('-')));
}

std::string MatchLanguage(const std::vector<std::string>& preferred,
                          const std::vector<std::string>& available) {
    for (const std::string& raw : preferred) {
        const std::string wanted = NormalizeLanguage(raw);
        if (wanted.empty() || LanguageBase(wanted) == "en") {
            continue;
        }
        for (const std::string& code : available) {
            if (NormalizeLanguage(code) == wanted) {
                return code;
            }
        }
        for (const std::string& code : available) {
            if (LanguageBase(NormalizeLanguage(code)) == LanguageBase(wanted)) {
                return code;
            }
        }
    }
    return {};
}

unsigned RetroLanguage(std::string_view code) {
    struct Entry {
        std::string_view code;
        unsigned id;
    };
    // libretro.h's RETRO_LANGUAGE_*; a whole code ("pt-PT") before its language ("pt").
    static constexpr Entry TABLE[] = {
        {"en-GB", 30}, {"ja", 1},  {"fr", 2},     {"es", 3},    {"de", 4},     {"it", 5},    {"nl", 6},
        {"pt-PT", 8},  {"pt", 7},  {"ru", 9},     {"ko", 10},   {"zh-TW", 11}, {"zh-HK", 11}, {"zh", 12},
        {"eo", 13},    {"pl", 14}, {"vi", 15},    {"ar", 16},   {"el", 17},    {"tr", 18},   {"sk", 19},
        {"fa", 20},    {"he", 21}, {"ast", 22},   {"fi", 23},   {"id", 24},    {"sv", 25},   {"uk", 26},
        {"cs", 27},    {"ca", 29}, {"hu", 31},    {"be", 32},   {"gl", 33},    {"nb", 34},   {"no", 34},
        {"ga", 35},    {"th", 36},
    };
    const std::string normal = NormalizeLanguage(code);
    for (const Entry& entry : TABLE) {
        if (entry.code == normal) {
            return entry.id;
        }
    }
    const std::string base = LanguageBase(normal);
    for (const Entry& entry : TABLE) {
        if (entry.code == base) {
            return entry.id;
        }
    }
    return 0;
}

} // namespace den
