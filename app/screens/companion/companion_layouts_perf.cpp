// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "performance" layout of the quick settings: how the game runs on top (the frame rate
// now and over the last minute, the time of each of the last frames, the sound queue) and
// the actions as small tiles below it. The history is kept across rebuilds of the page and
// starts again with another game.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "companion_quick.h"
#include "den/app/app.h"
#include "den/app/strings.h"
#include "den/gfx/canvas.h"
#include "den/gfx/text.h"
#include "den/ui/glyphs.h"

namespace den::app {

namespace {

constexpr float GAP = 12;
constexpr float PADDING = 32;           // the page's, top and bottom
constexpr std::size_t SAMPLES = 120;    // points of each graph
constexpr double FPS_EVERY = 0.5;       // seconds between frame rate points: a minute in all
constexpr float FRAME_TOP_MS = 50;      // the frame time graph's top, as the main screen's overlay

// What the graphs show, sampled while the panel is drawn.
struct History {
    std::string game_id;
    std::uint64_t frames = 0; // the game's frame count when the last one was timed; 0: none yet
    double time = 0;
    std::array<float, SAMPLES> frame_ms{}; // oldest at frame_at
    std::size_t frame_at = 0, frame_count = 0;
    std::array<float, SAMPLES> fps{};
    std::size_t fps_at = 0, fps_count = 0;
    double fps_next = 0;

    void Sample(const player::Snapshot& snapshot, double now) {
        if (snapshot.game_id != game_id) {
            *this = History{};
            game_id = snapshot.game_id;
        }
        if (snapshot.state != player::State::Running) {
            frames = 0; // a pause is not a slow frame
            return;
        }
        if (frames == 0 || snapshot.frames < frames) {
            frames = snapshot.frames;
            time = now;
            fps_next = now + FPS_EVERY;
            return;
        }
        if (snapshot.frames > frames) {
            // The frames made since the last draw share its time; a long gap the panel was
            // not drawn in (a section open) averages out, a single slow frame shows as one.
            const std::uint64_t made = snapshot.frames - frames;
            const float each = static_cast<float>((now - time) * 1000.0 / static_cast<double>(made));
            for (std::uint64_t i = 0; i < std::min<std::uint64_t>(made, SAMPLES); ++i) {
                frame_ms[frame_at] = each;
                frame_at = (frame_at + 1) % SAMPLES;
            }
            frame_count = std::min(SAMPLES, frame_count + static_cast<std::size_t>(std::min<std::uint64_t>(made, SAMPLES)));
            frames = snapshot.frames;
            time = now;
        }
        if (now >= fps_next) {
            fps[fps_at] = static_cast<float>(snapshot.measured_fps);
            fps_at = (fps_at + 1) % SAMPLES;
            fps_count = std::min(SAMPLES, fps_count + 1);
            fps_next = std::max(fps_next + FPS_EVERY, now);
        }
    }
};

History& Kept() {
    static History history;
    return history;
}

// The last `count` of a ring, oldest first.
template <typename F>
void EachOf(const std::array<float, SAMPLES>& ring, std::size_t at, std::size_t count, F&& f) {
    for (std::size_t i = 0; i < count; ++i) {
        f(i, ring[(at + SAMPLES - count + i) % SAMPLES]);
    }
}

// The panel at the top: the numbers, and the two graphs beside them (below them on an
// upright screen).
class PerfPanel final : public ui::Node {
public:
    explicit PerfPanel(std::string title) : title_(std::move(title)) {}

protected:
    ui::Size MeasureContent(ui::Context&) override {
        return {200, 180};
    }

