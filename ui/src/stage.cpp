// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/base/json.h"
#include "den/base/log.h"
#include "den/ui/page.h"
#include "den/ui/selection.h"

namespace den::ui {

namespace {

using input::Nav;
constexpr float HINT_BAR_HEIGHT = 48;

std::vector<std::string> Wrap(gfx::Text& text, const std::string& in, float size, float width) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush_word = [&] {
        if (word.empty()) {
            return;
        }
        const std::string joined = line.empty() ? word : line + " " + word;
        if (!line.empty() && text.Measure(joined, size) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = joined;
        }
        word.clear();
    };
    for (const char c : in) {
        if (c == ' ' || c == '\n') {
            flush_word();
            if (c == '\n') {
                lines.push_back(line);
                line.clear();
            }
        } else {
            word.push_back(c);
        }
    }
    flush_word();
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace

struct Stage::Impl {
    struct Pending {
        enum class Op { Push, Replace, Pop, PopToRoot } op;
        std::string route;
        Params params;
    };
    struct ToastItem {
        std::string text;
        float left = 0;
    };
    struct Dialog {
        std::string title, message, yes, no;
        std::function<void()> on_yes;
        int selected = 0; // 0 = no, 1 = yes
        gfx::Rect yes_rect, no_rect;
    };

    std::map<std::string, Factory> routes;
    std::vector<std::unique_ptr<Page>> stack;
    std::vector<Pending> pending;
    std::vector<ToastItem> toasts;
    std::unique_ptr<Dialog> dialog;
    float scale = 1.0f;

    void Show(Page* page) {
        if (page) {
            page->Prepare();
            page->OnShow();
        }
    }

    std::unique_ptr<Page> Make(Stage& stage, const std::string& route, Params params) {
        const auto it = routes.find(route);
        if (it == routes.end()) {
            log::Error("ui", "no screen is registered for the route '{}'", route);
            return nullptr;
        }
        std::unique_ptr<Page> page = it->second();
        page->stage_ = &stage;
        page->route_ = route;
        page->params_ = std::move(params);
        return page;
    }

    void Apply(Stage& stage) {
        // Operations queued while applying (a page pushing from OnShow) run in the same pass.
        for (std::size_t i = 0; i < pending.size(); ++i) {
            Pending op = std::move(pending[i]);
            switch (op.op) {
            case Pending::Op::Push:
            case Pending::Op::Replace: {
                std::unique_ptr<Page> page = Make(stage, op.route, std::move(op.params));
                if (!page) {
                    break;
                }
                if (!stack.empty()) {
                    stack.back()->OnHide();
                    if (op.op == Pending::Op::Replace) {
                        stack.pop_back();
                    }
                }
                stack.push_back(std::move(page));
                Show(stack.back().get());
                break;
            }
            case Pending::Op::Pop:
                if (stack.size() > 1) {
                    stack.back()->OnHide();
                    stack.pop_back();
                    Show(stack.back().get());
                } else if (stage.on_back_at_root) {
                    stage.on_back_at_root();
                }
                break;
            case Pending::Op::PopToRoot:
                if (stack.size() > 1) {
                    stack.back()->OnHide();
                    stack.resize(1);
                    Show(stack.back().get());
                }
                break;
            }
        }
        pending.clear();
    }

    void DrawHints(Context& ctx, Page& page) {
        const Theme& theme = *ctx.theme;
        gfx::Canvas& canvas = *ctx.canvas;
        gfx::Text& text = ctx.Text();
        const float y = canvas.Height() - HINT_BAR_HEIGHT;
        float x = canvas.Width() - theme.margin;
        const std::vector<Hint> hints = page.Hints();
        for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
            const std::string glyph = input::GlyphLabel(ctx.family, it->nav);
            const float text_w = text.Measure(it->text, theme.text_small + 1);
            const float glyph_w = std::max(26.0f, text.Measure(glyph, theme.text_small, gfx::FONT_BOLD) + 16);
            x -= text_w;
            canvas.DrawText(it->text, x, y + 13, theme.text_small + 1, theme.fg);
            x -= glyph_w + 8;
            const gfx::Rect badge{x, y + 9, glyph_w, 26};
            canvas.FillRect(badge, theme.panel_strong, 13);
            const float gw = text.Measure(glyph, theme.text_small, gfx::FONT_BOLD);
            canvas.DrawText(glyph, badge.x + (badge.w - gw) * 0.5f, badge.y + 4, theme.text_small,
                            theme.fg, gfx::FONT_BOLD);
            x -= 22;
        }
    }

