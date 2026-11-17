// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "language_packs.h"

#include <algorithm>

#include "den/base/language.h"

namespace den::app {

std::vector<LanguageOffer> LanguageOffers(std::vector<LanguageOffer> all) {
    std::stable_sort(all.begin(), all.end(), [](const LanguageOffer& a, const LanguageOffer& b) {
        return a.installed > b.installed;
    });
    std::vector<LanguageOffer> out;
    for (LanguageOffer& offer : all) {
        const bool seen = std::any_of(out.begin(), out.end(), [&](const LanguageOffer& o) { return o.code == offer.code; });
        if (!seen && !offer.code.empty()) {
            out.push_back(std::move(offer));
        }
    }
    std::sort(out.begin(), out.end(), [](const LanguageOffer& a, const LanguageOffer& b) { return a.name < b.name; });
    return out;
}

const LanguageOffer* FirstRunOffer(const std::vector<LanguageOffer>& offers,
                                   const std::vector<std::string>& system_languages, const std::string& chosen,
                                   bool asked) {
    if (asked || !chosen.empty()) {
        return nullptr;
    }
    std::vector<std::string> codes;
    for (const LanguageOffer& offer : offers) {
        codes.push_back(offer.code);
    }
    const std::string code = MatchLanguage(system_languages, codes);
    for (const LanguageOffer& offer : offers) {
        if (offer.code == code) {
            return offer.installed ? nullptr : &offer;
        }
    }
    return nullptr;
}

} // namespace den::app
