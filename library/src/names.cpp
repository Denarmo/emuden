// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>

#include "den/base/files.h"
#include "den/library/names.h"
#include "den/library/systems.h"

namespace den::library {

namespace {

// Extensions that are dropped from the end of a name, besides those of the consoles' ROMs. A
// name such as "Dr. Mario" keeps its dot.
constexpr std::array<std::string_view, 12> OTHER_EXTENSIONS{
    "png", "zip", "7z", "rar", "nsp", "xci", "nca", "nro", "nso", "nup", "exe", "lnk"};

bool IsKnownExtension(std::string_view extension) {
    if (std::find(OTHER_EXTENSIONS.begin(), OTHER_EXTENSIONS.end(), extension) !=
        OTHER_EXTENSIONS.end()) {
        return true;
    }
    const std::vector<std::string>& roms = BuiltinRomExtensions();
    return std::find(roms.begin(), roms.end(), extension) != roms.end();
}

constexpr std::array<std::string_view, 12> ARTICLES{"The", "A",   "An",  "Le",  "La",  "Les",
                                                    "El",  "Los", "Las", "Der", "Die", "Das"};

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

std::string CollapseSpaces(std::string_view text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            space = !out.empty();
        } else {
            if (space) {
                out.push_back(' ');
            }
            space = false;
            out.push_back(c);
        }
    }
    return out;
}

std::string_view DropExtension(std::string_view name) {
    // PICO-8 carts are named "<game>.p8.png".
    for (int pass = 0; pass < 2; ++pass) {
        const std::size_t dot = name.rfind('.');
        if (dot == std::string_view::npos || dot == 0) {
            break;
        }
        const std::string extension = ToLower(name.substr(dot + 1));
        if (!IsKnownExtension(extension)) {
            break;
        }
        name = name.substr(0, dot);
        if (extension != "png") {
            break;
        }
    }
    return name;
}

// The disc number of a tag such as "Disc 2", "Disk 1 of 3", "CD2" or "Disc B"; 0 if it is not one.
int DiscNumber(std::string_view tag) {
    const std::string lower = ToLower(tag);
    std::string_view rest;
    for (const std::string_view word : {"disc", "disk", "cd"}) {
        if (lower.starts_with(word)) {
            rest = std::string_view(lower).substr(word.size());
            break;
        }
    }
    if (rest.data() == nullptr) {
        return 0;
    }
    while (!rest.empty() && rest.front() == ' ') {
        rest.remove_prefix(1);
    }
    if (rest.empty()) {
        return 0;
    }
    if (IsDigit(rest.front())) {
        int value = 0;
        while (!rest.empty() && IsDigit(rest.front()) && value < 100) {
            value = value * 10 + (rest.front() - '0');
            rest.remove_prefix(1);
        }
        return rest.empty() || rest.front() == ' ' ? value : 0;
    }
    // "Disc A"
    return rest.size() == 1 && rest.front() >= 'a' && rest.front() <= 'h' ? rest.front() - 'a' + 1
                                                                          : 0;
}

// "Legend of Zelda, The - Ocarina of Time" -> "The Legend of Zelda - Ocarina of Time"
std::string MoveArticle(std::string name) {
    for (const std::string_view article : ARTICLES) {
        const std::string needle = ", " + std::string(article);
        std::size_t at = 0;
        while ((at = name.find(needle, at)) != std::string::npos) {
            const std::size_t end = at + needle.size();
            const bool ends_part = end == name.size() || name.compare(end, 3, " - ") == 0 ||
                                   name.compare(end, 2, ": ") == 0;
            if (ends_part) {
                name.erase(at, needle.size());
                return std::string(article) + " " + name;
            }
            at = end;
        }
    }
    return name;
}

// Latin-1 letters with accents (UTF-8 C3 80..BF) to their plain letter, 0 for anything else.
char FoldAccent(unsigned char second) {
    static constexpr char TABLE[] = "aaaaaaaceeeeiiiidnooooo ouuuuy saaaaaaaceeeeiiiidnooooo ouuuuy y";
    const char folded = second >= 0x80 && second <= 0xBF ? TABLE[second - 0x80] : ' ';
    return folded == ' ' ? '\0' : folded;
}

} // namespace

ParsedName ParseGameName(std::string_view raw) {
    ParsedName out;
    std::string name(DropExtension(Trim(raw)));
    std::replace(name.begin(), name.end(), '_', ' ');

    std::string kept;
    std::string tag;
    char closing = 0;
    for (const char c : name) {
        if (closing == 0) {
            if (c == '(' || c == '[') {
                closing = c == '(' ? ')' : ']';
                tag.clear();
                kept.push_back(' ');
            } else {
                kept.push_back(c);
            }
        } else if (c == closing) {
            closing = 0;
            if (const int disc = DiscNumber(Trim(tag)); disc != 0) {
                out.disc = disc;
            }
        } else {
            tag.push_back(c);
        }
    }
    kept = CollapseSpaces(kept);
    while (!kept.empty() && (kept.back() == '-' || kept.back() == ' ' || kept.back() == ',')) {
        kept.pop_back();
    }
    kept = MoveArticle(std::move(kept));
    if (kept.empty()) {
        kept = CollapseSpaces(name);
    }
    out.base = kept;
    out.display = out.disc != 0 ? kept + " (Disc " + std::to_string(out.disc) + ")" : kept;
    return out;
}

std::string NameBeforeDisc(std::string_view raw) {
    std::string name(DropExtension(Trim(raw)));
    std::replace(name.begin(), name.end(), '_', ' ');
    std::size_t tag_start = 0;
    char closing = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (closing == 0) {
            if (c == '(' || c == '[') {
                closing = c == '(' ? ')' : ']';
                tag_start = i;
            }
        } else if (c == closing) {
            closing = 0;
            if (DiscNumber(Trim(std::string_view(name).substr(tag_start + 1, i - tag_start - 1))) !=
                0) {
                name.resize(tag_start);
                break;
            }
        }
    }
    name = CollapseSpaces(name);
    while (!name.empty() && (name.back() == '-' || name.back() == ' ' || name.back() == ',')) {
        name.pop_back();
    }
    return name;
}

std::string CleanDisplayName(std::string_view raw) {
    return ParseGameName(raw).display;
}

std::string CleanStoreTitle(std::string_view title) {
    std::string text(title);
    // UTF-8 for the trademark, registered and copyright signs.
    for (const std::string_view sign : {"\xE2\x84\xA2", "\xC2\xAE", "\xC2\xA9"}) {
        std::size_t at = 0;
        while ((at = text.find(sign, at)) != std::string::npos) {
            text.erase(at, sign.size());
        }
    }
    return CollapseSpaces(text);
}

std::string MatchKey(std::string_view raw) {
    const std::string base = ParseGameName(raw).base;
    std::string key;
    for (std::size_t i = 0; i < base.size(); ++i) {
        const auto c = static_cast<unsigned char>(base[i]);
        if (c == 0xC3 && i + 1 < base.size()) {
            if (const char folded = FoldAccent(static_cast<unsigned char>(base[i + 1]))) {
                key.push_back(folded);
            }
            ++i;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            key.push_back(static_cast<char>(c));
        } else if (c >= 'A' && c <= 'Z') {
            key.push_back(static_cast<char>(c + 32));
        } else if (c == '&') {
            key += "and";
        }
    }
    return key;
}

} // namespace den::library