    void DrawToasts(Context& ctx) {
        const Theme& theme = *ctx.theme;
        float y = 24;
        for (ToastItem& toast : toasts) {
            toast.left -= ctx.dt;
            const float w = std::min(ctx.canvas->Width() - 64, ctx.Text().Measure(toast.text, theme.text_body) + 40);
            const gfx::Rect box{(ctx.canvas->Width() - w) * 0.5f, y, w, 44};
            ctx.canvas->FillRect(box, theme.dialog, 22);
            ctx.canvas->StrokeRect(box, theme.panel_strong, 1.5f, 22);
            ctx.canvas->DrawText(ctx.Text().Elide(toast.text, theme.text_body, w - 40), box.x + 20, box.y + 10,
                                 theme.text_body, theme.fg);
            y += 52;
        }
        std::erase_if(toasts, [](const ToastItem& t) { return t.left <= 0; });
    }

    void DrawDialog(Context& ctx) {
        const Theme& theme = *ctx.theme;
        gfx::Canvas& canvas = *ctx.canvas;
        canvas.FillRect(gfx::Rect{0, 0, canvas.Width(), canvas.Height()}, theme.scrim);
        const float w = std::min(560.0f, canvas.Width() - 48);
        const std::vector<std::string> lines = Wrap(ctx.Text(), dialog->message, theme.text_body, w - 56);
        const float h = 64 + 28 * static_cast<float>(lines.size()) + 96;
        const gfx::Rect box{(canvas.Width() - w) * 0.5f, (canvas.Height() - h) * 0.5f, w, h};
        canvas.FillRect(box, theme.dialog, theme.radius + 6);
        canvas.DrawText(dialog->title, box.x + 28, box.y + 22, theme.text_heading, theme.fg, gfx::FONT_BOLD);
        float y = box.y + 64;
        for (const std::string& line : lines) {
            canvas.DrawText(line, box.x + 28, y, theme.text_body, theme.text_dim);
            y += 28;
        }
        const float button_w = (w - 56 - 16) * 0.5f;
        dialog->no_rect = gfx::Rect{box.x + 28, box.Bottom() - 72, button_w, 48};
        dialog->yes_rect = gfx::Rect{dialog->no_rect.Right() + 16, dialog->no_rect.y, button_w, 48};
        const auto button = [&](const gfx::Rect& r, const std::string& label, bool selected) {
            canvas.FillRect(r, selected ? theme.panel_strong : theme.panel, theme.radius);
            const float tw = ctx.Text().Measure(label, theme.text_body, gfx::FONT_BOLD);
            canvas.DrawText(label, r.x + (r.w - tw) * 0.5f, r.y + 12, theme.text_body, theme.fg, gfx::FONT_BOLD);
            if (selected) {
                DrawSelectionBorder(canvas, r, theme.radius, ctx.time);
            }
        };
        button(dialog->no_rect, dialog->no, dialog->selected == 0);
        button(dialog->yes_rect, dialog->yes, dialog->selected == 1);
    }

    void Choose(bool yes) {
        std::function<void()> action = yes ? std::move(dialog->on_yes) : std::function<void()>();
        dialog.reset();
        if (action) {
            action();
        }
    }

