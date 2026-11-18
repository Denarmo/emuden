// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face_images.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace den::app {

namespace {

constexpr int GROUND_TOLERANCE = 38; // how far from the ground's colour a pixel still is ground
constexpr float MAX_MARGIN = 0.18f;  // a scan margin is never more than this of a side

struct Rgb {
    int r = 0, g = 0, b = 0;
};

std::uint8_t* At(gfx::ImageData& im, int x, int y) {
    return &im.rgba[(static_cast<std::size_t>(y) * im.width + x) * 4];
}
const std::uint8_t* At(const gfx::ImageData& im, int x, int y) {
    return &im.rgba[(static_cast<std::size_t>(y) * im.width + x) * 4];
}

int Distance(const std::uint8_t* p, Rgb c) {
    return std::max({std::abs(p[0] - c.r), std::abs(p[1] - c.g), std::abs(p[2] - c.b)});
}

bool HasClearPixels(const gfx::ImageData& im) {
    for (std::size_t i = 3; i < im.rgba.size(); i += 4) {
        if (im.rgba[i] < 250) {
            return true;
        }
    }
    return false;
}

// The colour of the ground when the four corners agree on one; false when they do not.
bool Ground(const gfx::ImageData& im, Rgb& out) {
    const int xs[] = {0, im.width - 1}, ys[] = {0, im.height - 1};
    Rgb sum;
    std::vector<Rgb> corners;
    for (const int y : ys) {
        for (const int x : xs) {
            const std::uint8_t* p = At(im, x, y);
            corners.push_back({p[0], p[1], p[2]});
            sum.r += p[0];
            sum.g += p[1];
            sum.b += p[2];
        }
    }
    out = {sum.r / 4, sum.g / 4, sum.b / 4};
    for (const Rgb& c : corners) {
        const std::uint8_t p[3] = {static_cast<std::uint8_t>(c.r), static_cast<std::uint8_t>(c.g),
                                   static_cast<std::uint8_t>(c.b)};
        if (Distance(p, out) > GROUND_TOLERANCE / 2) {
            return false;
        }
    }
    return true;
}

// Makes the ground clear: every pixel of its colour reached from the edges (and, for discs,
// from inside the center spindle hole), then feathers adjacent fringe pixels.
void ClearGround(gfx::ImageData& im, Rgb ground, bool disc_center) {
    const int w = im.width, h = im.height;
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(w) * h, 0);
    std::vector<int> stack;
    const auto push = [&](int x, int y) {
        const std::size_t i = static_cast<std::size_t>(y) * w + x;
        if (!seen[i] && Distance(At(im, x, y), ground) <= GROUND_TOLERANCE) {
            seen[i] = 1;
            stack.push_back(static_cast<int>(i));
        }
    };
    for (int x = 0; x < w; ++x) {
        push(x, 0);
        push(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        push(0, y);
        push(w - 1, y);
    }
    if (disc_center) {
        const int cx = w / 2, cy = h / 2;
        const int r = std::max(2, std::min(w, h) / 18);
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int x = cx + dx, y = cy + dy;
                if (dx * dx + dy * dy <= r * r && x >= 0 && x < w && y >= 0 && y < h) {
                    push(x, y);
                }
            }
        }
    }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % w, y = i / w;
        im.rgba[static_cast<std::size_t>(i) * 4 + 3] = 0;
        if (x > 0) {
            push(x - 1, y);
        }
        if (x + 1 < w) {
            push(x + 1, y);
        }
        if (y > 0) {
            push(x, y - 1);
        }
        if (y + 1 < h) {
            push(x, y + 1);
        }
    }
    const int feather_limit = GROUND_TOLERANCE * 3 / 2;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            if (seen[i]) {
                continue;
            }
            const bool touches = (x > 0 && seen[i - 1]) || (x + 1 < w && seen[i + 1]) ||
                                 (y > 0 && seen[i - static_cast<std::size_t>(w)]) ||
                                 (y + 1 < h && seen[i + static_cast<std::size_t>(w)]);
            if (!touches) {
                continue;
            }
            std::uint8_t* p = At(im, x, y);
            const int d = Distance(p, ground);
            if (d < feather_limit) {
                const float t = static_cast<float>(std::max(0, d - GROUND_TOLERANCE)) /
                                static_cast<float>(std::max(1, feather_limit - GROUND_TOLERANCE));
                p[3] = static_cast<std::uint8_t>(std::min<int>(p[3], static_cast<int>(t * 180.0f)));
            }
        }
    }
}

