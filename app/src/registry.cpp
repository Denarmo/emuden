// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/platform/pal.h"

namespace den::app {

Registry& Registry::Get() {
    static Registry registry;
    return registry;
}

void Registry::AddScreen(std::string route, ui::Stage::Factory factory) {
    screens_.emplace_back(std::move(route), std::move(factory));
}

void Registry::AddRail(RailEntry entry) {
    rail_.push_back(std::move(entry));
}

void Registry::AddSettings(SettingsSection section) {
    settings_.push_back(std::move(section));
}

void Registry::AddStartup(std::function<void()> hook) {
    startup_.push_back(std::move(hook));
}

void Registry::AddFrame(std::function<void()> hook) {
    frame_.push_back(std::move(hook));
}

void Registry::AddQuitHold(std::function<bool()> hook) {
    quit_holds_.push_back(std::move(hook));
}

void Registry::AddOverlay(std::function<void(ui::Context&)> draw) {
    overlays_.push_back(std::move(draw));
}

void Registry::AddAfterDraw(std::function<void(int, int)> hook) {
    after_draw_.push_back(std::move(hook));
}

void Registry::AddEventFilter(std::function<bool(const input::Event&)> filter) {
    event_filters_.push_back(std::move(filter));
}

void Registry::AddShutdown(std::function<void()> hook) {
    shutdown_.push_back(std::move(hook));
}

std::vector<RailEntry> Registry::Rail() const {
    std::vector<RailEntry> out = rail_;
    std::stable_sort(out.begin(), out.end(),
                     [](const RailEntry& a, const RailEntry& b) { return a.order < b.order; });
    return out;
}

std::vector<SettingsSection> Registry::Settings() const {
    std::vector<SettingsSection> out = settings_;
    std::stable_sort(out.begin(), out.end(),
                     [](const SettingsSection& a, const SettingsSection& b) { return a.order < b.order; });
    return out;
}

namespace {

// EmuDEN's mark ("D.", assets/branding), drawn in the theme's ink so it follows the theme.
class BrandMark final : public ui::Node {
public:
    explicit BrandMark(float side) {
        width = height = side;
    }

protected:
    void DrawContent(ui::Context& ctx) override {
        static const std::string path = PathToUtf8(pal::AssetsDir() / "branding" / "mark-white.png");
        if (const gfx::Texture texture = ctx.textures ? ctx.textures->Get(path, 128) : gfx::Texture{}) {
            ctx.canvas->DrawImage(rect, texture, ctx.theme->fg);
        }
    }
};

} // namespace

ui::Node* BuildRailFrame(ui::Page& page, const std::string& title) {
    const ui::Theme& theme = *Svc().theme;
    ui::Node& root = page.Root();
    root.axis = ui::Axis::Row;

    ui::Node* rail = root.Add(ui::MakeBox(ui::Axis::Column, 8));
    rail->id = "rail";
    rail->chrome = true;
    rail->width = 232;
    rail->background = theme.panel;
    rail->SetPadding(16, 24);
    ui::Node* brand = rail->Add(ui::MakeBox(ui::Axis::Row, 8));
    brand->align = ui::Align::Center;
    brand->pad_left = 6;
    brand->pad_bottom = 18;
    brand->Emplace<BrandMark>(40.0f);
    ui::Node* word = brand->Add(ui::MakeBox(ui::Axis::Row, 0));
    word->Emplace<ui::Label>("Emu", 26.0f, gfx::FONT_REGULAR);
    word->Emplace<ui::Label>("DEN", 26.0f, gfx::FONT_BOLD);
    for (const RailEntry& entry : Registry::Get().Rail()) {
        auto* button = rail->Emplace<ui::Button>(entry.label);
        button->id = "rail." + entry.route;
        button->centered = false;
        button->selected = entry.route == page.Route();
        button->on_activate = [route = entry.route, current = page.Route()] {
            if (route != current) {
                Svc().stage->PopToRoot();
                Svc().stage->Replace(route);
            }
        };
    }

    ui::Node* content = root.Add(ui::MakeBox(ui::Axis::Column, 16));
    content->id = "content";
    content->grow = 1;
    content->pad_left = theme.margin;
    content->pad_top = 28;
    if (!title.empty()) {
        content->Emplace<ui::Label>(title, theme.text_title, gfx::FONT_BOLD);
    }
    return content;
}

ui::Node* BuildPlainFrame(ui::Page& page, const std::string& title) {
    const ui::Theme& theme = *Svc().theme;
    ui::Node& root = page.Root();
    root.axis = ui::Axis::Column;
    root.gap = 16;
    root.SetPadding(theme.margin, 28);
    if (!title.empty()) {
        root.Emplace<ui::Label>(title, theme.text_title, gfx::FONT_BOLD);
    }
    return &root;
}

} // namespace den::app
