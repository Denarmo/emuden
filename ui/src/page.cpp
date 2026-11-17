// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "den/ui/page.h"
#include "den/ui/selection.h"

namespace den::ui {

namespace {

using input::Nav;
constexpr float DRAG_THRESHOLD = 10; // units a finger moves before a press becomes a drag
constexpr float HOLD_SECONDS = 0.5f; // a finger resting this long is a hold, not a tap
constexpr float WHEEL_STEP = 90;     // units per wheel line

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

void CollectScrolls(const Node& node, std::vector<std::pair<std::string, float>>& out) {
    if (node.scrollable && !node.id.empty()) {
        out.emplace_back(node.id, node.scroller.Destination());
    }
    for (const auto& child : node.Children()) {
        CollectScrolls(*child, out);
    }
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
        float start = (row ? node->rect.x - scroller->rect.x : node->rect.y - scroller->rect.y) +
                      scroller->scroll;
        const float end = start + (row ? node->rect.w : node->rect.h);
        float margin = std::min(scroller->scroll_margin, std::max(0.0f, (view - (end - start)) * 0.5f));
        if (!row) {
            // What the list holds the node in (a shelf under its heading) is shown from its
            // start when it fits, so the heading comes with the row below it.
            const Node* group = node;
            while (group->Parent() != scroller) {
                group = group->Parent();
            }
            const float group_start = group->rect.y - scroller->rect.y + scroller->scroll;
            if (group != node && group_start < start && end - group_start <= view) {
                start = group_start;
                margin = std::min(margin, std::max(0.0f, (view - (end - start)) * 0.5f));
            }
        }
        // From where the node is heading, so held directions do not chase a moving window.
        const float heading = scroller->scroller.Destination();
        if (start - margin < heading) {
            scroller->scroller.ScrollTo(start - margin);
        } else if (end + margin > heading + view) {
            scroller->scroller.ScrollTo(end + margin - view);
        }
    }
}

void Page::ReleaseScrollers(double time) {
    for (Node* node : held_) {
        node->scroller.EndDrag(time, node->snap ? node->SnapPoints() : std::vector<float>());
    }
    held_.clear();
    drag_node_ = nullptr;
    value_node_ = nullptr;
}

void Page::Prepare() {
    if (built_ && !rebuild_) {
        return;
    }
    const std::string keep = focus_ ? focus_->id : std::string();
    // Where every scrolling node with an id is: the rebuilt one starts there.
    std::vector<std::pair<std::string, float>> scrolls;
    if (keep_scroll_) {
        CollectScrolls(root_, scrolls);
    }
    keep_scroll_ = true;
    // A finger on the page keeps its press, its drag or its slider through the rebuild, on
    // the nodes with the same ids; when one of them is gone it lets go.
    Node* const before[] = {press_node_, drag_node_, value_node_};
    std::string held_ids[3];
    for (int i = 0; i < 3; ++i) {
        held_ids[i] = before[i] ? before[i]->id : std::string();
    }
    focus_ = nullptr;
    press_node_ = nullptr;
    drag_node_ = nullptr;
    value_node_ = nullptr;
    held_.clear();
    root_.Clear();
    Build();
    built_ = true;
    rebuild_ = false;
    for (const auto& [id, position] : scrolls) {
        if (Node* node = root_.FindById(id); node && node->scrollable) {
            node->scroller.Jump(position);
        }
    }
    if (pressed_) {
        Node* found[3] = {};
        bool lost = false;
        for (int i = 0; i < 3; ++i) {
            found[i] = held_ids[i].empty() ? nullptr : root_.FindById(held_ids[i]);
            lost = lost || (before[i] && !found[i]);
        }
        if (lost || (found[1] && !found[1]->scrollable)) {
            pressed_ = dragging_ = false;
        } else {
            press_node_ = found[0];
            drag_node_ = found[1];
            value_node_ = found[2]; // the drag takes hold of the new list at the next move
        }
    }
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
    reveal_ = true;
}

void Page::Activate(Node* node) {
    if (!node) {
        return;
    }
    if (!node->OnNav(Nav::Confirm) && node->on_activate) {
        node->on_activate();
    }
}

void Page::Dispatch(const input::Event& event, float scale, double time) {
    using Type = input::Event::Type;
    if (OnEvent(event)) {
        return;
    }
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
    default:
        Pointer(event, scale, time);
        break;
    }
}

