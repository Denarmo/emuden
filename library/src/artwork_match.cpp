// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>

#include "den/base/files.h"
#include "den/base/names.h"
#include "den/library/artwork.h"
#include "den/library/names.h"

namespace den::library {

namespace {

int ThumbnailScore(std::string_view candidate, std::string_view original, int disc) {
    const std::string name = ToLower(candidate);
    const std::string wanted = ToLower(original);
    int score = 0;

    const int candidate_disc = ParseGameName(candidate).disc;
    if (candidate_disc == disc) {
        score += 40;
    } else if (candidate_disc != 0 && disc != 0) {
        score -= 60;
    }

    // The ROM's own region first, then the usual order.
    static constexpr std::array<std::string_view, 4> REGIONS{"usa", "world", "europe", "japan"};
    int preference = 20;
    for (const std::string_view region : REGIONS) {
        if (name.find(region) != std::string::npos) {
            score += wanted.find(region) != std::string::npos ? 60 : preference;
            break;
        }
        preference -= 5;
    }

    for (const std::string_view tag :
         {"(beta", "(proto", "(demo", "(sample", "(pirate", "(unl", "(aftermarket", "(kiosk"}) {
        if (name.find(tag) != std::string::npos && wanted.find(tag) == std::string::npos) {
            score -= 25;
        }
    }
    // Among equals, the plainest name.
    return score * 100 - static_cast<int>(std::min<std::size_t>(candidate.size(), 99));
}

int EditDistance(std::string_view a, std::string_view b) {
    std::vector<int> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        row[j] = static_cast<int>(j);
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// Two match keys that are very likely the same game spelled a little differently: one letter
// off in a name of 12, two from 16, three from 24. Names that differ in their numbers, Arabic
// or Roman ("X4" and "X5", "... VII" and "... VIII"), never are.
bool IsNearMiss(std::string_view a, std::string_view b) {
    const std::size_t shorter = std::min(a.size(), b.size());
    const std::size_t longer = std::max(a.size(), b.size());
    const int allowed = shorter >= 24 ? 3 : shorter >= 16 ? 2 : shorter >= 12 ? 1 : 0;
    if (allowed == 0 || longer - shorter > static_cast<std::size_t>(allowed)) {
        return false;
    }
    const auto digits = [](std::string_view text) {
        std::string out;
        for (const char c : text) {
            if (c >= '0' && c <= '9') {
                out.push_back(c);
            }
        }
        return out;
    };
    const auto roman_tail = [](std::string_view text) {
        const std::size_t last = text.find_last_not_of("ivx");
        return last == std::string_view::npos ? text : text.substr(last + 1);
    };
    if (digits(a) != digits(b) || roman_tail(a) != roman_tail(b)) {
        return false;
    }
    return EditDistance(a, b) <= allowed;
}

// Lower-case words of letters and digits.
std::vector<std::string> Words(std::string_view text) {
    std::vector<std::string> out;
    std::string word;
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            word.push_back(raw);
        } else if (c >= 'A' && c <= 'Z') {
            word.push_back(static_cast<char>(c + 32));
        } else if (!word.empty()) {
            out.push_back(std::move(word));
            word.clear();
        }
    }
    if (!word.empty()) {
        out.push_back(std::move(word));
    }
    return out;
}

// Words of a box name that do not tell one game of a series from another.
constexpr std::array<std::string_view, 6> FILLER_WORDS{"version", "edition", "the", "of", "and", "a"};

// Well-known hacks whose ROM does not carry the name of the game they were made from.
struct KnownHack {
    std::string_view key;  // MatchKey of the hack's name
    std::string_view base; // words of the original's box name
};
constexpr KnownHack KNOWN_HACKS[] = {
    {"pokemonbrown", "red"},          {"pokemonprism", "crystal"},
    {"pokemoncrystalclear", "crystal"}, {"pokemonradicalred", "firered"},
    {"pokemonunbound", "firered"},    {"pokemongaia", "firered"},
    {"pokemonclover", "firered"},     {"pokemonglazed", "emerald"},
    {"pokemonlightplatinum", "ruby"}, {"pokemonrocketedition", "firered"},
};

} // namespace

// The thumbnail collection replaces these in file names.
std::string ThumbnailFileName(std::string_view name) {
    std::string out(name);
    for (char& c : out) {
        if (std::string_view("&*/:`<>?\\|\"").find(c) != std::string_view::npos) {
            c = '_';
        }
    }
    return out;
}

std::string UrlEncodeSegment(std::string_view segment) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string out;
    for (const char raw : segment) {
        const auto c = static_cast<unsigned char>(raw);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            out.push_back(raw);
        } else {
            out.push_back('%');
            out.push_back(HEX[c >> 4]);
            out.push_back(HEX[c & 0xF]);
        }
    }
    return out;
}

