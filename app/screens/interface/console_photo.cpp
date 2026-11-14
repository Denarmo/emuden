// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console_photo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <mutex>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/gfx/device.h"
#include "den/gfx/images.h"

namespace den::app {

namespace fs = std::filesystem;

namespace {

constexpr int MAX_SIDE = 1024;
constexpr int VERSION = 3; // bump when the cleaning changes

float Luma(float r, float g, float b) {
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

float Smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// The plastic: the most common colour, neither black nor white, in a band just inside the
// console's left and right edges (a large screen would outnumber it elsewhere).
gfx::Color ShellColor(const gfx::ImageData& im) {
    std::map<int, std::array<double, 4>> buckets; // key -> r, g, b sums and count
    const auto add = [&](const std::uint8_t* p) {
        const float l = Luma(p[0], p[1], p[2]);
        if (p[3] < 250 || l < 40 || l > 245) {
            return;
        }
        auto& b = buckets[(p[0] >> 4) << 8 | (p[1] >> 4) << 4 | (p[2] >> 4)];
        b[0] += p[0];
        b[1] += p[1];
        b[2] += p[2];
        b[3] += 1;
    };
    const int band = std::max(2, im.width / 12), skip = std::max(1, im.width / 50);
    for (int y = 0; y < im.height; ++y) {
        const std::uint8_t* row = &im.rgba[static_cast<std::size_t>(y) * im.width * 4];
        int left = 0, right = im.width - 1;
        while (left < im.width && row[left * 4 + 3] < 250) {
            ++left;
        }
        while (right > left && row[right * 4 + 3] < 250) {
            --right;
        }
        for (int x = left + skip; x < std::min(right, left + skip + band); ++x) {
            add(row + x * 4);
        }
        for (int x = std::max(left, right - skip - band); x < right - skip; ++x) {
            add(row + x * 4);
        }
    }
    const auto best = std::max_element(buckets.begin(), buckets.end(),
                                       [](const auto& a, const auto& b) { return a.second[3] < b.second[3]; });
    if (best == buckets.end()) {
        return gfx::Color{200, 200, 205, 255};
    }
    const auto& s = best->second;
    return gfx::Color{static_cast<std::uint8_t>(s[0] / s[3]), static_cast<std::uint8_t>(s[1] / s[3]),
                      static_cast<std::uint8_t>(s[2] / s[3]), 255};
}

double Cross(const gfx::Vec2& o, const gfx::Vec2& a, const gfx::Vec2& b) {
    return static_cast<double>(a.x - o.x) * (b.y - o.y) - static_cast<double>(a.y - o.y) * (b.x - o.x);
}

// The convex hull of the opaque pixels, counter-clockwise, in pixels.
std::vector<gfx::Vec2> Hull(const gfx::ImageData& im) {
    std::vector<gfx::Vec2> points;
    for (int y = 0; y < im.height; ++y) {
        int left = -1, right = -1;
        for (int x = 0; x < im.width; ++x) {
            if (im.rgba[(static_cast<std::size_t>(y) * im.width + x) * 4 + 3] >= 128) {
                left = left < 0 ? x : left;
                right = x;
            }
        }
        if (left >= 0) {
            points.push_back({static_cast<float>(left), static_cast<float>(y)});
            points.push_back({static_cast<float>(right + 1), static_cast<float>(y)});
            points.push_back({static_cast<float>(left), static_cast<float>(y + 1)});
            points.push_back({static_cast<float>(right + 1), static_cast<float>(y + 1)});
        }
    }
    std::sort(points.begin(), points.end(), [](const gfx::Vec2& a, const gfx::Vec2& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    if (points.size() < 3) {
        return {};
    }
    std::vector<gfx::Vec2> hull(points.size() * 2);
    std::size_t k = 0;
    for (const gfx::Vec2& p : points) {
        while (k >= 2 && Cross(hull[k - 2], hull[k - 1], p) <= 0) {
            --k;
        }
        hull[k++] = p;
    }
    for (std::size_t i = points.size() - 1, lower = k + 1; i-- > 0;) {
        while (k >= lower && Cross(hull[k - 2], hull[k - 1], points[i]) <= 0) {
            --k;
        }
        hull[k++] = points[i];
    }
    hull.resize(k - 1);
    return hull;
}

// The plastic in another colour: the pixels near the plastic's hue and brightness take the new
// colour, keeping their shading.
void Recolor(gfx::ImageData& im, gfx::Color from, gfx::Color to) {
    const float fs_ = std::max(1.0f, static_cast<float>(from.r + from.g + from.b));
    const float fc[3] = {from.r / fs_, from.g / fs_, from.b / fs_};
    const float fl = std::max(1.0f, Luma(from.r, from.g, from.b));
    const float tl = std::max(1.0f, Luma(to.r, to.g, to.b));
    // Grey plastic is told from the grey around the screen by brightness alone.
    const float chroma = std::abs(fc[0] - 1 / 3.0f) + std::abs(fc[1] - 1 / 3.0f) + std::abs(fc[2] - 1 / 3.0f);
    const float dark = chroma < 0.08f ? 0.72f : 0.22f;
    for (std::size_t i = 0; i + 3 < im.rgba.size(); i += 4) {
        std::uint8_t* p = &im.rgba[i];
        const float sum = std::max(1.0f, static_cast<float>(p[0] + p[1] + p[2]));
        const float d = std::abs(p[0] / sum - fc[0]) + std::abs(p[1] / sum - fc[1]) + std::abs(p[2] / sum - fc[2]);
        const float r = Luma(p[0], p[1], p[2]) / fl;
        const float m = Smooth(0.13f, 0.06f, d) * Smooth(dark, dark + 0.12f, r) * Smooth(1.8f, 1.45f, r);
        if (m <= 0) {
            continue;
        }
        // Lighter colours keep less of the photograph's shading so white stays white.
        const float shade = 1 + (r - 1) * std::min(1.0f, 120.0f / tl);
        const std::uint8_t target[3] = {to.r, to.g, to.b};
        for (int c = 0; c < 3; ++c) {
            const float v = std::clamp(target[c] * shade, 0.0f, 255.0f);
            p[c] = static_cast<std::uint8_t>(p[c] + (v - p[c]) * m);
        }
    }
}

// A photograph on a plain ground (no clear pixels in its corners): the ground joined to the
// edges, within a little of the corners' colour, made clear.
void ClearGround(gfx::ImageData& im) {
    const int w = im.width, h = im.height;
    const auto at = [&](int x, int y) { return &im.rgba[(static_cast<std::size_t>(y) * w + x) * 4]; };
    if (w < 2 || h < 2 || at(0, 0)[3] < 250 || at(w - 1, 0)[3] < 250 || at(0, h - 1)[3] < 250 ||
        at(w - 1, h - 1)[3] < 250) {
        return;
    }
    const std::uint8_t* c = at(0, 0);
    const int ground[3] = {c[0], c[1], c[2]};
    const auto plain = [&](const std::uint8_t* p) {
        return p[3] > 0 && std::abs(p[0] - ground[0]) + std::abs(p[1] - ground[1]) + std::abs(p[2] - ground[2]) < 36;
    };
    std::vector<std::pair<int, int>> stack;
    for (int x = 0; x < w; ++x) {
        stack.push_back({x, 0});
        stack.push_back({x, h - 1});
    }
    for (int y = 0; y < h; ++y) {
        stack.push_back({0, y});
        stack.push_back({w - 1, y});
    }
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        std::uint8_t* p = at(x, y);
        if (!plain(p)) {
            continue;
        }
        p[3] = 0;
        if (x > 0) stack.push_back({x - 1, y});
        if (x + 1 < w) stack.push_back({x + 1, y});
        if (y > 0) stack.push_back({x, y - 1});
        if (y + 1 < h) stack.push_back({x, y + 1});
    }
}

std::string Save(const fs::path& out, const gfx::ImageData& im) {
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    return gfx::SavePng(out, im.width, im.height, im.rgba.data()).ok ? PathToUtf8(out) : std::string();
}

} // namespace

ConsolePhoto PrepareConsolePhoto(const std::string& path, const std::vector<gfx::Color>& colors) {
    static std::mutex mutex;
    static std::map<std::string, ConsolePhoto> done;
    std::string key = path;
    for (const gfx::Color& c : colors) {
        key += std::format("|{:02x}{:02x}{:02x}", c.r, c.g, c.b);
    }
    const std::lock_guard lock(mutex);
    if (const auto found = done.find(key); found != done.end()) {
        return found->second;
    }
    ConsolePhoto& out = done[key];
    if (path.empty()) {
        return out;
    }
    auto loaded = gfx::LoadImageFile(PathFromUtf8(path));
    if (!loaded) {
        return out;
    }
    gfx::ImageData im = std::move(*loaded);
    gfx::ShrinkImage(im, MAX_SIDE);
    ClearGround(im);
    // Crop to the console.
    int x0 = im.width, y0 = im.height, x1 = -1, y1 = -1;
    for (int y = 0; y < im.height; ++y) {
        for (int x = 0; x < im.width; ++x) {
            if (im.rgba[(static_cast<std::size_t>(y) * im.width + x) * 4 + 3] >= 128) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            }
        }
    }
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    // A picture with no clear ground, or with little console on it, is not a console's outline.
    if (x1 < 0 || w < 64 || h < 64 || (w == im.width && h == im.height && im.rgba[3] >= 128)) {
        return out;
    }
    gfx::ImageData crop{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4)};
    for (int y = 0; y < h; ++y) {
        std::copy_n(&im.rgba[(static_cast<std::size_t>(y + y0) * im.width + x0) * 4], static_cast<std::size_t>(w) * 4,
                    &crop.rgba[static_cast<std::size_t>(y) * w * 4]);
    }
    std::vector<gfx::Vec2> hull = Hull(crop);
    if (hull.size() < 3) {
        return out;
    }
    const gfx::Color shell = ShellColor(crop);
    for (std::size_t i = 0; i + 3 < crop.rgba.size(); i += 4) {
        std::uint8_t* p = &crop.rgba[i];
        const float a = p[3] / 255.0f;
        p[0] = static_cast<std::uint8_t>(shell.r + (p[0] - shell.r) * a);
        p[1] = static_cast<std::uint8_t>(shell.g + (p[1] - shell.g) * a);
        p[2] = static_cast<std::uint8_t>(shell.b + (p[2] - shell.b) * a);
        p[3] = 255;
    }
    // Down to the points at least 1/200 of the height apart, in heights, y up.
    std::vector<gfx::Vec2> outline;
    const float scale = 1.0f / static_cast<float>(h);
    for (const gfx::Vec2& p : hull) {
        const gfx::Vec2 q{(p.x - w * 0.5f) * scale, (h * 0.5f - p.y) * scale};
        if (outline.empty() || std::hypot(q.x - outline.back().x, q.y - outline.back().y) >= 0.005f) {
            outline.push_back(q);
        }
    }
    while (outline.size() > 3 &&
           std::hypot(outline.back().x - outline.front().x, outline.back().y - outline.front().y) < 0.005f) {
        outline.pop_back();
    }
    // The hull ran counter-clockwise with y down: clockwise with y up. AddSlab takes them the
    // other way round.
    std::reverse(outline.begin(), outline.end());

    const std::size_t hash = std::hash<std::string>{}(std::format("{}|{}", path, VERSION));
    const fs::path dir = Svc().data_dir / "cache" / "consoles";
    const auto file = [&](const gfx::Color& c) {
        return dir / std::format("{:016x}-{:02x}{:02x}{:02x}.png", hash, c.r, c.g, c.b);
    };
    ConsolePhoto photo;
    photo.aspect = static_cast<float>(w) / static_cast<float>(h);
    photo.outline = std::move(outline);
    photo.images.push_back(IsRegularFile(file(shell)) ? PathToUtf8(file(shell)) : Save(file(shell), crop));
    photo.colors.push_back(shell);
    for (const gfx::Color& c : colors) {
        const fs::path target = file(c);
        std::string saved;
        if (IsRegularFile(target)) {
            saved = PathToUtf8(target);
        } else {
            gfx::ImageData copy = crop;
            Recolor(copy, shell, c);
            saved = Save(target, copy);
        }
        if (!saved.empty()) {
            photo.images.push_back(std::move(saved));
            photo.colors.push_back(c);
        }
    }
    if (photo.images.front().empty()) {
        return out;
    }
    out = std::move(photo);
    return out;
}

} // namespace den::app
