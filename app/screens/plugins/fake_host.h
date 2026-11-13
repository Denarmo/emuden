// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A feature host that serves fixed sample plugins and pages, with the delays, failures and
// downloads a real one has. Used when the app is started with EMUDEN_FAKE_PLUGINS=1, to
// build and check the Plugins screens without a script host.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "feature_api.h"

namespace den::app {

std::unique_ptr<plugins::FeatureHost> CreateSampleFeatureHost();

namespace sample {

constexpr const char* ROMHACKS = "emuden.romhacks";
constexpr const char* CATALOGUE = "sample.catalogue";
constexpr const char* GALLERY = "thirdparty.gallery";
constexpr int CATALOGUE_PAGE = 12, CATALOGUE_TOTAL = 36, PICTURES = 6;

struct CatalogueQuery {
    std::string text;
    std::vector<std::string> chips; // selected chip ids
    int shown = CATALOGUE_PAGE;
};

std::vector<plugins::FeaturePlugin> Plugins();
plugins::PageModel RomHacksHome();
plugins::PageModel RomHacksInstall(const std::string& file);
plugins::PageModel RomHacksBases();
/// `loaded` false gives the page as it is while the list is fetched.
plugins::PageModel CataloguePage(const CatalogueQuery& query, bool loaded);
plugins::PageModel CatalogueDetail(int index);
plugins::PageModel GalleryPage();
std::string CatalogueTitle(int index);

} // namespace sample

} // namespace den::app