std::vector<std::string> ParseThumbnailIndex(std::string_view html) {
    std::vector<std::string> names;
    constexpr std::string_view MARK = "href=\"";
    std::size_t at = 0;
    while ((at = html.find(MARK, at)) != std::string_view::npos) {
        at += MARK.size();
        const std::size_t end = html.find('"', at);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view link = html.substr(at, end - at);
        at = end;
        if (link.find('/') != std::string_view::npos || link.size() < 5 ||
            ToLower(link.substr(link.size() - 4)) != ".png") {
            continue;
        }
        names.push_back(PercentDecode(link.substr(0, link.size() - 4)));
    }
    return names;
}

ThumbnailIndex BuildThumbnailIndex(std::vector<std::string> names) {
    ThumbnailIndex index;
    index.names = std::move(names);
    index.keys.reserve(index.names.size());
    for (const std::string& name : index.names) {
        index.keys.push_back(MatchKey(name));
    }
    return index;
}

std::string MatchThumbnail(const ThumbnailIndex& index, std::string_view original_name) {
    const std::string exact = ThumbnailFileName(original_name);
    const std::string key = MatchKey(original_name);
    const int disc = ParseGameName(original_name).disc;
    const std::string* best = nullptr;
    int best_score = 0;
    for (std::size_t i = 0; i < index.names.size(); ++i) {
        const std::string& name = index.names[i];
        if (name == exact) {
            return name;
        }
        if (key.empty() || index.keys[i] != key) {
            continue;
        }
        const int score = ThumbnailScore(name, original_name, disc);
        if (!best || score > best_score) {
            best = &name;
            best_score = score;
        }
    }
    if (best || key.empty()) {
        return best ? *best : std::string();
    }
    // No name is this game exactly: accept a near miss ("Explorers of Skies" for "of Sky").
    int best_distance = 0;
    for (std::size_t i = 0; i < index.names.size(); ++i) {
        if (!IsNearMiss(key, index.keys[i])) {
            continue;
        }
        const int distance = EditDistance(key, index.keys[i]);
        const int score = ThumbnailScore(index.names[i], original_name, disc);
        if (!best || distance < best_distance || (distance == best_distance && score > best_score)) {
            best = &index.names[i];
            best_distance = distance;
            best_score = score;
        }
    }
    return best ? *best : std::string();
}

std::string MatchBaseGame(const ThumbnailIndex& index, std::string_view original_name,
                          std::string_view header_title) {
    const std::vector<std::string> file_words = Words(ParseGameName(original_name).base);
    if (file_words.empty() || file_words.front().size() < 3) {
        return {};
    }
    const std::string& series = file_words.front();
    std::vector<std::string> have = file_words;
    for (std::string& word : Words(header_title)) {
        have.push_back(std::move(word));
    }
    const std::string key = MatchKey(original_name);
    for (const KnownHack& hack : KNOWN_HACKS) {
        if (key == hack.key) {
            for (std::string& word : Words(hack.base)) {
                have.push_back(std::move(word));
            }
        }
    }
    const auto is_number = [](const std::string& word) {
        return word.find_first_not_of("0123456789") == std::string::npos;
    };
    // A word of the box is known when the ROM names it, in full or cut short as headers do.
    const auto known = [&](const std::string& word) {
        return std::any_of(have.begin(), have.end(), [&](const std::string& mine) {
            return mine == word || (mine.size() >= 4 && !is_number(word) && word.starts_with(mine));
        });
    };

    const int disc = ParseGameName(original_name).disc;
    const std::string* best = nullptr;
    std::size_t best_words = 0;
    int best_score = 0;
    for (std::size_t i = 0; i < index.names.size(); ++i) {
        if (!index.keys[i].starts_with(series)) {
            continue;
        }
        std::vector<std::string> words = Words(ParseGameName(index.names[i]).base);
        std::erase_if(words, [](const std::string& word) {
            return std::find(FILLER_WORDS.begin(), FILLER_WORDS.end(), word) != FILLER_WORDS.end();
        });
        // The series alone ("Pokemon") says nothing about which of its games this is.
        if (words.size() < 2 || words.front() != series ||
            !std::all_of(words.begin() + 1, words.end(), known)) {
            continue;
        }
        // "Mario Golf 2" is not "Mario Golf".
        const bool numbers_kept =
            std::all_of(file_words.begin(), file_words.end(), [&](const std::string& word) {
                return !is_number(word) || std::find(words.begin(), words.end(), word) != words.end();
            });
        if (!numbers_kept) {
            continue;
        }
        const int score = ThumbnailScore(index.names[i], original_name, disc);
        if (!best || words.size() > best_words || (words.size() == best_words && score > best_score)) {
            best = &index.names[i];
            best_words = words.size();
            best_score = score;
        }
    }
    return best ? *best : std::string();
}

} // namespace den::library