    void DrawContent(ui::Context& ctx) override {
        const player::Snapshot snapshot = Svc().player->GetSnapshot();
        History& h = Kept();
        h.Sample(snapshot, ctx.time);

        const ui::Theme& theme = *ctx.theme;
        gfx::Canvas& canvas = *ctx.canvas;
        gfx::Text& t = ctx.Text();
        canvas.FillRect(rect, theme.panel, theme.radius);
        const gfx::Rect inner = rect.Inset(16);
        const bool upright = inner.w < inner.h * 1.3f;

        // The numbers: a column at the left, or a band across the top.
        const float numbers_w = upright ? inner.w : std::clamp(inner.w * 0.3f, 170.0f, 300.0f);
        const float big = std::clamp(std::min(inner.h * 0.34f, numbers_w * 0.32f), 28.0f, 72.0f);
        const float body = theme.text_body, small = theme.text_small;
        const float small_line = t.Metrics(small).line_height;
        float y = inner.y;
        canvas.DrawText(t.Elide(title_, body, numbers_w, gfx::FONT_BOLD), inner.x, y, body, theme.fg, gfx::FONT_BOLD);
        y += t.Metrics(body, gfx::FONT_BOLD).line_height + 2;
        canvas.DrawText(t.Elide(Tr(Status(snapshot)), small, numbers_w), inner.x, y, small, theme.text_dim);
        y += small_line + 4;

        const bool running = snapshot.state == player::State::Running;
        const std::string rate = running ? std::format("{:.0f}", snapshot.measured_fps) : std::string("–");
        canvas.DrawText(rate, inner.x, y, big, theme.fg, gfx::FONT_BOLD);
        const float rate_w = t.Measure(rate, big, gfx::FONT_BOLD);
        const std::string target = TrF("of {:.0f} fps", snapshot.core_fps);
        canvas.DrawText(target, inner.x + rate_w + 10, y + big - small_line - big * 0.12f, small, theme.text_dim);
        y += t.Metrics(big, gfx::FONT_BOLD).line_height;

        float average = 0, slowest = 0;
        EachOf(h.frame_ms, h.frame_at, h.frame_count, [&](std::size_t, float ms) {
            average += ms;
            slowest = std::max(slowest, ms);
        });
        if (h.frame_count > 0) {
            average /= static_cast<float>(h.frame_count);
        }
        std::vector<std::string> lines;
        if (h.frame_count > 0) {
            lines.push_back(TrF("{:.1f} ms a frame · slowest {:.1f}", average, slowest));
        }
        lines.push_back(TrF("Sound {:.0f} ms queued · {} gaps", snapshot.audio_queued_ms, snapshot.audio_underruns));
        if (snapshot.frame_width > 0 && !snapshot.core_name.empty()) {
            lines.push_back(std::format("{} · {}×{}", snapshot.core_name, snapshot.frame_width, snapshot.frame_height));
        }
        const float numbers_bottom = upright ? inner.y + inner.h * 0.45f : inner.Bottom();
        for (const std::string& line : lines) {
            if (y + small_line > numbers_bottom) {
                break;
            }
            canvas.DrawText(t.Elide(line, small, numbers_w), inner.x, y, small, theme.text_dim);
            y += small_line + 2;
        }

        // The graphs, side by side in what is left.
        gfx::Rect area = upright ? gfx::Rect{inner.x, numbers_bottom + GAP, inner.w, inner.Bottom() - numbers_bottom - GAP}
                                 : gfx::Rect{inner.x + numbers_w + GAP * 1.5f, inner.y, inner.w - numbers_w - GAP * 1.5f,
                                             inner.h};
        if (area.w < 120 || area.h < 60) {
            return;
        }
        const float half = (area.w - GAP) * 0.5f;
        // The frame rate over the last minute, under a line at the game's own rate.
        float fps_top = static_cast<float>(snapshot.core_fps) * 1.25f;
        EachOf(h.fps, h.fps_at, h.fps_count, [&](std::size_t, float fps) { fps_top = std::max(fps_top, fps * 1.05f); });
        Graph(ctx, {area.x, area.y, half, area.h}, Tr("Frame rate · last minute"), h.fps, h.fps_at, h.fps_count,
              fps_top, static_cast<float>(snapshot.core_fps), false);
        // The time of each of the last frames, under a line at the time one frame may take.
        const float budget = snapshot.core_fps > 0 ? static_cast<float>(1000.0 / snapshot.core_fps) : 1000.0f / 60.0f;
        Graph(ctx, {area.x + half + GAP, area.y, half, area.h}, Tr("Frame times"), h.frame_ms, h.frame_at,
              h.frame_count, std::max(FRAME_TOP_MS, budget * 2.5f), budget, true);
    }

private:
    static std::string Status(const player::Snapshot& snapshot) {
        return snapshot.state == player::State::Paused ? "Paused"
               : snapshot.rewinding                    ? "Rewinding"
               : snapshot.fast_forward                 ? "Fast-forward"
                                                       : "Playing";
    }

