// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The language of the launcher ("ui.language": "en", or the code of a language pack). English
// is built in; any other comes from a language pack, a plugin that is downloaded like the rest
// (Settings, Language, or the offer at the first start when the system speaks a language a pack
// has). The cores are told the same language (docs/languages.md).

#include <algorithm>

#include "den/app/app.h"
#include "den/app/feature_api.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/app/strings.h"
#include "den/base/files.h"
#include "den/base/language.h"
#include "den/base/log.h"
#include "den/platform/pal.h"
#include "den/ui/translate.h"
#include "language_packs.h"

namespace den::app {

namespace {

constexpr const char* LANGUAGE_KEY = "ui.language";
constexpr const char* ASKED_KEY = "ui.language_asked";

struct State {
    std::string applied;                  // the code whose pack is loaded ("" = nothing yet)
    std::uint64_t revision = ~0ull;       // of the plugin list when it was loaded
    std::uint64_t offer_revision = ~0ull; // of the plugin list at the last first-run check
};

State& GetState() {
    static State state;
    return state;
}

std::vector<plugins::FeaturePlugin> LanguagePlugins() {
    std::vector<plugins::FeaturePlugin> out;
    if (plugins::FeatureHost* host = FeatureHostSvc()) {
        for (plugins::FeaturePlugin& plugin : host->Plugins()) {
            if (plugins::OffersLanguage(plugin) && !plugin.language.empty()) {
                out.push_back(std::move(plugin));
            }
        }
    }
    return out;
}

std::vector<LanguageOffer> Offers() {
    std::vector<LanguageOffer> all;
    for (const plugins::FeaturePlugin& plugin : LanguagePlugins()) {
        all.push_back({plugin.id, plugin.language, plugin.language_name, plugin.installed});
    }
    return LanguageOffers(std::move(all));
}

std::string ChosenLanguage() {
    const std::string code = Svc().settings ? Svc().settings->GetString(LANGUAGE_KEY, "en") : "en";
    return code.empty() ? "en" : code;
}

// The plugins' own texts in `code`: the console plugins', then the feature and interface
// plugins' with what a language pack adds for other plugins, which wins.
void ApplyPluginTexts(const std::string& code) {
    plugins::Texts texts;
    if (plugins::FeatureHost* host = FeatureHostSvc()) {
        texts = host->TranslationsFor(code);
    }
    if (Svc().plugins) {
        plugins::AddMissing(texts, Svc().plugins->TranslationsFor(code));
    }
    if (const std::size_t refused = SetPluginTexts(TextMap(texts.begin(), texts.end())); refused > 0) {
        log::Warn("language", "{} plugin texts left out for their fields", refused);
    }
}

// Loads the pack of `code` (its texts file), or English when it is not installed yet, and the
// plugins' own texts in that language (they apply without a pack).
void Apply(const std::string& code) {
    ClearLanguagePack();
    if (LanguageBase(code) != "en") {
        ApplyPluginTexts(code);
        for (const plugins::FeaturePlugin& plugin : LanguagePlugins()) {
            if (plugin.language != code || !plugin.installed) {
                continue;
            }
            const auto file = plugin.dir / plugins::LANGUAGE_STRINGS_FILE;
            const auto text = ReadTextFile(file);
            auto texts = text ? ParseTexts(*text) : std::nullopt;
            if (!texts) {
                log::Warn("language", "{}: no texts in {}", plugin.id, PathToUtf8(file));
                continue;
            }
            const std::size_t refused = SetLanguagePack(std::move(*texts));
            if (refused > 0) {
                log::Warn("language", "{}: {} texts left out for their fields", plugin.id, refused);
            }
            break;
        }
    }
    if (Svc().stage) {
        RebuildTop();
    }
}

// The pack follows the setting (also one synced from another device) and the plugin list: a
// pack that finishes downloading applies, one that is removed puts English back.
void Follow() {
    Services& svc = Svc();
    plugins::FeatureHost* host = FeatureHostSvc();
    if (!svc.settings) {
        return;
    }
    State& state = GetState();
    const std::string code = ChosenLanguage();
    // Either list changing may bring texts: a pack, a plugin and its translations.
    const std::uint64_t revision = (host ? host->Revision() : 0) * 1000003u + (svc.plugins ? svc.plugins->Revision() : 0);
    if (code == state.applied && revision == state.revision) {
        return;
    }
    const bool first = state.applied.empty();
    state.applied = code;
    state.revision = revision;
    if (first && LanguageBase(code) == "en") {
        return; // English is what is loaded
    }
    Apply(code);
}

// Once, on the first start: when the system speaks a language that a pack has, offers it.
void OfferAtFirstStart() {
    Services& svc = Svc();
    plugins::FeatureHost* host = FeatureHostSvc();
    if (!svc.settings || !host || !svc.stage || svc.settings->GetBool(ASKED_KEY, false)) {
        return;
    }
    State& state = GetState();
    const std::uint64_t revision = host->Revision();
    if (revision == state.offer_revision || svc.stage->DialogOpen()) {
        return;
    }
    const ui::Page* top = svc.stage->Top();
    if (top == nullptr || top->Route() == "player" || top->Route() == "boot") {
        return;
    }
    state.offer_revision = revision;
    const std::string chosen = svc.settings->Has(LANGUAGE_KEY) ? svc.settings->GetString(LANGUAGE_KEY) : "";
    const std::vector<LanguageOffer> offers = Offers();
    const LanguageOffer* offer = FirstRunOffer(offers, pal::PreferredLanguages(), chosen, false);
    if (offer == nullptr) {
        return;
    }
    const LanguageOffer found = *offer;
    // Asked in English and in the language itself: the user may not read the one or the other.
    svc.stage->Confirm(
        TrF("Use EmuDEN in {}?", found.name),
        TrF("The {} language pack is a small download. You can change the language later in Settings, Language.",
            found.name),
        "Download",
        [found] {
            Svc().settings->SetBool(ASKED_KEY, true);
            Svc().settings->SetString(LANGUAGE_KEY, found.code);
            InstallFeaturePlugin(found.plugin, true);
        },
        "Not now", true, [] { Svc().settings->SetBool(ASKED_KEY, true); });
}

std::vector<ui::RowSpec> LanguageRows() {
    struct Entry {
        std::string code, plugin;
        bool installed = true;
    };
    std::vector<Entry> entries = {{"en", "", true}};
    std::vector<std::string> names = {"English"};
    for (const LanguageOffer& offer : Offers()) {
        entries.push_back({offer.code, offer.plugin, offer.installed});
        names.push_back(offer.installed ? offer.name : offer.name + " (download)");
    }
    ui::RowSpec row = ChoiceRow(
        LANGUAGE_KEY, "Language", names,
        [entries] {
            const std::string chosen = ChosenLanguage();
            const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.code == chosen; });
            return it == entries.end() ? 0 : static_cast<int>(it - entries.begin());
        },
        [entries](int index) {
            const Entry& entry = entries[static_cast<std::size_t>(index)];
            Svc().settings->SetString(LANGUAGE_KEY, entry.code);
            Svc().settings->SetBool(ASKED_KEY, true);
            if (!entry.installed) {
                InstallFeaturePlugin(entry.plugin, true); // applies once it is installed
            }
        });
    row.description = "More languages come from plugin sources, like themes. Games that have a language of their own follow it.";
    std::vector<ui::RowSpec> rows;
    rows.push_back(std::move(row));
    return rows;
}

void Start() {
    ui::SetTranslator(&PackText);
}

} // namespace

DEN_REGISTER_STARTUP(Start);
DEN_REGISTER_FRAME(Follow);
DEN_REGISTER_FRAME(OfferAtFirstStart);
DEN_REGISTER_SETTINGS(5, "Language", LanguageRows);

} // namespace den::app
