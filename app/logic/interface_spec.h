// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The look an interface plugin describes (plugin.json, "interface"; docs/interface-plugins.md
// in emuden-plugins). It is data only: the app's layouts draw it, the plugin picks one and
// sets its colours, art and motion. Anything missing or out of range takes the default.

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"
#include "den/gfx/types.h"
#include "den/plugins/host.h"

namespace den::app {

/// The object a game is in the "shelf3d" layout. Sizes are relative to the object's height.
struct ObjectLook {
    std::string shape = "case"; // "cartridge", "card", "case", "jewel" or "disc"
    float aspect = 0;           // the front's width over its height; 0: the shape's own
    float depth = 0;            // thickness over height; 0: the shape's own
    gfx::Color shell{28, 29, 34, 255};
    bool has_shell = false;     // false: the shape's own colour
    gfx::Rect label{0, 0, 0, 0}; // the part of the front the cover is on; empty: the shape's own
    bool disc = false;          // a case or jewel case with a disc that slides out when chosen
    bool umd = false;           // that disc is a UMD: a shell whose disc turns inside it
    float radius = -1;          // the top corners' rounding; below 0: the shape's own
    float notch = -1;           // the top-right corner cut off at 45 degrees; below 0: the shape's own
    int grip = -1;              // a cartridge's grip ridges; below 0: the shape's own
    // A cartridge's mould: "plain"; "pocket", a handheld's with stepped top corners, a sunk
    // panel and label well; "pocket_wide", a wide one with shoulders; or a console's own
    // (game_moulds.cpp: "nes", "snes", "n64", "hucard", ...). Empty: plain.
    std::string style;
    std::string label_edge; // "none", "paper" or "foil" round the cover; empty: the style's own
    // A cartridge's or card's cardboard box standing behind it, front width over height (its
    // cover's own proportions once that is loaded).
    bool box = false;
    float box_aspect = 0.9f;
};

/// How one console looks: its colour, a wallpaper of the package and the label on its tab.
struct ConsoleLook {
    gfx::Color color{90, 96, 120, 255};
    std::string wallpaper; // UTF-8 path inside the package, or empty
    std::string label;     // empty: the console's short name
    ObjectLook object;
};

/// "grid": many covers per row, the selected game's details in a panel beside them.
struct GridLook {
    int columns = 0;    // covers per row; 0: as many as fit at the height `covers.height` gives
    bool panel = true;  // the details panel (beside the grid, or under it on a tall screen)
    bool titles = false; // each game's name under its cover
};

/// "list": the games as rows of text, the selected one's cover or picture large beside them.
struct ListLook {
    int rows = 9;             // rows on the screen at the default text size
    bool art = true;          // the cover beside the list (left out on narrow screens)
    bool art_left = false;    // that cover on the left instead of the right
    bool thumbnails = true;   // a small cover at the start of each row
};

/// "wheel": game names on a turning arc at one side, the selected game's art filling the rest.
struct WheelLook {
    bool left = false; // the arc on the left instead of the right
    int names = 9;     // names on the arc, the selected one in the middle
    float curve = 0.5f; // 0: a straight column, 1: a deep arc
};

/// "hero": the selected game large across the screen, a thin row of covers under it.
struct HeroLook {
    float strip = 0.24f; // the row's share of the layout's height
};

struct InterfaceSpec {
    std::string id, name;
    std::filesystem::path dir; // the package

    /// The layout that draws it. "carousel": consoles as tabs, one row of covers with the
    /// selected one large in the middle. "shelf3d": the same row as 3D objects (cartridges,
    /// cases, discs) that turn towards the selected one. "grid", "list", "wheel" and "hero":
    /// see GridLook, ListLook, WheelLook and HeroLook.
    std::string layout = "carousel";

    gfx::Color background{16, 17, 24, 255};
    gfx::Color text{255, 255, 255, 255};
    gfx::Color dim{184, 186, 200, 255};
    gfx::Color accent{255, 204, 64, 255};
    gfx::Color panel{255, 255, 255, 26};

    /// Behind the covers: "cover" (the selected cover, blurred), "console" (its wallpaper or
    /// colour) or "plain" (the background colour).
    std::string backdrop = "cover";
    float tint = 0.35f; // how much of the console's colour washes over the backdrop
    std::string wallpaper; // the package's own wallpaper for consoles without one

    float cover_height = 0.62f; // the selected cover's share of the carousel's height
    float side_scale = 0.72f;   // covers beside it, relative to it
    float side_dim = 0.45f;     // how much darker they are
    float spacing = 28;         // between covers
    float reflection = 0.18f;   // opacity of the mirrored cover under it; 0 = none
    float bob = 4;              // the selected cover floats up and down by this much
    float speed = 12;           // how fast the row glides (higher is snappier)

    float side_angle = 0.9f; // shelf3d: how far side objects turn towards the middle (radians)
    float sway = 0.12f;      // shelf3d: the selected object's slow turn either way (radians)

    GridLook grid;
    ListLook list;
    WheelLook wheel;
    HeroLook hero;

    bool tab_continue = true; // a "Continue" tab first: the games last played on any device
    bool tab_recent = true; // a "Recent" tab first when something was played
    bool tab_all = true;    // an "All" tab with every game

    std::string launch = "zoom"; // "zoom" (the cover grows into the console colour) or "none"
    float launch_seconds = 0.32f;

    /// By system id, "group:<group>" or "*".
    std::map<std::string, ConsoleLook, std::less<>> consoles;

    /// The look for a console: by its id, else its group, else "*", else a quiet default.
    ConsoleLook LookFor(std::string_view system, std::string_view group) const;
};

/// Reads the "interface" object of an interface plugin's plugin.json from `dir`. Fails when it
/// asks for a layout this app does not have. `moulds`: the cartridge styles the app draws
/// besides "plain", "pocket" and "pocket_wide" (MouldNames()).
Result<InterfaceSpec> ParseInterface(std::string_view manifest_json, const std::filesystem::path& dir,
                                     const std::vector<std::string>& moulds);

/// The layouts this app draws.
bool KnownLayout(std::string_view layout);

/// "#rrggbb" or "#rrggbbaa"; nothing for anything else.
std::optional<gfx::Color> ParseHexColor(std::string_view text);

} // namespace den::app
