// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "den/ui/page.h"
#include "den/ui/selection.h"

namespace den::ui {

namespace {

using input::Nav;

bool IsDirection(Nav nav) {
    return nav == Nav::Up || nav == Nav::Down || nav == Nav::Left || nav == Nav::Right;
}

bool IsUnder(const Node* node, const Node* ancestor) {
    for (; node; node = node->Parent()) {
        if (node == ancestor) {
            return true;
        }
    }
    return false;
}

// A node in another scrolling container counts only while it is inside that container's
// window: what is scrolled out of a neighbouring shelf is not a place to jump to.
bool Reachable(const Node* candidate, const Node* from) {
    for (const Node* s = candidate->Parent(); s; s = s->Parent()) {
        if (!s->scrollable || IsUnder(from, s)) {
            continue;
        }
        const bool row = s->axis == Axis::Row;
        const float centre = row ? candidate->rect.CenterX() : candidate->rect.CenterY();
        const float start = row ? s->rect.x : s->rect.y;
        const float end = row ? s->rect.Right() : s->rect.Bottom();
        if (centre < start || centre > end) {
            return false;
        }
    }
    return true;
}

// Lower is better; negative when the candidate is not in that direction.
float Score(const gfx::Rect& from, const gfx::Rect& to, Nav direction) {
    float along = 0, across = 0;
    switch (direction) {
    case Nav::Right:
        if (to.CenterX() <= from.CenterX() + 1 || to.x < from.CenterX()) {
            return -1;
        }
        along = std::max(0.0f, to.x - from.Right());
        across = std::max({0.0f, to.y - from.Bottom(), from.y - to.Bottom()});
        break;
    case Nav::Left:
        if (to.CenterX() >= from.CenterX() - 1 || to.Right() > from.CenterX()) {
            return -1;
        }
        along = std::max(0.0f, from.x - to.Right());
        across = std::max({0.0f, to.y - from.Bottom(), from.y - to.Bottom()});
        break;
    case Nav::Down:
        if (to.CenterY() <= from.CenterY() + 1 || to.y < from.CenterY()) {
            return -1;
        }
        along = std::max(0.0f, to.y - from.Bottom());
        across = std::max({0.0f, to.x - from.Right(), from.x - to.Right()});
        break;
    case Nav::Up:
        if (to.CenterY() >= from.CenterY() - 1 || to.Bottom() > from.CenterY()) {
            return -1;
        }
        along = std::max(0.0f, from.y - to.Bottom());
        across = std::max({0.0f, to.x - from.Right(), from.x - to.Right()});
        break;
    default:
        return -1;
    }
    const float centre = direction == Nav::Left || direction == Nav::Right
                             ? std::fabs(to.CenterY() - from.CenterY())
                             : std::fabs(to.CenterX() - from.CenterX());
    return along + across * 4.0f + centre * 0.05f;
}

} // namespace

Node* FindFocusTarget(Node& root, Node* from, Nav direction) {
    std::vector<Node*> all;
    root.CollectFocusable(all);
    if (all.empty()) {
        return nullptr;
    }
    if (!from) {
        return all.front();
    }
    const gfx::Rect origin = from->rect;
    const Node* examined = from;
    for (Node* scope = from->Parent(); scope; scope = scope->Parent()) {
        Node* best = nullptr;
        float best_score = 0;
        for (Node* candidate : all) {
            if (candidate == from || !IsUnder(candidate, scope) || IsUnder(candidate, examined) ||
                !Reachable(candidate, from)) {
                continue;
            }
            const float score = Score(origin, candidate->rect, direction);
            if (score >= 0 && (!best || score < best_score)) {
                best = candidate;
                best_score = score;
            }
        }
        if (best) {
            return best;
        }
        examined = scope;
    }
    return nullptr;
}

std::vector<Hint> Page::Hints() const {
    return {{Nav::Confirm, "Select"}, {Nav::Back, "Back"}};
}

std::string Page::Param(const std::string& key) const {
    const auto it = params_.find(key);
    return it == params_.end() ? std::string() : it->second;
}

void Page::SetFocus(Node* node, bool scroll_to) {
    if (focus_ == node) {
        return;
    }
    if (focus_) {
        focus_->focused = false;
    }
    focus_ = node;
    if (node) {
        node->focused = true;
        if (scroll_to) {
            EnsureVisible(node);
        }
        if (node->on_focus) {
            node->on_focus();
        }
    }
}

void Page::EnsureVisible(Node* node) {
    for (Node* scroller = node->Parent(); scroller; scroller = scroller->Parent()) {
        if (!scroller->scrollable) {
            continue;
        }
        const bool row = scroller->axis == Axis::Row;
        const float view = row ? scroller->rect.w : scroller->rect.h;
        const float start = (row ? node->rect.x - scroller->rect.x : node->rect.y - scroller->rect.y) +
                            scroller->scroll;
        const float end = start + (row ? node->rect.w : node->rect.h);
        const float margin = std::min(scroller->scroll_margin, std::max(0.0f, (view - (end - start)) * 0.5f));
        if (start - margin < scroller->scroll_target) {
            scroller->scroll_target = start - margin;
        } else if (end + margin > scroller->scroll_target + view) {
            scroller->scroll_target = end + margin - view;
        }
        scroller->scroll_target = std::max(0.0f, scroller->scroll_target);
    }
}

void Page::Prepare() {
    if (built_ && !rebuild_) {
        return;
    }
    const std::string keep = focus_ ? focus_->id : std::string();
    focus_ = nullptr;
    press_node_ = nullptr;
    root_.Clear();
    Build();
    built_ = true;
    rebuild_ = false;
    Node* target = keep.empty() ? nullptr : root_.FindById(keep);
    if (!target || !target->focusable) {
        std::vector<Node*> all;
        root_.CollectFocusable(all);
        target = all.empty() ? nullptr : all.front();
        // Navigation chrome (the rail) gets the first focus only when nothing else can.
        for (Node* node : all) {
            bool chrome = false;
            for (const Node* n = node; n; n = n->Parent()) {
                chrome = chrome || n->chrome;
            }
            if (!chrome) {
                target = node;
                break;
            }
        }
    }
    SetFocus(target, false);
}

void Page::Activate(Node* node) {
    if (!node) {
        return;
    }
    if (!node->OnNav(Nav::Confirm) && node->on_activate) {
        node->on_activate();
    }
}

void Page::Dispatch(const input::Event& event, float scale) {
    using Type = input::Event::Type;
    if (OnEvent(event)) {
        return;
    }
    const float x = event.x / scale, y = event.y / scale;
    const auto scrollable = [](const Node& n) { return n.scrollable; };
    switch (event.type) {
    case Type::Nav: {
        if (focus_ && focus_->OnNav(event.nav)) {
            return;
        }
        if (OnNav(event.nav)) {
            return;
        }
        if (IsDirection(event.nav)) {
            if (Node* next = FindFocusTarget(root_, focus_, event.nav)) {
                SetFocus(next);
            }
        } else if (event.nav == Nav::Confirm && !event.repeat) {
            if (focus_ && focus_->on_activate) {
                focus_->on_activate();
            }
        } else if (event.nav == Nav::Back && !event.repeat) {
            stage_->Pop();
        }
        break;
    }
    case Type::PointerDown:
        pressed_ = true;
        dragging_ = false;
        press_x_ = last_x_ = x;
        press_y_ = last_y_ = y;
        press_node_ = root_.HitTest(x, y, [](const Node& n) { return n.focusable; });
        break;
    case Type::PointerMove: {
        if (!pressed_) {
            break;
        }
        const float dx = x - last_x_, dy = y - last_y_;
        last_x_ = x;
        last_y_ = y;
        if (!dragging_ && std::hypot(x - press_x_, y - press_y_) > 10) {
            dragging_ = true;
        }
        if (dragging_) {
            const bool horizontal = std::fabs(x - press_x_) > std::fabs(y - press_y_);
            for (Node* n = root_.HitTest(press_x_, press_y_, scrollable); n; n = n->Parent()) {
                if (n->scrollable && (n->axis == Axis::Row) == horizontal) {
                    n->ScrollBy(horizontal ? -dx : -dy);
                    break;
                }
            }
        }
        break;
    }
    case Type::PointerUp:
        if (pressed_ && !dragging_ && press_node_ && press_node_->rect.Contains(x, y)) {
            SetFocus(press_node_, false);
            Activate(press_node_);
        }
        pressed_ = false;
        dragging_ = false;
        break;
    case Type::Wheel: {
        Node* target = nullptr;
        for (Node* n = root_.HitTest(x, y, scrollable); n; n = n->Parent()) {
            if (n->scrollable && (!target || n->axis == Axis::Column)) {
                target = n;
                if (n->axis == Axis::Column) {
                    break;
                }
            }
        }
        if (target) {
            target->scroll_target -= (event.dy != 0 ? event.dy : -event.dx) * 90.0f;
        }
        break;
    }
    default:
        break;
    }
}

void Page::Draw(Context& ctx, const gfx::Rect& area) {
    enter_ = Approach(enter_, 1.0f, ctx.dt, 12.0f);
    gfx::Rect shifted = area;
    shifted.y += (1.0f - enter_) * 18.0f;
    root_.Tick(ctx);
    root_.Measure(ctx);
    root_.Arrange(ctx, shifted);
    root_.Draw(ctx);
    if (!focus_) {
        return;
    }
    // The border is clipped like the node it surrounds: along each scrolling ancestor's axis.
    gfx::Rect clip{0, 0, ctx.canvas->Width(), ctx.canvas->Height()};
    for (const Node* n = focus_->Parent(); n; n = n->Parent()) {
        if (!n->scrollable) {
            continue;
        }
        if (n->axis == Axis::Row) {
            clip = clip.Intersect(gfx::Rect{n->rect.x, clip.y, n->rect.w, clip.h});
        } else {
            clip = clip.Intersect(gfx::Rect{clip.x, n->rect.y, clip.w, n->rect.h});
        }
    }
    ctx.canvas->PushClip(clip);
    DrawSelectionBorder(*ctx.canvas, focus_->FocusRect(), focus_->FocusRadius(*ctx.theme), ctx.time);
    ctx.canvas->PopClip();
}

} // namespace den::ui
