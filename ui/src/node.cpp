// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "den/ui/node.h"

namespace den::ui {

float Approach(float value, float target, float dt, float rate) {
    const float next = target + (value - target) * std::exp(-rate * dt);
    return std::fabs(next - target) < 0.05f ? target : next;
}

Node* Node::Add(std::unique_ptr<Node> child) {
    child->parent_ = this;
    children_.push_back(std::move(child));
    return children_.back().get();
}

void Node::Clear() {
    children_.clear();
    snap_points_.clear();
    scroller.Jump(0);
    scroll = 0;
}

Size Node::MeasureContent(Context& ctx) {
    Size size;
    int shown = 0;
    for (auto& child : children_) {
        if (!child->visible) {
            continue;
        }
        const Size c = child->Measure(ctx);
        if (axis == Axis::Row) {
            size.w += c.w;
            size.h = std::max(size.h, c.h);
        } else {
            size.h += c.h;
            size.w = std::max(size.w, c.w);
        }
        ++shown;
    }
    if (shown > 1) {
        (axis == Axis::Row ? size.w : size.h) += gap * static_cast<float>(shown - 1);
    }
    return size;
}

Size Node::Measure(Context& ctx) {
    if (!visible) {
        return measured_ = Size{};
    }
    Size size = MeasureContent(ctx);
    size.w += pad_left + pad_right;
    size.h += pad_top + pad_bottom;
    if (scrollable) {
        // A scrolling node does not ask for its whole content.
        content_extent = axis == Axis::Row ? size.w : size.h;
        (axis == Axis::Row ? size.w : size.h) = 0;
    }
    if (width >= 0) {
        size.w = width;
    }
    if (height >= 0) {
        size.h = height;
    }
    return measured_ = size;
}

float Node::HeightForWidth(Context& ctx, float w) {
    // A plain column of wrapping children grows with them.
    if (axis != Axis::Column || scrollable || height >= 0 || align != Align::Stretch || !visible) {
        return -1;
    }
    const float inner = std::max(0.0f, w - pad_left - pad_right);
    float total = 0;
    int shown = 0;
    bool changed = false;
    for (auto& child : children_) {
        if (!child->visible) {
            continue;
        }
        const float h = child->width < 0 ? child->HeightForWidth(ctx, inner) : -1.0f;
        changed = changed || h >= 0;
        total += h >= 0 ? h : child->measured_.h;
        ++shown;
    }
    if (!changed) {
        return -1;
    }
    return total + gap * static_cast<float>(std::max(0, shown - 1)) + pad_top + pad_bottom;
}

void Node::Arrange(Context& ctx, const gfx::Rect& area) {
    rect = area;
    const bool row = axis == Axis::Row;
    const float inner_x = area.x + pad_left, inner_y = area.y + pad_top;
    const float inner_w = std::max(0.0f, area.w - pad_left - pad_right);
    const float inner_h = std::max(0.0f, area.h - pad_top - pad_bottom);
    const float main_size = row ? inner_w : inner_h;
    const float cross_size = row ? inner_h : inner_w;

    float total = 0, total_grow = 0;
    int shown = 0;
    for (auto& child : children_) {
        if (!child->visible) {
            continue;
        }
        if (!row && align == Align::Stretch && child->width < 0) {
            if (const float h = child->HeightForWidth(ctx, cross_size); h >= 0) {
                child->measured_.h = h;
            }
        }
        total += row ? child->measured_.w : child->measured_.h;
        total_grow += child->grow;
        ++shown;
    }
    if (shown == 0) {
        return;
    }
    total += gap * static_cast<float>(shown - 1);
    float free_space = main_size - total;
    float cursor = row ? inner_x : inner_y;

    if (scrollable) {
        content_extent = total + (row ? pad_left + pad_right : pad_top + pad_bottom); // wrapped rows included
        const float limit = std::max(0.0f, total - main_size);
        scroller.SetBounds(limit, main_size);
        // Past the ends while the rubber band is stretched, so not clamped here.
        scroll = scroller.Position();
        cursor -= scroll;
        snap_points_.clear();
        if (snap) {
            float offset = 0;
            for (auto& child : children_) {
                if (child->visible) {
                    snap_points_.push_back(std::min(offset, limit));
                    offset += (row ? child->measured_.w : child->measured_.h) + gap;
                }
            }
        }
        free_space = 0;
    } else if (total_grow <= 0 && free_space > 0) {
        if (justify == Align::Center) {
            cursor += free_space * 0.5f;
        } else if (justify == Align::End) {
            cursor += free_space;
        }
    }

    for (auto& child : children_) {
        if (!child->visible) {
            continue;
        }
        float main = row ? child->measured_.w : child->measured_.h;
        if (total_grow > 0 && child->grow > 0) {
            main = std::max(0.0f, main + free_space * child->grow / total_grow);
        }
        float cross = row ? child->measured_.h : child->measured_.w;
        const bool fixed_cross = (row ? child->height : child->width) >= 0;
        float cross_pos = row ? inner_y : inner_x;
        if (align == Align::Stretch && !fixed_cross) {
            cross = cross_size;
        } else if (align == Align::Center) {
            cross_pos += (cross_size - cross) * 0.5f;
        } else if (align == Align::End) {
            cross_pos += cross_size - cross;
        }
        child->Arrange(ctx, row ? gfx::Rect{cursor, cross_pos, main, cross}
                                : gfx::Rect{cross_pos, cursor, cross, main});
        cursor += main + gap;
    }
}

void Node::Tick(Context& ctx) {
    if (scrollable) {
        scroller.Tick(ctx.dt);
    }
    OnTick(ctx);
    for (auto& child : children_) {
        child->Tick(ctx);
    }
}

gfx::Rect Node::ScrollWindow(const gfx::Rect& clip) const {
    // At rest on an end nothing is scrolled out there, so the window opens by the border's
    // width and the first or last child keeps its whole selection border.
    constexpr float BORDER = 8.0f;
    const float before = scroller.Position() <= 0.5f ? BORDER : 0.0f;
    const float after = scroller.Position() >= scroller.Max() - 0.5f ? BORDER : 0.0f;
    if (axis == Axis::Row) {
        return clip.Intersect(gfx::Rect{rect.x - before, clip.y, rect.w + before + after, clip.h});
    }
    return clip.Intersect(gfx::Rect{clip.x, rect.y - before, clip.w, rect.h + before + after});
}

void Node::Draw(Context& ctx) {
    if (!visible) {
        return;
    }
    const gfx::Rect clip = ctx.canvas->CurrentClip();
    // Tiles grow a little when focused, so the cull keeps a margin.
    if (rect.x - 40 > clip.Right() || rect.Right() + 40 < clip.x || rect.y - 40 > clip.Bottom() ||
        rect.Bottom() + 40 < clip.y) {
        return;
    }
    // Half a heading at the edge of a list reads as lying under what is drawn there.
    if (whole && (rect.y < clip.y - 0.5f || rect.Bottom() > clip.Bottom() + 0.5f)) {
        return;
    }
    if (background.a > 0) {
        ctx.canvas->FillRect(rect, background, radius);
    }
    DrawContent(ctx);
    if (children_.empty()) {
        return;
    }
    if (scrollable) {
        // Clip along the scroll axis only, so the selection border and focus growth of a
        // child are not cut across it.
        ctx.canvas->PushClip(ScrollWindow(clip));
    }
    for (auto& child : children_) {
        child->Draw(ctx);
    }
    if (scrollable) {
        ctx.canvas->PopClip();
    }
}

Node* Node::FindById(const std::string& wanted) {
    if (id == wanted) {
        return this;
    }
    for (auto& child : children_) {
        if (Node* found = child->FindById(wanted)) {
            return found;
        }
    }
    return nullptr;
}

void Node::CollectFocusable(std::vector<Node*>& out) {
    if (!visible) {
        return;
    }
    if (focusable) {
        out.push_back(this);
    }
    for (auto& child : children_) {
        child->CollectFocusable(out);
    }
}

Node* Node::HitTest(float x, float y, const std::function<bool(const Node&)>& pred) {
    if (!visible || !rect.Contains(x, y)) {
        return nullptr;
    }
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (Node* hit = (*it)->HitTest(x, y, pred)) {
            return hit;
        }
    }
    return pred(*this) ? this : nullptr;
}

std::unique_ptr<Node> MakeBox(Axis axis, float gap) {
    auto node = std::make_unique<Node>();
    node->axis = axis;
    node->gap = gap;
    return node;
}

} // namespace den::ui
