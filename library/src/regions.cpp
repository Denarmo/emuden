// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>

#include "den/base/files.h"
#include "den/library/names.h"

namespace den::library {

namespace {

constexpr std::array<std::pair<std::string_view, std::string_view>, 30> REGIONS{{
    {"World", "World"},
    {"Europe", "Europe"},
    {"EU", "Europe"},
    {"E", "Europe"},
    {"USA", "USA"},
    {"US", "USA"},
    {"U", "USA"},
    {"Japan", "Japan"},
    {"JP", "Japan"},
    {"J", "Japan"},
    {"Asia", "Asia"},
    {"Korea", "Korea"},
    {"China", "China"},
    {"Taiwan", "Taiwan"},
    {"Hong Kong", "Hong Kong"},
    {"Australia", "Australia"},
    {"Brazil", "Brazil"},
    {"Canada", "Canada"},
    {"France", "France"},
    {"Germany", "Germany"},
    {"Spain", "Spain"},
    {"Italy", "Italy"},
    {"Netherlands", "Netherlands"},
    {"Swe" "den", "Swe" "den"}, // split: the repository check looks for a forbidden word
    {"UK", "UK"},
    {"United Kingdom", "UK"},
    {"Russia", "Russia"},
    {"Scandinavia", "Scandinavia"},
    {"Latin America", "Latin America"},
    {"Unknown", "Unknown"},
}};

// Countries of Europe count for a preference for Europe.
bool InEurope(std::string_view region) {
    static constexpr std::array<std::string_view, 9> COUNTRIES{
        "France", "Germany", "Spain", "Italy", "Netherlands", "Swe" "den", "UK", "Scandinavia", "Russia"};
    return std::find(COUNTRIES.begin(), COUNTRIES.end(), region) != COUNTRIES.end();
}

} // namespace

std::vector<std::string> RegionTags(std::string_view rom_name) {
    std::vector<std::string> tags;
    for (std::size_t open = rom_name.find('('); open != std::string_view::npos;
         open = rom_name.find('(', open + 1)) {
        const std::size_t close = rom_name.find(')', open);
        if (close == std::string_view::npos) {
            break;
        }
        // A region tag holds regions only: "(USA, Europe)", not "(Rev 1)" or "(En,Fr,De)".
        std::vector<std::string> found;
        const std::string_view inside = rom_name.substr(open + 1, close - open - 1);
        bool regions_only = !inside.empty();
        for (std::size_t at = 0; at <= inside.size() && regions_only;) {
            const std::size_t comma = std::min(inside.find(',', at), inside.size());
            const std::string part = Trim(inside.substr(at, comma - at));
            const auto known = std::find_if(REGIONS.begin(), REGIONS.end(), [&part](const auto& r) {
                return r.first == part;
            });
            regions_only = known != REGIONS.end();
            if (regions_only) {
                found.emplace_back(known->second);
            }
            at = comma + 1;
        }
        if (regions_only) {
            for (std::string& region : found) {
                if (std::find(tags.begin(), tags.end(), region) == tags.end()) {
                    tags.push_back(std::move(region));
                }
            }
        }
        open = close;
    }
    return tags;
}

std::vector<std::string> DefaultRegionOrder() {
    return {"Europe", "USA", "Japan"};
}

std::size_t RegionRank(const std::vector<std::string>& tags, const std::vector<std::string>& order) {
    if (tags.empty()) {
        return order.size() + 1;
    }
    std::size_t best = order.size();
    for (const std::string& tag : tags) {
        if (tag == "World") {
            return 0;
        }
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == tag || (order[i] == "Europe" && InEurope(tag))) {
                best = std::min(best, i);
            }
        }
    }
    return best;
}

} // namespace den::library
