// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <set>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/library/artwork.h"
#include "game_widgets.h"
#include "games.h"

namespace den::app {

namespace {

constexpr int BLUR_SIDE = 64;

std::string BlurKey(const std::string& path) {
    return "blur:" + path;
}

// One horizontal and one vertical box pass; three rounds come close to a Gaussian.
void BoxBlur(gfx::ImageData& image, int radius) {
    const int w = image.width, h = image.height;
    std::vector<std::uint8_t> out(image.rgba.size());
    for (int pass = 0; pass < 2; ++pass) {
        const bool horizontal = pass == 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int sum[4] = {0, 0, 0, 0};
                for (int d = -radius; d <= radius; ++d) {
                    const int sx = horizontal ? std::clamp(x + d, 0, w - 1) : x;
                    const int sy = horizontal ? y : std::clamp(y + d, 0, h - 1);
                    const std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(sy) * w + sx) * 4];
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += p[c];
                    }
                }
                std::uint8_t* q = &out[(static_cast<std::size_t>(y) * w + x) * 4];
                for (int c = 0; c < 4; ++c) {
                    q[c] = static_cast<std::uint8_t>(sum[c] / (2 * radius + 1));
                }
            }
        }
        image.rgba.swap(out);
    }
}

std::set<std::string>& PendingBlurs() {
    static std::set<std::string> pending;
    return pending;
}

void RequestBlur(const std::string& path) {
    Services& svc = Svc();
    const std::string key = BlurKey(path);
    if (svc.textures->StateOf(key) == gfx::TextureCache::State::Ready || !PendingBlurs().insert(path).second) {
        return;
    }
    auto image = std::make_shared<gfx::ImageData>();
    svc.jobs->Run(
        "blur cover",
        [image, path](Job&) {
            auto loaded = gfx::LoadImageFile(PathFromUtf8(path));
            if (!loaded) {
                return loaded.status();
            }
            gfx::ShrinkImage(*loaded, BLUR_SIDE);
            for (int round = 0; round < 3; ++round) {
                BoxBlur(*loaded, 3);
            }
            *image = std::move(*loaded);
            return Status{};
        },
        [image, path, key](Job& job) {
            // A failed cover stays pending, so it is not tried again every frame.
            if (job.State() == JobState::Done && Svc().textures) {
                Svc().textures->Put(key, *image);
                PendingBlurs().erase(path);
            }
        });
}

gfx::Texture BlurTexture(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    const std::string key = BlurKey(path);
    if (Svc().textures->StateOf(key) != gfx::TextureCache::State::Ready) {
        RequestBlur(path);
        return {};
    }
    return Svc().textures->Get(key);
}

void DrawLine(ui::Context& ctx, const gfx::Rect& rect, const std::string& text, float size, int font,
              gfx::Color color, bool centered) {
    const std::string shown = ctx.Text().Elide(text, size, rect.w, font);
    const float width = centered ? ctx.Text().Measure(shown, size, font) : rect.w;
    const float line = ctx.Text().Metrics(size, font).line_height;
    ctx.canvas->DrawText(shown, rect.x + (rect.w - width) * 0.5f, rect.y + (rect.h - line) * 0.5f, size, color,
                         font);
}

void DrawCentered(ui::Context& ctx, const gfx::Rect& rect, const std::string& text, float size, int font,
                  gfx::Color color) {
    DrawLine(ctx, rect, text, size, font, color, true);
}

// A picture this close to the tile's shape fills it: the sliver cut off is not box art.
constexpr float FILL_TOLERANCE = 0.04f;

} // namespace

void Backdrop::Set(const std::string& cover_path) {
    if (cover_path == current_) {
        return;
    }
    previous_ = current_;
    current_ = cover_path;
    fade_ = 0.0f;
}

void Backdrop::Draw(ui::Context& ctx, const gfx::Rect& area) {
    const gfx::Texture now = BlurTexture(current_);
    const gfx::Texture before = BlurTexture(previous_);
    // The fade starts once the new image exists, so a slow decode does not flash the page.
    if (now || current_.empty()) {
        fade_ = ui::Approach(fade_, 1.0f, ctx.dt, 6.0f);
    }
    const auto draw = [&](const gfx::Texture& texture, float alpha) {
        if (texture && alpha > 0.01f) {
            ctx.canvas->DrawImage(area, texture, gfx::Color{255, 255, 255, 255}.WithAlpha(alpha), 0,
                                  gfx::CoverUv(texture.width, texture.height, area));
        }
    };
    if (!now && !before) {
        return;
    }
    draw(before, 1.0f - fade_);
    draw(now, fade_);
    ctx.canvas->FillRect(area, ctx.theme->page.WithAlpha(0.74f));
}

void DrawCover(ui::Context& ctx, const gfx::Rect& rect, const gfx::Texture& texture, bool fill) {
    const float radius = ctx.theme->radius;
    const gfx::Rect fit = gfx::FitRect(texture.width, texture.height, rect);
    if (fill || (fit.w >= rect.w * (1 - FILL_TOLERANCE) && fit.h >= rect.h * (1 - FILL_TOLERANCE))) {
        ctx.canvas->DrawImage(rect, texture, gfx::Color{255, 255, 255, 255}, radius,
                              gfx::CoverUv(texture.width, texture.height, rect));
        return;
    }
    ctx.canvas->FillRect(rect, ctx.theme->panel, radius);
    ctx.canvas->DrawImage(fit, texture, gfx::Color{255, 255, 255, 255}, ctx.theme->radius_small);
}

float DrawChip(ui::Context& ctx, const gfx::Rect& image, const std::string& text, bool right, float room);

CoverView::CoverView(std::string title, std::string image_path, float w, float h)
    : title_(std::move(title)), image_path_(std::move(image_path)) {
    width = w;
    height = h;
}

