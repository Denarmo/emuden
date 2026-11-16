// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Names of downloaded games and hacks: the title from the page, the file, the archive and the
// game's header, the version, and how well one of the user's games fits them.

#include <algorithm>
#include <cctype>
#include <regex>

#include "den/base/files.h"
#include "den/base/names.h"
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

// "Pokemon_Unbound_v2.1.1" -> "Pokemon Unbound": separators become spaces; tags in brackets,
// a site's name in front, hashes, "patched" and the version go.
std::string CleanName(std::string text) {
    text = PercentDecode(text);
    static const std::regex SITE(R"(^\s*(www\.)?[a-z0-9-]+\.(com|net|org|io|info)(?=[\s_-])\s*[-_]?\s*)",
                                 std::regex::icase);
    text = std::regex_replace(text, SITE, "");
    static const std::regex HASH(R"((^|[\s_.-])(?=[0-9a-f]*[0-9])(?=[0-9a-f]*[a-f])[0-9a-f]{8,}(?=$|[\s_.-]))",
                                 std::regex::icase);
    text = std::regex_replace(text, HASH, " ");
    static const std::regex PATCHED(R"((^|[\s_.-])(un)?patched(?=$|[\s_.-]))", std::regex::icase);
    text = std::regex_replace(text, PATCHED, " ");
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

// The page names the same thing as a file: a word in common, or the file's run-together word
// in the page's words ("pokemonunbound" in "Pokemon Unbound").
bool Fits(const std::string& page, const std::string& file) {
    if (Shares(page, file)) {
        return true;
    }
    std::string joined;
    for (const std::string& word : Words(page)) {
        joined += word;
    }
    const std::vector<std::string> words = Words(file);
    return std::any_of(words.begin(), words.end(), [&](const std::string& word) {
        return word.size() >= 6 && !IsCommon(word) && joined.find(word) != std::string::npos;
    });
}

bool SaysSomething(const std::string& name) {
    const std::vector<std::string> words = Words(name);
    return std::any_of(words.begin(), words.end(), [](const std::string& word) {
        return word.size() >= 3 && !IsCommon(word) && !std::all_of(word.begin(), word.end(), ::isdigit);
    });
}

// "gen3evolved" -> "Gen 3 Evolved": an all lower-case name gets spaces between letters and
// digits and capitals; a name with capitals of its own is left as its author wrote it.
std::string Finish(std::string name) {
    if (std::any_of(name.begin(), name.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)); })) {
        return name;
    }
    std::string spaced;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        const auto prev = i > 0 ? static_cast<unsigned char>(name[i - 1]) : ' ';
        if ((std::isalpha(prev) && std::isdigit(c)) || (std::isdigit(prev) && std::isalpha(c))) {
            spaced.push_back(' ');
        }
        spaced.push_back(static_cast<char>(c));
    }
    static constexpr std::string_view SMALL[] = {"of", "the", "and", "a", "an", "in", "on", "to", "for"};
    std::string out;
    std::size_t at = 0;
    while (at < spaced.size()) {
        const std::size_t end = std::min(spaced.find(' ', at), spaced.size());
        std::string word = spaced.substr(at, end - at);
        const bool small = std::find(std::begin(SMALL), std::end(SMALL), word) != std::end(SMALL);
        if (!word.empty() && (!small || out.empty())) {
            word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
        }
        out += (out.empty() ? "" : " ") + word;
        at = end + 1;
    }
    return out;
}

std::string StemOf(const std::string& file_name) {
    const std::size_t dot = file_name.rfind('.');
    return dot != std::string::npos && dot > 0 ? file_name.substr(0, dot) : file_name;
}

} // namespace

std::string HackTitle(const std::string& page_title, const std::string& file_name) {
    const std::string stem = StemOf(file_name);
    const std::string from_file = Finish(CleanName(stem));
    const std::string from_page = Finish(TitleOfPage(page_title, file_name));
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

std::string GameName(const NameSources& sources) {
    const std::string from_file = Finish(CleanName(StemOf(sources.file_name)));
    const std::string from_archive =
        sources.archive_name.empty() ? std::string() : Finish(CleanName(StemOf(sources.archive_name)));
    std::string from_page = TitleOfPage(sources.page_title, sources.file_name);
    if (!sources.archive_name.empty() && ToLower(from_page) == ToLower(CleanName(StemOf(sources.archive_name)))) {
        from_page.clear(); // a file host's page named after the archive
    }
    from_page = Finish(from_page);
    const bool file_says = SaysSomething(from_file), archive_says = SaysSomething(from_archive);
    if (!from_page.empty() &&
        (Fits(from_page, from_file) || Fits(from_page, from_archive) || (!file_says && !archive_says))) {
        return from_page;
    }
    if (file_says) {
        return from_file;
    }
    if (archive_says) {
        return from_archive;
    }
    if (!sources.header_title.empty()) {
        return sources.header_title;
    }
    return !from_file.empty() ? from_file : !from_archive.empty() ? from_archive : std::string("Game");
}

bool TidyFileName(const std::string& file_name) {
    const std::string stem = StemOf(file_name);
    if (stem.empty() || stem.front() == '.' || stem.find_first_of("_%") != std::string::npos) {
        return false;
    }
    static const std::regex MESSY(
        R"((\(\d+\)$)|( - copy$)|(\[[^\]]*\.[a-z]{2,4}\])|(\b(un)?patched\b)|(\bv(er)?\.?\s?\d)|)"
        R"(((^|[\s.-])(?=[0-9a-f]*[0-9])(?=[0-9a-f]*[a-f])[0-9a-f]{8,}($|[\s.-])))",
        std::regex::icase);
    if (std::regex_search(stem, MESSY)) {
        return false;
    }
    // A name of one lower-case word ("gen3evolved") is a file name, not a title.
    const bool upper = std::any_of(stem.begin(), stem.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)); });
    return upper && (stem.find(' ') != std::string::npos || stem.find('-') == std::string::npos);
}

std::string HeaderName(const std::string& header_title) {
    std::string name;
    for (const char c : header_title) {
        name.push_back(std::isprint(static_cast<unsigned char>(c)) ? c : ' ');
    }
    name = Trim(std::regex_replace(name, std::regex(R"(\s+)"), " "));
    return SaysSomething(name) ? Finish(ToLower(name)) : std::string();
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
