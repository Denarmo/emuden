// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The parts of a console manifest that say how its games look and play: screens, pad
// layout, media, netplay and per-device tuning of its cores.

#include <cmath>

#include "json_util.h"
#include "manifest_parts.h"

namespace den::plugins {

namespace {

constexpr int MAX_SHAPES = 8;
constexpr int MAX_SCREENS = 4;

bool Fraction(const JsonValue& value, float& out) {
    if (value.kind != JsonValue::Kind::Number || !std::isfinite(value.number) || value.number < 0 ||
        value.number > 1) {
        return false;
    }
    out = static_cast<float>(value.number);
    return true;
}

// [x, y, w, h], each 0..1, inside the picture.
bool ParseRect(const JsonValue* rect, ScreenPart& part) {
    if (rect == nullptr || rect->kind != JsonValue::Kind::Array || rect->items.size() != 4) {
        return false;
    }
    if (!Fraction(rect->items[0], part.x) || !Fraction(rect->items[1], part.y) || !Fraction(rect->items[2], part.w) ||
        !Fraction(rect->items[3], part.h)) {
        return false;
    }
    return part.w > 0 && part.h > 0 && part.x + part.w <= 1.0001f && part.y + part.h <= 1.0001f;
}

std::vector<std::pair<std::string, std::string>> StringMembers(const JsonValue* object) {
    std::vector<std::pair<std::string, std::string>> out;
    if (object != nullptr) {
        for (const auto& [key, value] : object->members) {
            if (value.kind == JsonValue::Kind::String && !key.empty()) {
                out.emplace_back(key, value.string);
            }
        }
    }
    return out;
}

} // namespace

std::vector<ScreenShape> ParseScreens(const JsonValue& shapes) {
    std::vector<ScreenShape> out;
    for (const JsonValue& item : shapes.items) {
        const JsonValue* frame = item.Find("frame");
        const JsonValue* screens = item.Find("screens");
        if (frame == nullptr || frame->items.size() != 2 || screens == nullptr || screens->items.empty() ||
            screens->items.size() > MAX_SCREENS) {
            continue;
        }
        ScreenShape shape;
        const double w = frame->items[0].number, h = frame->items[1].number;
        if (frame->items[0].kind != JsonValue::Kind::Number || frame->items[1].kind != JsonValue::Kind::Number ||
            w < 1 || h < 1 || w > 16384 || h > 16384) {
            continue;
        }
        shape.frame_width = static_cast<int>(w);
        shape.frame_height = static_cast<int>(h);
        bool good = true;
        for (const JsonValue& screen : screens->items) {
            ScreenPart part;
            part.touch = json::Bool(screen, "touch", false);
            good = good && ParseRect(screen.Find("rect"), part);
            shape.screens.push_back(part);
        }
        if (good && static_cast<int>(out.size()) < MAX_SHAPES) {
            out.push_back(std::move(shape));
        }
    }
    return out;
}

void ParsePresentation(const JsonValue& presentation, Console& console) {
    if (const JsonValue* screens = presentation.Find("screens")) {
        console.screens = ParseScreens(*screens);
    }
    const std::string layout = presentation.String("layout");
    if (layout == "nintendo" || layout == "other") {
        console.pad_layout = layout;
    }
    if (const JsonValue* digital = presentation.Find("digital_only"); digital && digital->kind == JsonValue::Kind::Bool) {
        console.digital_only = digital->boolean;
    }
    if (const JsonValue* media = presentation.Find("media"); media && media->kind == JsonValue::Kind::Object) {
        const std::string shape = media->String("shape");
        if (shape == "cartridge" || shape == "card" || shape == "case" || shape == "jewel" || shape == "disc") {
            console.media.shape = shape;
            console.media.style = media->String("style");
            const double aspect = json::Number(*media, "aspect", 0);
            console.media.aspect = aspect > 0.1 && aspect < 10 ? static_cast<float>(aspect) : 0.0f;
            console.media.disc = json::Bool(*media, "disc", false);
            console.media.umd = json::Bool(*media, "umd", false);
        }
    }
}

void ParseCoreExtras(const JsonValue& value, Component& component) {
    component.option_hints = StringMembers(value.Find("option_hints"));
    if (const JsonValue* presentation = value.Find("presentation")) {
        if (const JsonValue* screens = presentation->Find("screens")) {
            component.screens = ParseScreens(*screens);
        }
    }
    if (const JsonValue* netplay = value.Find("netplay"); netplay && netplay->kind == JsonValue::Kind::Object) {
        const std::string mode = netplay->String("mode");
        if (mode == "link" || mode == "lockstep") {
            component.netplay.mode = mode;
            const double subsystem = json::Number(*netplay, "subsystem", 0);
            component.netplay.subsystem =
                subsystem >= 0 && subsystem < 65536 ? static_cast<unsigned>(subsystem) : 0u;
            component.netplay.description = netplay->String("description");
            for (const auto& [name, side] : {std::pair{"host", &component.netplay.host},
                                             std::pair{"guest", &component.netplay.guest}}) {
                if (const JsonValue* object = netplay->Find(name)) {
                    const double memory = json::Number(*object, "save_memory", 0);
                    side->save_memory = memory >= 0 && memory < 65536 ? static_cast<unsigned>(memory) : 0u;
                    side->options = StringMembers(object->Find("options"));
                }
            }
        }
    }
    component.options_name = value.String("options_name");
    if (const JsonValue* tuning = value.Find("tuning"); tuning && tuning->kind == JsonValue::Kind::Object) {
        component.tuning = *tuning;
    }
}

} // namespace den::plugins