void CoverView::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    const gfx::Texture texture = image_path_.empty() ? gfx::Texture{} : ctx.textures->Get(image_path_, 1024);
    if (texture) {
        DrawCover(ctx, rect, texture, overlay_title);
        if (overlay_title) {
            DrawCentered(ctx, rect.Inset(16), title_, theme.text_heading, gfx::FONT_BOLD, theme.page);
        }
        if (library::IsHackBox(image_path_)) {
            DrawChip(ctx, rect, "HACK", true, rect.w - 16);
        }
    } else {
        ctx.canvas->FillRect(rect, theme.panel, theme.radius);
        DrawCentered(ctx, rect.Inset(16), title_, theme.text_heading, gfx::FONT_BOLD, theme.fg);
    }
}

ProgressBar::ProgressBar() {
    height = 10;
}

void ProgressBar::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    ctx.canvas->FillRect(rect, theme.panel_strong, rect.h * 0.5f);
    if (fraction >= 0) {
        const float w = std::max(rect.h, rect.w * std::clamp(fraction, 0.0f, 1.0f));
        ctx.canvas->FillRect(gfx::Rect{rect.x, rect.y, w, rect.h}, theme.fg, rect.h * 0.5f);
    } else {
        const float block = rect.w * 0.25f;
        const float t = 0.5f + 0.5f * static_cast<float>(std::sin(ctx.time * 2.5));
        ctx.canvas->FillRect(gfx::Rect{rect.x + (rect.w - block) * t, rect.y, block, rect.h}, theme.fg,
                             rect.h * 0.5f);
    }
}

// A small label in a top corner of a picture, never wider than `room`: a long text is cut.
// Returns the width it took.
float DrawChip(ui::Context& ctx, const gfx::Rect& image, const std::string& text, bool right, float room) {
    const ui::Theme& theme = *ctx.theme;
    constexpr float INSET = 8, PAD = 14;
    if (room < 3 * PAD) {
        return 0;
    }
    const float wanted = std::ceil(ctx.Text().Measure(text, theme.text_small, gfx::FONT_BOLD)) + PAD;
    const float w = std::min(room, wanted);
    const gfx::Rect chip{right ? image.x + image.w - INSET - w : image.x + INSET, image.y + INSET, w, 22};
    ctx.canvas->FillRect(chip, theme.dialog, theme.radius_small);
    // Only a chip that was cut needs its text cut: the whole one gets the full width.
    const gfx::Rect inner = w < wanted ? gfx::Rect{chip.x + PAD / 2, chip.y, chip.w - PAD, chip.h} : chip;
    DrawCentered(ctx, inner, text, theme.text_small, gfx::FONT_BOLD, theme.fg);
    return w;
}

void GameTile::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    const gfx::Rect image = FocusRect();
    constexpr float HACK_CHIP = 58; // kept free for the "HACK" chip beside a long badge
    float hack_room = image.w - 16 - (hack && !badge.empty() ? HACK_CHIP : 0);
    const gfx::Texture texture =
        ctx.textures && !image_path.empty() ? ctx.textures->Get(image_path) : gfx::Texture{};
    if (texture) {
        DrawCover(ctx, image, texture, overlay_title);
        if (overlay_title) {
            DrawCentered(ctx, image.Inset(10), title, theme.text_body, gfx::FONT_BOLD, theme.page);
        }
    } else {
        ctx.canvas->FillRect(image, theme.panel, theme.radius);
        DrawCentered(ctx, image.Inset(12), title, theme.text_body, gfx::FONT_BOLD, theme.fg);
    }
    if (!badge.empty()) {
        DrawChip(ctx, image, badge, false, hack_room);
    }
    if (hack) {
        DrawChip(ctx, image, "HACK", true, image.w - 16);
    }
    const gfx::Rect caption{rect.x, image.Bottom() + 6, image_width, 24};
    DrawLine(ctx, caption, title, theme.text_small + 1, focused ? gfx::FONT_BOLD : gfx::FONT_REGULAR,
             focused ? theme.fg : theme.text_dim, false);
}

ui::Tile* AddGameTile(ui::Node* parent, const library::Entry& game, const GameTileOptions& options) {
    auto* tile = parent->Emplace<GameTile>(game.name, game.cover, options.w, options.h);
    tile->overlay_title = game.placeholder_cover;
    tile->hack = library::IsHackBox(game.cover);
    tile->id = options.id_prefix + "." + game.id;
    if (options.show_badge) {
        tile->badge = options.badge.empty() ? SystemShortName(game.system) : options.badge;
    }
    if (options.on_activate) {
        tile->on_activate = [game, fn = options.on_activate] { fn(game); };
    }
    if (options.on_focus) {
        tile->on_focus = [game, fn = options.on_focus] { fn(game); };
    }
    return tile;
}

int GridColumns(float width, float tile_w, float gap) {
    return std::max(1, static_cast<int>((width + gap) / (tile_w + gap)));
}

ui::Node* AddGameGrid(ui::Node* parent, const std::vector<library::Entry>& games, int columns,
                      GameTileOptions options) {
    options.w = GRID_COVER_W;
    options.h = GRID_COVER_H;
    ui::Node* list = parent->Add(ui::MakeList(14));
    list->pad_top = 10;
    list->pad_left = 10;
    list->pad_bottom = 24;
    list->scroll_margin = 30;
    ui::Node* row = nullptr;
    int in_row = 0;
    for (const library::Entry& game : games) {
        if (!row || in_row == columns) {
            row = list->Add(ui::MakeBox(ui::Axis::Row, 18));
            row->align = ui::Align::Start;
            in_row = 0;
        }
        AddGameTile(row, game, options);
        ++in_row;
    }
    return list;
}

} // namespace den::app