    // A line graph of `count` points of a ring, 0 to `top`, with a guide at `guide`. With
    // `over`, points above the guide are drawn in the accent: frames that took too long.
    static void Graph(ui::Context& ctx, const gfx::Rect& box, const std::string& title,
                      const std::array<float, SAMPLES>& ring, std::size_t at, std::size_t count, float top,
                      float guide, bool over) {
        const ui::Theme& theme = *ctx.theme;
        gfx::Canvas& canvas = *ctx.canvas;
        gfx::Text& t = ctx.Text();
        const float small = theme.text_small;
        canvas.FillRect(box, theme.panel, theme.radius_small);
        canvas.DrawText(t.Elide(title, small, box.w - 20), box.x + 10, box.y + 8, small, theme.text_dim);
        const gfx::Rect plot{box.x + 10, box.y + t.Metrics(small).line_height + 16, box.w - 20,
                             box.h - t.Metrics(small).line_height - 26};
        if (plot.h < 16 || top <= 0) {
            return;
        }
        const auto y_of = [&](float value) { return plot.Bottom() - std::clamp(value / top, 0.0f, 1.0f) * plot.h; };
        canvas.FillRect({plot.x, y_of(guide), plot.w, 1}, theme.text_dim.WithAlpha(0.5f));
        if (count < 2) {
            return;
        }
        // The newest point at the right edge; a history that is not full yet starts part way.
        const float step = plot.w / static_cast<float>(SAMPLES - 1);
        const float first_x = plot.Right() - step * static_cast<float>(count - 1);
        float last_x = 0, last_y = 0;
        EachOf(ring, at, count, [&](std::size_t i, float value) {
            const float x = first_x + step * static_cast<float>(i);
            const float y = y_of(value);
            if (i > 0) {
                const bool slow = over && value > guide * 1.5f;
                ui::DrawLine(canvas, last_x, last_y, x, y, slow ? 3.0f : 2.0f, slow ? theme.accent : theme.fg);
            }
            last_x = x;
            last_y = y;
        });
    }

    std::string title_;
};

} // namespace

void BuildPerformanceLayout(QuickContext& ctx) {
    const float scale = ctx.spec.tile_size;
    const float aspect = ctx.width > 0 && ctx.height > 0 ? ctx.width / ctx.height : 1.6f;
    const float height = ctx.height > 0 ? ctx.height : 600.0f;
    ui::Node& root = *ctx.root.Add(ui::MakeBox(ui::Axis::Column, GAP));
    root.id = "companion.performance";
    root.grow = 1;
    ctx.beside(root);

    auto* panel = root.Emplace<PerfPanel>(ctx.game ? ctx.game->name : std::string("Game"));
    panel->id = "companion.performance_panel";
    panel->grow = 1;

    // The actions as small tiles, more to a row than the grid; they scroll when they do not
    // fit in the lower part.
    const int columns = ctx.spec.columns > 0 ? ctx.spec.columns : aspect < 0.95f ? 4 : aspect < 1.45f ? 5 : 7;
    const int count = static_cast<int>(ctx.actions.size());
    const int rows = (count + columns - 1) / columns;
    const float tile_h = 78 * scale;
    ui::Node& list = *root.Add(ui::MakeList(GAP * 0.75f));
    list.id = "companion.page";
    const float wanted = static_cast<float>(rows) * (tile_h + GAP * 0.75f);
    list.height = std::min(wanted, (height - PADDING) * (aspect < 0.95f ? 0.5f : 0.45f));
    for (int first = 0; first < count; first += columns) {
        ui::Node* row = list.Add(ui::MakeBox(ui::Axis::Row, GAP * 0.75f));
        row->height = tile_h;
        for (int i = 0; i < columns; ++i) {
            ui::Node* cell = first + i < count
                                 ? AddQuickTile(*row, ctx.actions[static_cast<std::size_t>(first + i)], false, scale * 0.7f)
                                 : row->Add(ui::MakeBox(ui::Axis::Row));
            cell->grow = 1;
            cell->width = 0;
        }
    }
}

} // namespace den::app
