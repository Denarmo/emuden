// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Names of downloaded hacks: the title from the page and the file, the version, and how well
// one of the user's games fits them.

#include <algorithm>
#include <cctype>
#include <regex>

#include "den/base/files.h"
#include "den/library/downloads.h"

namespace den::library::downloads {

namespace {

// Lower case ASCII words; letters with accents lose them ("Pokémon" -> "pokemon").
std::vector<std::string> Words(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isalnum(c)) {
            word.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == 0xC3 && i + 1 < text.size()) {
            // Latin-1 letters with accents, as UTF-8.
            static constexpr std::string_view PLAIN = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
            const auto next = static_cast<unsigned char>(text[++i]);
            const int index = (next & 0x1F);
            if (next >= 0x80 && next <= 0xBF && index < static_cast<int>(PLAIN.size()) && PLAIN[index] != '/') {
                word.push_back(PLAIN[index]);
            } else {
                flush();
            }
        } else if (c != '\'') {
            flush();
        }
    }
    flush();
    return words;
}

bool IsVersionWord(const std::string& word) {
    static const std::regex VERSION(R"(v?\d+([a-z]|rc\d*|b\d*)?)");
    return std::regex_match(word, VERSION) || word == "version" || word == "ver";
}

// Words that say nothing about which game a hack is of.
bool IsCommon(const std::string& word) {
    static constexpr std::string_view COMMON[] = {
        "the", "of", "a", "and", "version", "edition", "game", "hack", "rom", "patch", "usa", "europe", "japan",
        "world", "en", "rev", "beta", "demo", "pokemon", "super", "mario", "zelda", "legend", "bros",
    };
    return std::find(std::begin(COMMON), std::end(COMMON), word) != std::end(COMMON) || IsVersionWord(word);
}

// Names of sites and file hosts, and words a page title adds around the hack's name.
bool IsSiteText(const std::string& segment) {
    const std::string lower = ToLower(segment);
    static constexpr std::string_view SITES[] = {
        "romhacking", "hackdex", "pokecommunity", "whackahack", "pokeharbor", "mediafire", "mega", "google drive",
        "dropbox", "github", "itch.io", "gamebanana", "archive.org", "internet archive", "forum", "download",
        "hacks", "translations", "homebrew", ".com", ".net", ".org", ".io", ".app",
    };
    for (const std::string_view site : SITES) {
        if (lower == site || (site.size() > 4 && lower.find(site) != std::string::npos)) {
            return true;
        }
    }
    return false;
}

// "Pokemon_Unbound_v2.1.1" -> "Pokemon Unbound": separators become spaces, tags in brackets
// and the version go.
std::string CleanName(std::string text) {
    static const std::regex BRACKETS(R"(\s*[\(\[\{][^\)\]\}]*[\)\]\}])");
    text = std::regex_replace(text, BRACKETS, " ");
    if (text.find(' ') == std::string::npos) {
        std::replace(text.begin(), text.end(), '_', ' ');
        std::replace(text.begin(), text.end(), '.', ' ');
    }
    std::replace(text.begin(), text.end(), '_', ' ');
    static const std::regex VERSION(R"((^|\s|-)(v|ver\.?|version)\s?\d+(\.\d+)*[a-z]?\b.*$)", std::regex::icase);
    text = std::regex_replace(text, VERSION, "");
    static const std::regex NUMBERS(R"(\s\d+(\.\d+)+[a-z]?\s*$)");
    text = std::regex_replace(text, NUMBERS, "");
    static const std::regex TAIL(R"(\s*(-|\s)\s*(patch|hack|rom ?hack|bps|ips|ups|xdelta)\s*$)", std::regex::icase);
    text = std::regex_replace(text, TAIL, "");
    static const std::regex SPACES(R"(\s+)");
    text = std::regex_replace(text, SPACES, " ");
    text = Trim(text);
    while (!text.empty() && (text.back() == '-' || text.back() == ':' || text.back() == ',')) {
        text.pop_back();
        text = Trim(text);
    }
    return text;
}