// The smallest rectangle holding every pixel that `keep` keeps.
template <typename Keep>
bool Bounds(const gfx::ImageData& im, Keep keep, int& x0, int& y0, int& x1, int& y1) {
    x0 = im.width;
    y0 = im.height;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < im.height; ++y) {
        for (int x = 0; x < im.width; ++x) {
            if (keep(At(im, x, y))) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    return x1 - x0 >= 8 && y1 - y0 >= 8;
}

void Crop(gfx::ImageData& im, int x0, int y0, int x1, int y1) {
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        std::copy_n(At(im, x0, y0 + y), static_cast<std::size_t>(w) * 4, &out[static_cast<std::size_t>(y) * w * 4]);
    }
    im.width = w;
    im.height = h;
    im.rgba = std::move(out);
}

// Extracts the circular 60 mm disc from a cropped PSP UMD caddy image when lower caddy corners
// are opaque plastic rather than transparent circular corners.
void ExtractUmdDiscIfCaddy(gfx::ImageData& im) {
    const int w = im.width, h = im.height;
    if (w < 16 || h < 16) {
        return;
    }
    const int lx = std::clamp(static_cast<int>(w * 0.14f), 0, w - 1);
    const int rx = std::clamp(static_cast<int>(w * 0.86f), 0, w - 1);
    const int by = std::clamp(static_cast<int>(h * 0.86f), 0, h - 1);
    if (At(im, lx, by)[3] <= 180 || At(im, rx, by)[3] <= 180) {
        return;
    }
    const float cx = w * 0.5f, cy = h * 0.47f;
    const float side = static_cast<float>(std::min(w, h));
    const float r_out = side * 0.435f;
    const float r_in = side * 0.085f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float d = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy);
            std::uint8_t* p = At(im, x, y);
            if (d > r_out + 1.0f || d < r_in - 1.0f) {
                p[3] = 0;
            } else if (d > r_out) {
                const float k = std::clamp(r_out + 1.0f - d, 0.0f, 1.0f);
                p[3] = static_cast<std::uint8_t>(std::min<int>(p[3], static_cast<int>(k * 255.0f)));
            } else if (d < r_in) {
                const float k = std::clamp(d - (r_in - 1.0f), 0.0f, 1.0f);
                p[3] = static_cast<std::uint8_t>(std::min<int>(p[3], static_cast<int>(k * 255.0f)));
            }
        }
    }
    const int x0 = std::clamp(static_cast<int>(std::floor(cx - r_out)), 0, w - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil(cx + r_out)), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(cy - r_out)), 0, h - 1);
    const int y1 = std::clamp(static_cast<int>(std::ceil(cy + r_out)), 0, h - 1);
    if (x1 - x0 >= 8 && y1 - y0 >= 8) {
        Crop(im, x0, y0, x1, y1);
    }
}

// A quarter turn clockwise: the left edge becomes the top.
void TurnClockwise(gfx::ImageData& im) {
    const int w = im.width, h = im.height;
    std::vector<std::uint8_t> out(im.rgba.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int nx = h - 1 - y, ny = x;
            std::copy_n(At(im, x, y), 4, &out[(static_cast<std::size_t>(ny) * h + nx) * 4]);
        }
    }
    im.width = h;
    im.height = w;
    im.rgba = std::move(out);
}

// Pads to a square around the middle with clear pixels.
void Square(gfx::ImageData& im) {
    const int side = std::max(im.width, im.height);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(side) * side * 4, 0);
    const int ox = (side - im.width) / 2, oy = (side - im.height) / 2;
    for (int y = 0; y < im.height; ++y) {
        std::copy_n(At(im, 0, y), static_cast<std::size_t>(im.width) * 4,
                    &out[(static_cast<std::size_t>(oy + y) * side + ox) * 4]);
    }
    im.width = im.height = side;
    im.rgba = std::move(out);
}

// The longest run of true in `v`; false when it is shorter than `least`.
bool LongestRun(const std::vector<bool>& v, int least, int& from, int& to) {
    int best = 0, start = 0;
    for (int i = 0; i <= static_cast<int>(v.size()); ++i) {
        if (i < static_cast<int>(v.size()) && v[static_cast<std::size_t>(i)]) {
            continue;
        }
        if (i - start > best) {
            best = i - start;
            from = start;
            to = i - 1;
        }
        start = i + 1;
    }
    return best >= least;
}

int Median(std::vector<int>& values) {
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
    return values[mid];
}

} // namespace

