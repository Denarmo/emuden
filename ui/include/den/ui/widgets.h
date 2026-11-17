// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "den/ui/node.h"

namespace den::ui {

enum class Tone { Text, Dim };

/// Below this width a settings row stacks: a value that does not fit beside the label goes
/// under it, and the description wraps to two lines.
constexpr float ROW_STACK_WIDTH = 640;

/// `text` in one line inside `rect`, cut with an ellipsis, centred vertically.
void DrawTextIn(Context& ctx, const gfx::Rect& rect, const std::string& text, float size, int font,
                gfx::Color color, Align align);
/// `text` broken into lines at spaces (and at line breaks) so each fits `width`.
std::vector<std::string> WrapText(gfx::Text& text, const std::string& in, float size, float width);

/// One line of text, cut with an ellipsis when the node is narrower.
class Label : public Node {
public:
    explicit Label(std::string text = {}, float size = 0, int font = gfx::FONT_REGULAR);
    std::string text;
    float size = 0; // 0 = the theme's body size
    int font = gfx::FONT_REGULAR;
    Tone tone = Tone::Text;
    Align text_align = Align::Start;

protected:
    Size MeasureContent(Context& ctx) override;
    void DrawContent(Context& ctx) override;
};

/// An image from the texture cache by path, cropped to fill (cover) or letterboxed (fit). A
/// panel tint shows until it is decoded.
class ImageView : public Node {
public:
    explicit ImageView(std::string path = {});
    std::string path;
    bool cover = true;
    int max_side = 512;

protected:
    void DrawContent(Context& ctx) override;
};

/// A focusable picture with a title under it: a game, a console. Without a picture it shows
/// the title on a tinted card. It grows slightly while focused.
class Tile : public Node {
public:
    Tile(std::string title, std::string image_path, float image_width, float image_height);
    std::string title;
    std::string subtitle;
    std::string image_path;
    std::string badge; // small text in the picture's corner (a system name)
    float image_width, image_height;

    gfx::Rect FocusRect() const override;

protected:
    void DrawContent(Context& ctx) override;
    void OnTick(Context& ctx) override;

private:
    float grow_ = 0;
};

/// A focusable text button on a panel tint.
class Button : public Node {
public:
    explicit Button(std::string text, std::function<void()> action = {});
    std::string text;
    bool centered = true;
    bool selected = false; // drawn with the stronger tint (the current rail entry)

protected:
    Size MeasureContent(Context& ctx) override;
    void DrawContent(Context& ctx) override;
};

/// A settings row described as data. One struct covers every kind, so pages and plugins can
/// build rows from tables.
struct RowSpec {
    enum class Kind { Action, Value, Toggle, Choice, Slider };
    Kind kind = Kind::Action;
    std::string id;
    std::string label;
    std::string description;
    /// The description as it is now, read at every draw (a row with a `description` only).
    std::function<std::string()> live_description;

    std::function<void()> action;            // Action; also Confirm on a Value row
    std::function<std::string()> value;      // Value: the text at the right
    std::function<bool()> get_bool;          // Toggle
    std::function<void(bool)> set_bool;
    std::vector<std::string> options;        // Choice
    std::function<int()> get_index;
    std::function<void(int)> set_index;
    float min = 0, max = 1, step = 0.1f;     // Slider
    std::function<float()> get_number;
    std::function<void(float)> set_number;
    std::function<std::string(float)> format; // Slider: the value as text
};

class Row : public Node {
public:
    explicit Row(RowSpec spec);
    RowSpec spec;
    bool OnNav(input::Nav nav) override;
    bool WantsDrag(float x, float y) const override;
    void DragTo(float x, float y) override;
    float HeightForWidth(Context& ctx, float width) override;

protected:
    void DrawContent(Context& ctx) override;

private:
    // How the row lays out at a width under ROW_STACK_WIDTH.
    struct Stack {
        bool value_below = false; // the value (a slider's track) on a line of its own
        float height = 0;
    };
    Stack StackFor(Context& ctx, float width) const;
    std::string ValueText() const;
    void DrawStacked(Context& ctx);
    void DrawSlider(Context& ctx, const gfx::Rect& area, float text_y, float text_h);
    void Step(int direction);
    gfx::Rect track_; // a slider's track as last drawn
};

/// A heading above a group of rows or a shelf.
std::unique_ptr<Label> MakeHeading(std::string text);
/// A vertical scrolling list.
std::unique_ptr<Node> MakeList(float gap = 8);
/// A horizontal scrolling shelf of tiles.
std::unique_ptr<Node> MakeShelf(float gap = 16);

} // namespace den::ui
