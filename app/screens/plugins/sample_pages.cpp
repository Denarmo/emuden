// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sample host's plugins and pages: one of every element of the UI model.

#include <algorithm>
#include <cctype>
#include <format>

#include "den/base/files.h"
#include "fake_host.h"

namespace den::app::sample {

namespace {

using plugins::Element;
using plugins::ElementType;
using plugins::FeaturePlugin;
using plugins::PageModel;

Element Make(ElementType type, std::string id = {}) {
    Element element;
    element.type = type;
    element.id = std::move(id);
    return element;
}

Element Text(std::string text, std::string style = "body") {
    Element element = Make(ElementType::Text);
    element.text = std::move(text);
    element.style = std::move(style);
    return element;
}

Element Button(std::string id, std::string label, std::string action, std::string arg = {}) {
    Element element = Make(ElementType::Button, std::move(id));
    element.label = std::move(label);
    element.action = std::move(action);
    element.arg_json = std::move(arg);
    return element;
}

Element Tile(std::string id, std::string title, std::string image, std::string action, std::string arg,
             std::string badge = {}) {
    Element element = Make(ElementType::Tile, std::move(id));
    element.title = std::move(title);
    element.image = std::move(image);
    element.action = std::move(action);
    element.arg_json = std::move(arg);
    element.badge = std::move(badge);
    return element;
}

Element Row(std::string id, std::string label, std::string value, std::string action = {}) {
    Element element = Make(ElementType::Row, std::move(id));
    element.label = std::move(label);
    element.value = std::move(value);
    element.action = std::move(action);
    return element;
}

const char* const NAMES[] = {"Amber Coast", "Birch Hollow", "Cinder Peak", "Dawn Harbour", "Ember Field",
                             "Frost Vale",  "Glass Lagoon", "Hazel Ridge", "Iron Meadow",  "Juniper Bay",
                             "Kite Summit", "Lantern Marsh"};
const char* const KINDS[] = {"complete", "demo", "beta"};

std::string Picture(int index) {
    return std::format("https://images.sample.invalid/{}.png", index % PICTURES);
}

const char* const LONG_TEXT =
    "A new region with its own story, set a few years after the original game. Every route was "
    "drawn again, the towns are larger and the gyms are fought in a different order.\n\n"
    "What changes: about 380 creatures can be caught without trading, the physical and special "
    "split is in, moves can be relearned from the menu, and the day and night cycle decides which "
    "ones appear. Trainers are harder than in the original and scale with the number of badges.\n\n"
    "What stays: saves made with an earlier version keep working unless the notes of a version "
    "say otherwise. The patch is applied to your own copy of the game; nothing else is downloaded.";

} // namespace

std::vector<FeaturePlugin> Plugins() {
    FeaturePlugin hacks;
    hacks.id = ROMHACKS;
    hacks.name = "ROM Hacks (sample)";
    hacks.version = "1.0.0";
    hacks.publisher = "EmuDEN";
    hacks.summary = "Install ROM hack patches on your own games.";
    hacks.source_name = "Official";
    hacks.installed = true;
    hacks.data_bytes = 18u << 20;
    hacks.permissions = {{"open-url", "Open pages of www.hackdex.app in your browser"},
                         {"files.import", "Read files you choose"},
                         {"downloads.watch", "See patch files in your Downloads folder"},
                         {"library.read", "See which games of these consoles you have: GB, GBC, GBA, NDS"},
                         {"patch", "Create patched copies of your games"},
                         {"library.write", "Change the hacks it installed"}};

    FeaturePlugin catalogue;
    catalogue.id = CATALOGUE;
    catalogue.name = "Catalogue (sample)";
    catalogue.version = "0.9.0";
    catalogue.publisher = "EmuDEN";
    catalogue.summary = "Search, filters, a grid that loads more, downloads with progress.";
    catalogue.source_name = "Official";
    catalogue.installed = true;
    catalogue.update_available = true;
    catalogue.needs_consent = true;
    catalogue.data_bytes = 3u << 20;
    catalogue.permissions = {{"http", "Connect to: images.sample.invalid"}};
    catalogue.new_permissions = {{"download", "Download files from: files.sample.invalid"}};

    FeaturePlugin gallery;
    gallery.id = GALLERY;
    gallery.name = "Element gallery";
    gallery.version = "0.3.1";
    gallery.publisher = "thirdparty";
    gallery.summary = "Every element a plugin page can show.";
    gallery.source_name = "github:thirdparty/plugins";
    gallery.trust = FeaturePlugin::Trust::ThirdParty;
    gallery.needs_consent = true;
    gallery.permissions = {{"http", "Connect to: api.sample.invalid, images.sample.invalid"},
                           {"files.import", "Read files you choose"},
                           {"library.read", "See which games of these consoles you have: GBA"}};
    return {hacks, catalogue, gallery};
}

PageModel RomHacksHome() {
    PageModel page;
    page.title = "ROM Hacks";
    page.subtitle = "Sample data: nothing here is read from your device";
    page.body.push_back(Text("1. Find a hack on Hackdex in your browser and download its patch file.\n"
                             "2. Choose that file here, or pick it from Downloads below.\n"
                             "3. The patch is applied to a copy of your own game and the hack appears "
                             "in your library."));
    page.body.push_back(Button("open_site", "Open Hackdex", "open_site"));
    page.body.push_back(Button("import", "Choose a patch file", "import"));
    Element inbox = Make(ElementType::Shelf, "inbox");
    inbox.title = "Found in Downloads";
    inbox.items.push_back(Tile("inbox.0", "Amber Coast v2.1.bps", "", "install", "\"Amber Coast v2.1.bps\"", "BPS"));
    inbox.items.push_back(Tile("inbox.1", "Frost Vale.xdelta", "", "install", "\"Frost Vale.xdelta\"", "xdelta"));
    for (Element& tile : inbox.items) {
        tile.aspect = 1.0f;
    }
    page.body.push_back(std::move(inbox));
    page.body.push_back(Text("Installed hacks", "heading"));
    Element hacks = Make(ElementType::Grid, "hacks");
    for (int i = 0; i < 5; ++i) {
        hacks.items.push_back(
            Tile(std::format("hack.{}", i), NAMES[i], std::format("plugin:{}.png", i), "open_game", "", "Hack"));
    }
    page.body.push_back(std::move(hacks));
    page.body.push_back(Row("bases", "Base games", "7 of 39 in your library", "bases"));
    return page;
}

PageModel RomHacksInstall(const std::string& file) {
    PageModel page;
    page.title = "Install a hack";
    const std::string name = PathToUtf8(PathFromUtf8(file).stem());
    Element detail = Make(ElementType::Detail, "install");
    detail.title = name;
    detail.subtitle = "For Pokemon Emerald (GBA)";
    detail.image = "plugin:2.png";
    detail.facts = {{"File", PathToUtf8(PathFromUtf8(file).filename())},
                    {"Format", ToLower(file).ends_with(".xdelta") ? "xdelta" : "BPS"},
                    {"Base game", "In your library"}};
    detail.text = "The patch names its base game by checksum, and that game is in your library. "
                  "Your copy is only read: the hack is a new file.";
    detail.items.push_back(Button("get", "Install", "download", "\"patch\""));
    page.body.push_back(std::move(detail));
    Element input = Make(ElementType::Input, "install.title");
    input.label = "Title";
    input.value = name;
    input.action = "typed";
    page.body.push_back(std::move(input));
    return page;
}

PageModel RomHacksBases() {
    PageModel page;
    page.title = "Base games";
    page.subtitle = "The dumps patches are made for";
    for (int i = 0; i < 14; ++i) {
        page.body.push_back(Row(std::format("base.{}", i),
                                std::format("{} ({})", NAMES[i % 12], i % 3 ? "USA" : "Europe"),
                                i % 2 ? "Missing" : "In your library"));
    }
    return page;
}

std::string CatalogueTitle(int index) {
    const int round = index / 12;
    return round == 0 ? NAMES[index % 12] : std::format("{} {}", NAMES[index % 12], round + 1);
}

PageModel CataloguePage(const CatalogueQuery& query, bool loaded) {
    PageModel page;
    page.title = "Catalogue";
    page.subtitle = "A sample list; search for \"error\" to see a failed request";
    page.has_search = true;
    page.search_placeholder = "Search hacks";
    page.search_value = query.text;
    page.search_action = "search";
    page.chips_action = "filter";
    page.chips_multi = true;
    for (const char* kind : KINDS) {
        std::string label = kind;
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        page.chips.push_back({kind, label, std::ranges::find(query.chips, kind) != query.chips.end()});
    }
    page.busy = !loaded;
    if (!loaded) {
        return page;
    }
    if (ToLower(query.text) == "error") {
        Element failed = Make(ElementType::Empty, "failed");
        failed.title = "The list could not be loaded";
        failed.text = "The server did not answer (sample failure).";
        failed.items.push_back(Button("retry", "Retry", "retry"));
        page.body.push_back(std::move(failed));
        return page;
    }
    Element grid = Make(ElementType::Grid, "results");
    for (int i = 0; i < CATALOGUE_TOTAL; ++i) {
        const std::string title = CatalogueTitle(i);
        const char* kind = KINDS[i % 3];
        if (!query.text.empty() && ToLower(title).find(ToLower(query.text)) == std::string::npos) {
            continue;
        }
        if (!query.chips.empty() && std::ranges::find(query.chips, kind) == query.chips.end()) {
            continue;
        }
        if (static_cast<int>(grid.items.size()) >= query.shown) {
            grid.more_action = "more";
            break;
        }
        Element tile = Tile(std::format("hack.{}", i), title, Picture(i), "detail", std::to_string(i),
                            i % 3 == 0 ? "" : (i % 3 == 1 ? "Demo" : "Beta"));
        tile.subtitle = std::format("{} downloads", 1200 + i * 317);
        grid.items.push_back(std::move(tile));
    }
    if (grid.items.empty()) {
        Element none = Make(ElementType::Empty, "none");
        none.title = "Nothing found";
        none.text = "No hack matches the search and the filters.";
        page.body.push_back(std::move(none));
        return page;
    }
    page.body.push_back(std::move(grid));
    return page;
}

PageModel CatalogueDetail(int index) {
    PageModel page;
    page.title = CatalogueTitle(index);
    Element detail = Make(ElementType::Detail, "detail");
    detail.title = CatalogueTitle(index);
    detail.subtitle = "by a sample author";
    detail.image = Picture(index);
    detail.facts = {{"Base game", "Pokemon Emerald (GBA)"},
                    {"Version", std::format("2.{}", index % 7)},
                    {"Status", KINDS[index % 3]},
                    {"Downloads", std::to_string(1200 + index * 317)}};
    detail.text = LONG_TEXT;
    for (int i = 1; i <= 4; ++i) {
        detail.screenshots.push_back(Picture(index + i));
    }
    detail.items.push_back(Button("get", "Install", "download", std::to_string(index)));
    detail.items.push_back(Button("library", "Open the game in the library", "open_game"));
    detail.items.push_back(Button("ask", "Ask a question", "confirm"));
    page.body.push_back(std::move(detail));
    return page;
}

PageModel GalleryPage() {
    PageModel page;
    page.title = "Element gallery";
    page.subtitle = "Every element of the UI model";
    page.body.push_back(Text("Text: heading", "heading"));
    page.body.push_back(Text("Text: body. " + std::string(LONG_TEXT).substr(0, 180)));
    page.body.push_back(Text("Text: caption, for notes under something.", "caption"));
    page.body.push_back(Text("Text: warning. This patch breaks saves made with version 1.", "warning"));
    Element shelf = Make(ElementType::Shelf, "shelf");
    shelf.title = "Shelf";
    for (int i = 0; i < 8; ++i) {
        shelf.items.push_back(Tile(std::format("shelf.{}", i), NAMES[i],
                                   std::format("plugin:{}.png", i % PICTURES), "toast",
                                   std::format("\"{}\"", NAMES[i])));
    }
    page.body.push_back(std::move(shelf));
    Element image = Make(ElementType::Image, "image");
    image.image = "plugin:4.png";
    image.height = 120;
    image.aspect = 3.0f;
    page.body.push_back(std::move(image));
    page.body.push_back(Button("button", "Button", "toast", "\"Button pressed\""));
    Element off = Button("button.off", "Button, not enabled", "toast");
    off.enabled = false;
    page.body.push_back(std::move(off));
    page.body.push_back(Row("row", "Row", "with a value", "toast"));
    Element input = Make(ElementType::Input, "input");
    input.label = "Input";
    input.value = "typed text";
    input.action = "typed";
    page.body.push_back(std::move(input));
    Element secret = Make(ElementType::Input, "secret");
    secret.label = "Input, secret";
    secret.value = "hidden";
    secret.secret = true;
    secret.action = "typed";
    page.body.push_back(std::move(secret));
    Element known = Make(ElementType::Progress, "progress.known");
    known.label = "Progress";
    known.fraction = 0.4f;
    page.body.push_back(std::move(known));
    Element unknown = Make(ElementType::Progress, "progress.unknown");
    unknown.label = "Progress, amount unknown";
    page.body.push_back(std::move(unknown));
    page.body.push_back(Button("fail", "Make the script fail", "fail"));
    Element empty = Make(ElementType::Empty, "empty");
    empty.title = "Empty";
    empty.text = "Shown when a list has nothing in it.";
    empty.items.push_back(Button("empty.button", "Its button", "toast", "\"Empty's button\""));
    page.body.push_back(std::move(empty));
    return page;
}

} // namespace den::app::sample