bool CleanFace(gfx::ImageData& im, FaceKind kind) {
    if (im.width < 8 || im.height < 8) {
        return false;
    }
    const bool disc = kind == FaceKind::Disc || kind == FaceKind::UmdDisc;
    const bool object = kind == FaceKind::Cart || disc;
    Rgb ground;
    const bool clear = HasClearPixels(im);
    const bool plain = !clear && Ground(im, ground);
    if (object && plain) {
        ClearGround(im, ground, disc);
    }
    int x0 = 0, y0 = 0, x1 = im.width - 1, y1 = im.height - 1;
    if (clear || (object && plain)) {
        if (!Bounds(im, [](const std::uint8_t* p) { return p[3] > 128; }, x0, y0, x1, y1)) {
            return false;
        }
    } else if (plain && Bounds(im, [&](const std::uint8_t* p) { return Distance(p, ground) > GROUND_TOLERANCE; },
                               x0, y0, x1, y1)) {
        // A scan's margins, but never so much that the picture's own plain edge goes too.
        const int mx = static_cast<int>(im.width * MAX_MARGIN), my = static_cast<int>(im.height * MAX_MARGIN);
        x0 = std::min(x0, mx);
        y0 = std::min(y0, my);
        x1 = std::max(x1, im.width - 1 - mx);
        y1 = std::max(y1, im.height - 1 - my);
    }
    if (x0 > 0 || y0 > 0 || x1 < im.width - 1 || y1 < im.height - 1) {
        Crop(im, x0, y0, x1, y1);
    }
    if (kind == FaceKind::Spine && im.width > im.height) {
        TurnClockwise(im);
    }
    if (kind == FaceKind::UmdDisc) {
        ExtractUmdDiscIfCaddy(im);
    }
    if (disc) {
        Square(im);
    }
    return true;
}

CartReading ReadCart(const gfx::ImageData& im) {
    CartReading out;
    if (im.width < 8 || im.height < 8) {
        return out;
    }
    out.aspect = static_cast<float>(im.width) / static_cast<float>(im.height);
    // The plastic: opaque pixels in a band along the sides and the bottom, where labels are not.
    const int band = std::max(2, std::min(im.width, im.height) / 14);
    std::vector<int> rs, gs, bs;
    std::vector<const std::uint8_t*> edge_pixels;
    int opaque_total = 0;
    for (int y = 0; y < im.height; ++y) {
        for (int x = 0; x < im.width; ++x) {
            const std::uint8_t* p = At(im, x, y);
            if (p[3] > 200) {
                ++opaque_total;
                const bool edge = x < band || im.width - 1 - x < band || im.height - 1 - y < band;
                if (edge) {
                    rs.push_back(p[0]);
                    gs.push_back(p[1]);
                    bs.push_back(p[2]);
                    edge_pixels.push_back(p);
                }
            }
        }
    }
    if (edge_pixels.empty()) {
        out.aspect = 0;
        return out;
    }
    const Rgb plastic{Median(rs), Median(gs), Median(bs)};
    // The edge is in shadow on most photographs: a little lighter is closer to the plastic.
    const auto lift = [](int c) { return static_cast<std::uint8_t>(std::min(255.0, c * 1.10)); };
    out.plastic = gfx::Color{lift(plastic.r), lift(plastic.g), lift(plastic.b), 255};

    int uniform_edge = 0;
    for (const std::uint8_t* p : edge_pixels) {
        if (Distance(p, plastic) <= 42) {
            ++uniform_edge;
        }
    }
    const float opaque_share =
        static_cast<float>(opaque_total) / static_cast<float>(static_cast<std::size_t>(im.width) * im.height);
    const float edge_uniformity = static_cast<float>(uniform_edge) / static_cast<float>(edge_pixels.size());

    // The label: the rows, then the columns of those rows, that are mostly not plastic.
    const auto label_pixel = [&](int x, int y) {
        const std::uint8_t* p = At(im, x, y);
        return p[3] > 200 && Distance(p, plastic) > 64;
    };
    std::vector<bool> rows(static_cast<std::size_t>(im.height));
    for (int y = 0; y < im.height; ++y) {
        int n = 0;
        for (int x = 0; x < im.width; ++x) {
            n += label_pixel(x, y) ? 1 : 0;
        }
        rows[static_cast<std::size_t>(y)] = n > im.width * 0.5f;
    }
    int top = 0, bottom = 0, left = 0, right = 0;
    if (LongestRun(rows, im.height / 4, top, bottom)) {
        std::vector<bool> columns(static_cast<std::size_t>(im.width));
        for (int x = 0; x < im.width; ++x) {
            int n = 0;
            for (int y = top; y <= bottom; ++y) {
                n += label_pixel(x, y) ? 1 : 0;
            }
            columns[static_cast<std::size_t>(x)] = n > (bottom - top + 1) * 0.6f;
        }
        if (LongestRun(columns, im.width / 4, left, right)) {
            const float w = static_cast<float>(im.width), h = static_cast<float>(im.height);
            const gfx::Rect label{left / w, top / h, (right + 1 - left) / w, (bottom + 1 - top) / h};
            // A label is a good part of the front, never all of it.
            if (label.w * label.h > 0.12f && label.w * label.h < 0.85f) {
                out.label = label;
            }
        }
    }
    if (opaque_share > 0.965f && edge_uniformity < 0.55f) {
        out.label_only = true;
        out.label = {0, 0, 0, 0};
    }
    return out;
}

} // namespace den::app
