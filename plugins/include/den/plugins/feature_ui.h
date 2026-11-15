// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The UI model a feature plugin's script hands to the app (docs/feature-plugins.md, section 5).
// A model has no colours, sizes or positions: the app's renderer draws it with the UI kit.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace den::plugins {

enum class ElementType { Shelf, Grid, Tile, Detail, Text, Image, Button, Row, Input, Progress, Empty, Qr };

struct Fact {
    std::string label, value;
};

struct Element {
    ElementType type = ElementType::Text;
    std::string id, title, subtitle, text, label, value, style, badge;
    std::string image;            // as the script gave it; resolve with FeatureHost::ImagePath
    std::string action, arg_json; // arg as JSON text, passed back untouched
    float aspect = 0, height = 0, fraction = -1;
    int columns = 0;
    bool enabled = true, secret = false;
    std::string more_action;
    std::vector<Fact> facts;
    std::vector<std::string> screenshots;
    std::vector<Element> items; // shelf, grid: tiles; detail: buttons; empty: its button
};

struct Chip {
    std::string id, label;
    bool selected = false;
};

struct PageModel {
    std::string title, subtitle;
    bool has_search = false;
    std::string search_placeholder, search_value, search_action;
    std::string chips_action;
    bool chips_multi = false;
    std::vector<Chip> chips;
    std::vector<Element> body;
    bool busy = false;
};

struct UiEvent {
    std::string action, arg_json, value; // value: input text, chip ids, search text
};

/// OpenWeb is answered many times: with WebFileAnswer for every file the user downloaded in
/// the page (accepted), and once with "not accepted" when the page was closed.
enum class AskKind { PickFile, Confirm, Toast, OpenGame, OpenUrl, Pop, OpenWeb, CloseWeb };

/// Something only the screen can do.
struct Ask {
    std::uint64_t id = 0;
    AskKind kind = AskKind::Toast;
    std::string title, message, yes, value; // value: game id or URL
    std::vector<std::string> extensions;    // PickFile, OpenWeb: the files that are taken
    std::vector<std::string> hosts;         // OpenWeb: where the page may go
};

/// A file the user downloaded in the in-app browser page, as the screen hands it to the host.
struct WebFile {
    std::string file; // where it is now (the app's web inbox); the host moves it
    std::string name; // the name the site gave it
    std::string host; // where it came from
};
/// The `value` of an accepted answer to an OpenWeb ask.
std::string WebFileAnswer(const WebFile& file);

} // namespace den::plugins