// The page title without the site's name: its segments around " - ", " | " and the like,
// the first that is not about a site or a file.
std::string TitleOfPage(const std::string& page_title, const std::string& file_name) {
    static const std::regex SEPARATOR(R"(\s+-\s+|\s*\|\s*|\s+::\s+)");
    std::string text = page_title;
    for (const std::string_view dash : {"\xE2\x80\x93", "\xE2\x80\x94", "\xC2\xB7"}) {
        for (std::size_t at = text.find(dash); at != std::string::npos; at = text.find(dash)) {
            text.replace(at, dash.size(), "|");
        }
    }
    std::sregex_token_iterator it(text.begin(), text.end(), SEPARATOR, -1), end;
    const std::string file_lower = ToLower(file_name);
    static const std::regex FILE_NAME(R"(\S\.[A-Za-z0-9]{2,4}$)");
    for (; it != end; ++it) {
        std::string segment = Trim(it->str());
        // A code host names a project "owner/project".
        if (const std::size_t slash = segment.find('/');
            slash != std::string::npos && segment.find(' ') == std::string::npos && slash + 1 < segment.size()) {
            segment = segment.substr(slash + 1);
            std::replace(segment.begin(), segment.end(), '-', ' ');
        }
        // A file host's page is named after the file.
        if (segment.empty() || IsSiteText(segment) || std::regex_search(segment, FILE_NAME) ||
            ToLower(segment).find(file_lower) != std::string::npos) {
            continue;
        }
        std::string name = CleanName(segment);
        // "Release 1.1.4", "Downloads": words about the page, not a name.
        const std::vector<std::string> words = Words(name);
        const bool says_nothing = std::all_of(words.begin(), words.end(), [](const std::string& word) {
            return IsCommon(word) || word == "release" || word == "releases" || word == "download" ||
                   word == "downloads" || word == "files" || word == "page" || word == "tag";
        });
        if (!says_nothing) {
            return name;
        }
    }
    return {};
}

bool Shares(const std::string& a, const std::string& b) {
    const std::vector<std::string> left = Words(a), right = Words(b);
    for (const std::string& word : left) {
        if (word.size() >= 3 && !IsCommon(word) && std::find(right.begin(), right.end(), word) != right.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string HackTitle(const std::string& page_title, const std::string& file_name) {
    std::string stem = file_name;
    if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos && dot > 0) {
        stem.resize(dot);
    }
    const std::string from_file = CleanName(stem);
    const std::string from_page = TitleOfPage(page_title, file_name);
    // A file name that is only a word like "patch" or a number says nothing.
    const std::vector<std::string> words = Words(from_file);
    const bool says_something = std::any_of(words.begin(), words.end(), [](const std::string& word) {
        return word.size() >= 3 && !IsCommon(word) && !std::all_of(word.begin(), word.end(), ::isdigit);
    });
    // The page's title names the hack better than a file name ("SMO64_v1.2"), unless it is a
    // sentence that has nothing in common with the file.
    if (!from_page.empty() && (!says_something || Shares(from_page, from_file) || Words(from_page).size() <= 8)) {
        return from_page;
    }
    if (says_something) {
        return from_file;
    }
    return from_page.empty() ? (from_file.empty() ? std::string("Hack") : from_file) : from_page;
}

std::string HackVersion(const std::string& page_title, const std::string& file_name) {
    static const std::regex MARKED(R"((?:^|[^a-z0-9])(?:v|ver\.?|version)\s?(\d+(?:\.\d+)*[a-z]?))", std::regex::icase);
    static const std::regex DOTTED(R"((?:^|[^a-z0-9.])(\d+\.\d+(?:\.\d+)*[a-z]?)(?:[^0-9]|$))", std::regex::icase);
    for (const std::string* text : {&file_name, &page_title}) {
        std::string stem = *text;
        if (text == &file_name) {
            if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
                stem.resize(dot);
            }
        }
        std::smatch match;
        if (std::regex_search(stem, match, MARKED) || std::regex_search(stem, match, DOTTED)) {
            return match[1].str();
        }
    }
    return {};
}

int NameScore(const std::string& game_name, const std::string& text) {
    const std::vector<std::string> game = Words(game_name);
    const std::vector<std::string> words = Words(text);
    std::string joined;
    for (const std::string& word : words) {
        joined += word;
    }
    int score = 0;
    for (std::size_t i = 0; i < game.size(); ++i) {
        if (IsCommon(game[i]) || game[i].size() < 3) {
            continue;
        }
        const bool alone = std::find(words.begin(), words.end(), game[i]) != words.end();
        // "Fire Red" in the game, "FireRed" in the text.
        const bool paired = i + 1 < game.size() && joined.find(game[i] + game[i + 1]) != std::string::npos;
        if (alone || paired) {
            ++score;
        }
    }
    return score;
}

} // namespace den::library::downloads
