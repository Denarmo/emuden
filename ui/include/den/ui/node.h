// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "den/gfx/canvas.h"
#include "den/gfx/images.h"
#include "den/input/input.h"
#include "den/ui/scroller.h"
#include "den/ui/theme.h"

namespace den::ui {

/// What a node needs to measure and draw itself. Built by the app once per frame.
struct Context {
    gfx::Canvas* canvas = nullptr;
    gfx::TextureCache* textures = nullptr;
    const Theme* theme = nullptr;
    input::PadFamily family = input::PadFamily::Keyboard;
    float dt = 0;      // seconds since the last frame
    double time = 0;   // seconds since start
    gfx::Text& Text() const {
        return canvas->GetText();
    }
};

struct Size {
    float w = 0, h = 0;
};

enum class Axis { Row, Column };
enum class Align { Start, Center, End, Stretch };
constexpr float AUTO = -1.0f;

/// Moves `value` towards `target` so that most of the distance is covered in about
/// 1/`rate` seconds, independent of the frame rate.
float Approach(float value, float target, float dt, float rate = 14.0f);

/// A retained node: a box that lays its children out along an axis (a small flexbox), may
/// scroll along it, may take focus, and draws itself. Widgets derive from it.
class Node {
public:
    Node() = default;
    virtual ~Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // Tree.
    Node* Add(std::unique_ptr<Node> child);
    template <typename T, typename... Args>
    T* Emplace(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = node.get();
        Add(std::move(node));
        return raw;
    }
    void Clear();
    const std::vector<std::unique_ptr<Node>>& Children() const {
        return children_;
    }
    Node* Parent() const {
        return parent_;
    }

    // Style: plain fields, set them and the next frame shows it.
    std::string id;    // for automation and lookups
    std::string label; // what the automation endpoint reports for the focused node
    Axis axis = Axis::Column;
    float gap = 0;
    float pad_left = 0, pad_top = 0, pad_right = 0, pad_bottom = 0;
    float width = AUTO, height = AUTO;
    float grow = 0;                 // share of the parent's free space along its axis
    Align align = Align::Stretch;   // children across the axis
    Align justify = Align::Start;   // children along the axis when there is free space
    gfx::Color background{0, 0, 0, 0};
    float radius = 0;
    bool visible = true;
    bool focusable = false;
    bool scrollable = false; // along `axis`; children keep their size and the node clips
    bool chrome = false;     // navigation around the content: never the page's first focus
    bool snap = false;       // a scrolling node whose flings come to rest on a child's edge
    float scroll_margin = 48; // room kept around the focused child when scrolling to it
    std::function<void()> on_activate;
    std::function<void()> on_focus;

    void SetPadding(float all) {
        pad_left = pad_top = pad_right = pad_bottom = all;
    }
    void SetPadding(float horizontal, float vertical) {
        pad_left = pad_right = horizontal;
        pad_top = pad_bottom = vertical;
    }

    // Layout results.
    gfx::Rect rect;
    bool focused = false;
    float scroll = 0, content_extent = 0; // `scroll` is what the last layout used
    /// The motion of a scrolling node: drags, flings, wheel and focus moves all go through it.
    Scroller scroller;
    /// Where a snapping node's flings may rest (filled by the layout).
    const std::vector<float>& SnapPoints() const {
        return snap_points_;
    }

    Size Measure(Context& ctx);
    void Arrange(Context& ctx, const gfx::Rect& area);
    void Draw(Context& ctx);
    /// What a scrolling node shows of `clip`: its own length along its axis, opened by the
    /// selection border's width at an end it rests on.
    gfx::Rect ScrollWindow(const gfx::Rect& clip) const;
    /// Advances animations in the whole subtree.
    void Tick(Context& ctx);

    /// Where the selection border goes when this node has focus.
    virtual gfx::Rect FocusRect() const {
        return rect;
    }
    virtual float FocusRadius(const Theme& theme) const {
        return radius > 0 ? radius : theme.radius;
    }
    /// The focused node sees navigation first; true when it used the event.
    virtual bool OnNav(input::Nav nav) {
        return false;
    }

    Node* FindById(const std::string& wanted);
    /// Every visible focusable node below (and including) this one.
    void CollectFocusable(std::vector<Node*>& out);
    /// The deepest visible node at a point that satisfies `pred`.
    Node* HitTest(float x, float y, const std::function<bool(const Node&)>& pred);

protected:
    /// The size of the content without padding. The default lays the children out.
    virtual Size MeasureContent(Context& ctx);
    /// Draws the node's own content, between its background and its children.
    virtual void DrawContent(Context& ctx) {}
    virtual void OnTick(Context& ctx) {}

private:
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    Size measured_;
    std::vector<float> snap_points_;
};

/// A plain container with an axis; shorthand for building trees.
std::unique_ptr<Node> MakeBox(Axis axis, float gap = 0);

} // namespace den::ui
