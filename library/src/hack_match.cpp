// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>

#include "den/base/files.h"
#include "den/library/hack_covers.h"
#include "den/library/names.h"

namespace den::library {

namespace {

constexpr std::array<std::string_view, 10> FILLER{"version", "edition", "the", "of",  "and",
                                                  "a",       "hack",    "rom", "romhack", "by"};

// The name without what stands in brackets or between tildes ("~Hack~ Pokemon Brown [v2]").
std::string DropTags(std::string_view text) {
    std::string out;
    int depth = 0;
    bool tilde = false;
    for (const char c : text) {
        if (c == '~') {
            tilde = !tilde;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            depth = std::max(0, depth - 1);
            out.push_back(' ');
        } else if (depth == 0 && !tilde) {
            out.push_back(c);
        }
    }
    return out;
}

// Lower-case words without accents, filler words left out.
std::vector<std::string> NameWords(std::string_view name) {
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&] {
        // MatchKey folds accents and case; it sees one word, so it has no tags to drop.
        std::string key = MatchKey(word);
        word.clear();
        if (!key.empty() && std::find(FILLER.begin(), FILLER.end(), key) == FILLER.end()) {
            words.push_back(std::move(key));
        }
    };
    for (const char c : DropTags(name)) {
        const auto u = static_cast<unsigned char>(c);
        const bool letter = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                            u >= 0x80;
        if (letter) {
            word.push_back(c);
        } else if (c != '\'') {
            flush();
        }
    }
    flush();
    return words;
}

std::string Joined(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& word : words) {
        out += word;
    }
    return out;
}

bool Has(const std::vector<std::string>& words, const std::string& word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}

} // namespace

std::vector<std::string> DefaultHackCollections() {
    return {"tttrash/pokehackcovers"};
}

bool HasHackTag(std::string_view rom_name) {
    const std::string lower = ToLower(std::string(rom_name));
    for (const std::string_view tag : {"(hack", "[hack", "(romhack", "[romhack", "(rom hack", "~hack~"}) {
        if (lower.find(tag) != std::string::npos) {
            return true;
        }
    }
    return false;
}

NameMatch MatchHackName(std::string_view rom_name, std::string_view title,
                        std::string_view header_title) {
    const std::vector<std::string> rom = NameWords(rom_name);
    const std::vector<std::string> theirs = NameWords(title);
    if (rom.empty() || theirs.empty()) {
        return {};
    }
    if (Joined(rom) == Joined(theirs)) {
        return {100, true};
    }
    std::vector<std::string> missing; // words of the ROM the title lacks
    std::vector<std::string> extra;   // words of the title the ROM lacks
    std::size_t common = 0;
    for (const std::string& word : rom) {
        if (Has(theirs, word)) {
            ++common;
        } else {
            missing.push_back(word);
        }
    }
    for (const std::string& word : theirs) {
        if (!Has(rom, word)) {
            extra.push_back(word);
        }
    }
    // The series alone ("Pokemon") says nothing.
    const bool beyond_series =
        common >= 2 || (common == 1 && (rom.size() == 1 || !Has(theirs, rom.front())));
    if (!beyond_series) {
        return {};
    }
    if (missing.empty()) {
        // The title says more than the file name: fine when the cartridge says it too.
        const std::vector<std::string> header = NameWords(header_title);
        const bool named = std::all_of(extra.begin(), extra.end(), [&](const std::string& word) {
            return std::any_of(header.begin(), header.end(), [&](const std::string& mine) {
                return mine == word || (mine.size() >= 4 && word.starts_with(mine));
            });
        });
        return named ? NameMatch{90, true} : NameMatch{60, false};
    }
    return extra.empty() ? NameMatch{40, false} : NameMatch{30, false};
}

} // namespace den::library
