// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "den/ui/node.h"

namespace den::ui {

class Stage;

/// A button hint shown in the bar at the bottom: the glyph of `nav` and what it does.
struct Hint {
    input::Nav nav = input::Nav::None;
    std::string text;
};

using Params = std::map<std::string, std::string>;

/// The focusable node to move to from `from` in a direction, or null. Nodes that share a
/// parent with `from` are preferred over nodes further away in the tree, so a shelf keeps
/// its focus until its end.
Node* FindFocusTarget(Node& root, Node* from, input::Nav direction);

/// One screen on the router's stack. A page builds a node tree; the kit handles focus,
/// scrolling, pointer input and drawing. A page that draws itself (the player) overrides Draw.
class Page {
public:
    virtual ~Page() = default;

    /// Fills Root(). Called when the page is first shown and again after Rebuild().
    virtual void Build() = 0;
    /// The page became the top of the stack (pushed, or the page above it was popped).
    virtual void OnShow() {}
    virtual void OnHide() {}
    /// Once per frame while on top, before layout.
    virtual void Update(Context& ctx) {}
    /// Raw events before the kit sees them; true swallows the event.
    virtual bool OnEvent(const input::Event& event) {
        return false;
    }
    /// Navigation the focused node did not use; true swallows it.
    virtual bool OnNav(input::Nav nav) {
        return false;
    }
    virtual std::vector<Hint> Hints() const;
    /// True hides the hint bar and gives the page the whole window.
    virtual bool Immersive() const {
        return false;
    }
    virtual void Draw(Context& ctx, const gfx::Rect& area);

    Node& Root() {
        return root_;
    }
    Node* Focus() const {
        return focus_;
    }
    void SetFocus(Node* node, bool scroll_to = true);
    /// Rebuilds the tree before the next frame; focus returns to the node with the same id.
    void Rebuild() {
        rebuild_ = true;
    }
    Stage& GetStage() const {
        return *stage_;
    }
    const std::string& Route() const {
        return route_;
    }
    const Params& GetParams() const {
        return params_;
    }
    std::string Param(const std::string& key) const;

private:
    friend class Stage;
    void Prepare();
    void Dispatch(const input::Event& event, float scale);
    void Activate(Node* node);
    void EnsureVisible(Node* node);

    Node root_;
    Node* focus_ = nullptr;
    Stage* stage_ = nullptr;
    std::string route_;
    Params params_;
    bool built_ = false, rebuild_ = false;
    float enter_ = 0;
    // Pointer state.
    Node* press_node_ = nullptr;
    float press_x_ = 0, press_y_ = 0, last_x_ = 0, last_y_ = 0;
    bool pressed_ = false, dragging_ = false;
};

/// The router and everything drawn above pages: hint bar, toasts, the confirm dialog.
class Stage {
public:
    using Factory = std::function<std::unique_ptr<Page>()>;

    Stage();
    ~Stage();

    void Register(const std::string& route, Factory factory);
    bool HasRoute(const std::string& route) const;
    // Navigation takes effect before the next event or frame, so it is safe from callbacks.
    void Push(const std::string& route, Params params = {});
    void Replace(const std::string& route, Params params = {});
    void Pop();
    void PopToRoot();
    Page* Top() const;
    std::size_t Depth() const;

    void Toast(std::string text, float seconds = 3.0f);
    /// A modal question. `on_yes` runs when the confirm button is chosen.
    void Confirm(std::string title, std::string message, std::string yes_label,
                 std::function<void()> on_yes, std::string no_label = "Cancel");
    bool DialogOpen() const;

    void HandleEvent(const input::Event& event);
    /// Ticks, lays out and draws the top page and the overlays. The canvas must be begun.
    void Frame(Context& ctx);

    /// Route, focused node, dialog and toasts as JSON (the automation endpoint's "state").
    std::string StateJson() const;
    /// Back on the first page.
    std::function<void()> on_back_at_root;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace den::ui