    void DialogEvent(const input::Event& event) {
        using Type = input::Event::Type;
        if (event.type == Type::Nav) {
            if (event.nav == Nav::Left || event.nav == Nav::Right) {
                dialog->selected = event.nav == Nav::Right ? 1 : 0;
            } else if (event.nav == Nav::Confirm && !event.repeat) {
                Choose(dialog->selected == 1);
            } else if (event.nav == Nav::Back) {
                Choose(false);
            }
        } else if (event.type == Type::PointerUp) {
            const float x = event.x / scale, y = event.y / scale;
            if (dialog->yes_rect.Contains(x, y)) {
                Choose(true);
            } else if (dialog->no_rect.Contains(x, y)) {
                Choose(false);
            }
        }
    }
};

Stage::Stage() : impl_(std::make_unique<Impl>()) {}
Stage::~Stage() = default;

void Stage::Register(const std::string& route, Factory factory) {
    impl_->routes[route] = std::move(factory);
}

bool Stage::HasRoute(const std::string& route) const {
    return impl_->routes.count(route) != 0;
}

void Stage::Push(const std::string& route, Params params) {
    impl_->pending.push_back({Impl::Pending::Op::Push, route, std::move(params)});
}

void Stage::Replace(const std::string& route, Params params) {
    impl_->pending.push_back({Impl::Pending::Op::Replace, route, std::move(params)});
}

void Stage::Pop() {
    impl_->pending.push_back({Impl::Pending::Op::Pop, {}, {}});
}

void Stage::PopToRoot() {
    impl_->pending.push_back({Impl::Pending::Op::PopToRoot, {}, {}});
}

Page* Stage::Top() const {
    return impl_->stack.empty() ? nullptr : impl_->stack.back().get();
}

std::size_t Stage::Depth() const {
    return impl_->stack.size();
}

void Stage::Toast(std::string text, float seconds) {
    log::Info("ui", "toast: {}", text);
    impl_->toasts.push_back({std::move(text), seconds});
    if (impl_->toasts.size() > 4) {
        impl_->toasts.erase(impl_->toasts.begin());
    }
}

void Stage::Confirm(std::string title, std::string message, std::string yes_label,
                    std::function<void()> on_yes, std::string no_label, bool yes_first) {
    impl_->dialog = std::make_unique<Impl::Dialog>();
    impl_->dialog->title = std::move(title);
    impl_->dialog->message = std::move(message);
    impl_->dialog->yes = std::move(yes_label);
    impl_->dialog->no = std::move(no_label);
    impl_->dialog->on_yes = std::move(on_yes);
    impl_->dialog->selected = yes_first ? 1 : 0;
}

bool Stage::DialogOpen() const {
    return impl_->dialog != nullptr;
}

void Stage::HandleEvent(const input::Event& event) {
    impl_->Apply(*this);
    if (impl_->dialog) {
        impl_->DialogEvent(event);
    } else if (Page* page = Top()) {
        page->Dispatch(event, impl_->scale);
    }
    impl_->Apply(*this);
}

void Stage::Frame(Context& ctx) {
    impl_->Apply(*this);
    impl_->scale = ctx.canvas->Scale();
    gfx::Canvas& canvas = *ctx.canvas;
    if (Page* page = Top()) {
        page->Prepare();
        page->Update(ctx);
        impl_->Apply(*this);
        page = Top();
        page->Prepare();
        const bool immersive = page->Immersive();
        const gfx::Rect area{0, 0, canvas.Width(), canvas.Height() - (immersive ? 0 : HINT_BAR_HEIGHT)};
        page->Draw(ctx, area);
        if (!immersive) {
            impl_->DrawHints(ctx, *page);
        }
    }
    impl_->DrawToasts(ctx);
    if (impl_->dialog) {
        impl_->DrawDialog(ctx);
    }
}

std::string Stage::StateJson() const {
    std::vector<std::pair<std::string, JsonValue>> members;
    const Page* page = Top();
    members.emplace_back("route", JsonString(page ? page->Route() : ""));
    members.emplace_back("depth", JsonNumber(static_cast<double>(Depth())));
    if (page && page->Focus()) {
        members.emplace_back("focus", JsonObject({{"id", JsonString(page->Focus()->id)},
                                                  {"label", JsonString(page->Focus()->label)}}));
    } else {
        members.emplace_back("focus", JsonValue{});
    }
    if (impl_->dialog) {
        members.emplace_back("dialog", JsonObject({{"title", JsonString(impl_->dialog->title)},
                                                   {"selected", JsonString(impl_->dialog->selected ? impl_->dialog->yes : impl_->dialog->no)}}));
    }
    std::vector<JsonValue> toasts;
    for (const auto& toast : impl_->toasts) {
        toasts.push_back(JsonString(toast.text));
    }
    members.emplace_back("toasts", JsonArray(std::move(toasts)));
    return WriteJson(JsonObject(std::move(members)));
}

} // namespace den::ui