void Page::Pointer(const input::Event& event, float scale, double time) {
    using Type = input::Event::Type;
    const float x = event.x / scale, y = event.y / scale;
    const auto scrollable = [](const Node& n) { return n.scrollable; };
    switch (event.type) {
    case Type::PointerDown:
        ReleaseScrollers(time);
        pressed_ = true;
        dragging_ = false;
        stopped_fling_ = false;
        press_x_ = last_x_ = x;
        press_y_ = last_y_ = y;
        hold_ = 0;
        hold_fired_ = false;
        press_node_ = root_.HitTest(x, y, [](const Node& n) { return n.focusable; });
        // A finger on moving content stops it, and that press is not a tap.
        for (Node* n = root_.HitTest(x, y, scrollable); n; n = n->Parent()) {
            if (n->scrollable && n->scroller.Kinetic()) {
                n->scroller.BeginDrag(time);
                held_.push_back(n);
                stopped_fling_ = true;
            }
        }
        break;
    case Type::PointerMove: {
        if (!pressed_) {
            break;
        }
        const float dx = x - last_x_, dy = y - last_y_;
        last_x_ = x;
        last_y_ = y;
        if (!dragging_ && std::hypot(x - press_x_, y - press_y_) > DRAG_THRESHOLD) {
            // The gesture locks to one axis and one node here: the nearest scrolling node
            // along the finger's direction (a shelf inside the page), else the nearest that
            // can move at all.
            dragging_ = true;
            const bool horizontal = std::fabs(x - press_x_) > std::fabs(y - press_y_);
            if (horizontal && press_node_ && press_node_->WantsDrag(press_x_, press_y_)) {
                // A slider taken by its track: the value follows the finger, nothing scrolls.
                value_node_ = press_node_;
                SetFocus(press_node_, false);
                value_node_->DragTo(x, y);
                break;
            }
            Node* other = nullptr;
            for (Node* n = root_.HitTest(press_x_, press_y_, scrollable); n && !drag_node_; n = n->Parent()) {
                if (!n->scrollable || n->scroller.Max() <= 0) {
                    continue;
                }
                if ((n->axis == Axis::Row) == horizontal) {
                    drag_node_ = n;
                } else if (!other) {
                    other = n;
                }
            }
            drag_node_ = drag_node_ ? drag_node_ : other;
            if (drag_node_ && !drag_node_->scroller.Dragging()) {
                drag_node_->scroller.BeginDrag(time);
                held_.push_back(drag_node_);
            }
            break; // the content follows the finger from here, without a jump
        }
        if (dragging_ && value_node_) {
            value_node_->DragTo(x, y); // may rebuild the page; the drag goes on with the new row
        } else if (dragging_ && drag_node_) {
            if (!drag_node_->scroller.Dragging()) { // the page was rebuilt under the finger
                drag_node_->scroller.BeginDrag(time);
                held_.push_back(drag_node_);
            }
            drag_node_->scroller.DragBy(drag_node_->axis == Axis::Row ? -dx : -dy, time);
        }
        break;
    }
    case Type::PointerUp:
        if (pressed_ && !dragging_ && !stopped_fling_ && !hold_fired_ && press_node_ &&
            press_node_->rect.Contains(x, y)) {
            SetFocus(press_node_, false);
            if (press_node_->WantsDrag(x, y)) {
                press_node_->DragTo(x, y); // a tap on the track puts the value there
            } else {
                Activate(press_node_);
            }
        }
        ReleaseScrollers(time);
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
            target->scroller.ScrollBy(-(event.dy != 0 ? event.dy : -event.dx) * WHEEL_STEP);
        }
        break;
    }
    default:
        break;
    }
}

void Page::Draw(Context& ctx, const gfx::Rect& area) {
    enter_ = Approach(enter_, 1.0f, ctx.dt, 12.0f);
    // A finger resting on a node that answers to it.
    if (pressed_ && !dragging_ && !stopped_fling_ && !hold_fired_ && press_node_ && press_node_->on_hold) {
        hold_ += ctx.dt;
        if (hold_ >= HOLD_SECONDS) {
            hold_fired_ = true;
            SetFocus(press_node_, false);
            // The callback may rebuild the page, which drops the node.
            const std::function<void()> held = press_node_->on_hold;
            held();
        }
    }
    gfx::Rect shifted = area;
    shifted.y += (1.0f - enter_) * 18.0f;
    root_.Tick(ctx);
    root_.Measure(ctx);
    root_.Arrange(ctx, shifted);
    if (reveal_) {
        // The first layout after a build: the kept focus is brought into view at once.
        reveal_ = false;
        if (focus_) {
            EnsureVisible(focus_);
            for (Node* n = focus_->Parent(); n; n = n->Parent()) {
                if (n->scrollable) {
                    n->scroller.Jump(n->scroller.Destination());
                }
            }
            root_.Arrange(ctx, shifted);
        }
    }
    root_.Draw(ctx);
    if (!focus_) {
        return;
    }
    // The border is clipped like the node it surrounds: along each scrolling ancestor's axis.
    gfx::Rect clip{0, 0, ctx.canvas->Width(), ctx.canvas->Height()};
    for (const Node* n = focus_->Parent(); n; n = n->Parent()) {
        if (n->scrollable) {
            clip = n->ScrollWindow(clip);
        }
    }
    ctx.canvas->PushClip(clip);
    DrawSelectionBorder(*ctx.canvas, focus_->FocusRect(), focus_->FocusRadius(*ctx.theme), ctx.time, ctx.theme->accent);
    ctx.canvas->PopClip();
}

} // namespace den::ui
