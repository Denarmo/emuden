// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Widgets the plugin pages need beyond the kit's: text that wraps, a search field, a filter
// chip, a button that can be off, a progress line.

#pragma once

#include <string>
#include <vector>

#include "../home/game_widgets.h"
#include "den/ui/widgets.h"

namespace den::app {

/// Text broken into lines at `wrap_width`. Line breaks in the text are kept.
class Paragraph final : public ui::Node {
public:
    Paragraph(std::string text, float wrap_width);
    std::string text;
    float wrap_width;
    float size = 0; // 0 = the theme's body size
    int font = gfx::FONT_REGULAR;
    ui::Tone tone = ui::Tone::Text;
    bool centered = false;
    bool boxed = false; // on a panel tint (a warning)
    bool clip = false;  // only the lines that fit its height, the last cut with "..."

protected:
    ui::Size MeasureContent(ui::Context& ctx) override;
    void DrawContent(ui::Context& ctx) override;

private:
    void Wrap(ui::Context& ctx);
    std::vector<std::string> lines_;
    std::string wrapped_text_;
    float wrapped_width_ = -1, wrapped_scale_ = 0;
};

/// The page's search: shows the query or the placeholder; Confirm or a tap opens the text entry.
class SearchField final : public ui::Node {
public:
    SearchField();
    std::string text, placeholder;

protected:
    void DrawContent(ui::Context& ctx) override;
};

/// A filter that is on or off.
class FilterChip final : public ui::Node {
public:
    explicit FilterChip(std::string text);
    std::string text;
    bool selected = false;

protected:
    ui::Size MeasureContent(ui::Context& ctx) override;
    void DrawContent(ui::Context& ctx) override;
};

/// A button of the model: drawn dim and out of the navigation when it is not enabled.
class ModelButton final : public ui::Button {
public:
    using ui::Button::Button;
    void SetEnabled(bool enabled);

protected:
    void DrawContent(ui::Context& ctx) override;

private:
    bool enabled_ = true;
};

/// A label, the amount as text and a bar.
class ProgressLine final : public ui::Node {
public:
    ProgressLine(std::string label, float fraction);
    void Set(const std::string& label, float fraction);

private:
    ui::Label* label_ = nullptr;
    ui::Label* amount_ = nullptr;
    ProgressBar* bar_ = nullptr;
};

} // namespace den::app
