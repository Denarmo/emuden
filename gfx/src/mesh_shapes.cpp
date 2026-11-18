// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/gfx/mesh_shapes.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace den::gfx {

namespace {

constexpr float PI = 3.14159265f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

Vec2 Normalized(Vec2 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y);
    return l > 1e-6f ? Vec2{v.x / l, v.y / l} : Vec2{0, 0};
}

// The outward normal of the edge from a to b of a counter-clockwise outline.
Vec2 EdgeNormal(Vec2 a, Vec2 b) {
    return Normalized(Vec2{b.y - a.y, a.x - b.x});
}

MeshVertex V(Vec3 p, Vec3 n, float u, float v, Part part) {
    return MeshVertex{p.x, p.y, p.z, n.x, n.y, n.z, u, v, static_cast<float>(part)};
}

void Corner(std::vector<Vec2>& out, float cx, float cy, float r, float from, float to, int segments) {
    if (r <= 0) {
        out.push_back({cx, cy});
        return;
    }
    for (int i = 0; i <= segments; ++i) {
        const float a = from + (to - from) * static_cast<float>(i) / segments;
        out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
}

} // namespace

std::vector<Vec2> RoundedOutline(float w, float h, float r_top, float r_bottom, float notch, int segments) {
    const float x = w * 0.5f, y = h * 0.5f;
    const float limit = std::min(x, y) * 0.95f;
    r_top = std::clamp(r_top, 0.0f, limit);
    r_bottom = std::clamp(r_bottom, 0.0f, limit);
    notch = std::clamp(notch, 0.0f, limit);
    std::vector<Vec2> points;
    Corner(points, x - r_bottom, -y + r_bottom, r_bottom, -PI * 0.5f, 0, segments);
    if (notch > 0) {
        points.push_back({x, y - notch});
        points.push_back({x - notch, y});
    } else {
        Corner(points, x - r_top, y - r_top, r_top, 0, PI * 0.5f, segments);
    }
    Corner(points, -x + r_top, y - r_top, r_top, PI * 0.5f, PI, segments);
    Corner(points, -x + r_bottom, -y + r_bottom, r_bottom, PI, PI * 1.5f, segments);
    return points;
}

std::vector<Vec2> ShoulderedOutline(float w, float h, float shoulder, float drop, float r_top, float r_bottom,
                                    float chamfer, int segments) {
    const float x = w * 0.5f, y = h * 0.5f;
    shoulder = std::clamp(shoulder, 0.0f, x * 0.45f);
    drop = std::clamp(drop, 0.0f, h * 0.45f);
    r_top = std::clamp(r_top, 0.0f, std::min(shoulder, x - shoulder) * 0.9f);
    r_bottom = std::clamp(r_bottom, 0.0f, std::min(x, y - drop) * 0.9f);
    chamfer = std::clamp(chamfer, 0.0f, std::min(shoulder, drop) * 0.9f);
    const float xs = x - shoulder, ys = y - drop;
    std::vector<Vec2> points;
    Corner(points, x - r_bottom, -y + r_bottom, r_bottom, -PI * 0.5f, 0, segments);
    Corner(points, x - r_top, ys - r_top, r_top, 0, PI * 0.5f, segments);
    // The step up to the narrower top, its inner corner cut at 45 degrees.
    points.push_back({xs + chamfer, ys});
    points.push_back({xs, ys + chamfer});
    Corner(points, xs - r_top, y - r_top, r_top, 0, PI * 0.5f, segments);
    Corner(points, -xs + r_top, y - r_top, r_top, PI * 0.5f, PI, segments);
    points.push_back({-xs, ys + chamfer});
    points.push_back({-xs - chamfer, ys});
    Corner(points, -x + r_top, ys - r_top, r_top, PI * 0.5f, PI, segments);
    Corner(points, -x + r_bottom, -y + r_bottom, r_bottom, PI, PI * 1.5f, segments);
    return points;
}

void AddSlab(std::vector<MeshVertex>& out, const std::vector<Vec2>& outline, float depth, float bevel,
             const SlabParts& parts, float cz, float cx, float cy, int steps) {
    const std::size_t n = outline.size();
    if (n < 3) {
        return;
    }
    bevel = std::clamp(bevel, 0.0f, depth * 0.49f);
    steps = bevel > 0 ? std::max(1, steps) : 0;
    float min_x = outline[0].x, max_x = min_x, min_y = outline[0].y, max_y = min_y;
    for (const Vec2& p : outline) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    const float bw = std::max(1e-6f, max_x - min_x), bh = std::max(1e-6f, max_y - min_y);
    // Each point's outward normal (the mean of its edges') and its place inside the bevel.
    std::vector<Vec2> normal(n), inner(n);
    std::vector<Vec2> edge(n);
    for (std::size_t i = 0; i < n; ++i) {
        edge[i] = EdgeNormal(outline[i], outline[(i + 1) % n]);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 e0 = edge[(i + n - 1) % n], e1 = edge[i];
        normal[i] = Normalized(Vec2{e0.x + e1.x, e0.y + e1.y});
        // Mitred, so square corners keep an even bevel.
        const float miter = bevel / std::max(0.5f, normal[i].x * e1.x + normal[i].y * e1.y);
        inner[i] = Vec2{outline[i].x - normal[i].x * miter, outline[i].y - normal[i].y * miter};
    }
    float in_min_x = inner[0].x, in_max_x = in_min_x, in_min_y = inner[0].y, in_max_y = in_min_y;
    for (const Vec2& p : inner) {
        in_min_x = std::min(in_min_x, p.x);
        in_max_x = std::max(in_max_x, p.x);
        in_min_y = std::min(in_min_y, p.y);
        in_max_y = std::max(in_max_y, p.y);
    }
    const float in_bw = std::max(1e-6f, in_max_x - in_min_x), in_bh = std::max(1e-6f, in_max_y - in_min_y);
    // Rings from the back face to the front face: back bevel, the wall, front bevel.
    struct Ring {
        float z, cos_t, sin_t; // position = inner + normal * bevel * cos_t; normal leans by sin_t
    };
    std::vector<Ring> rings;
    const float half = depth * 0.5f - bevel;
    for (int k = steps; k >= 0; --k) {
        const float t = PI * 0.5f * static_cast<float>(k) / std::max(1, steps);
        rings.push_back({-half - bevel * std::sin(t), std::cos(t), -std::sin(t)});
    }
    for (int k = 0; k <= steps; ++k) {
        const float t = PI * 0.5f * static_cast<float>(k) / std::max(1, steps);
        rings.push_back({half + bevel * std::sin(t), std::cos(t), std::sin(t)});
    }
    std::vector<float> ring_s(rings.size(), 0.0f);
    for (std::size_t j = 1; j < rings.size(); ++j) {
        const float dr = bevel * (rings[j].cos_t - rings[j - 1].cos_t);
        const float dz = rings[j].z - rings[j - 1].z;
        ring_s[j] = ring_s[j - 1] + std::hypot(dr, dz);
    }
    const float total_s = ring_s.back() > 1e-6f ? ring_s.back() : 1.0f;
    for (float& s : ring_s) {
        s /= total_s;
    }
    const auto at = [&](const Ring& r, std::size_t i, Vec2 nrm) {
        const Vec2 p{inner[i].x + nrm.x * bevel * r.cos_t, inner[i].y + nrm.y * bevel * r.cos_t};
        const float lean = std::sqrt(std::max(0.0f, 1 - r.sin_t * r.sin_t));
        return std::pair{Vec3{cx + p.x, cy + p.y, cz + r.z}, Vec3{nrm.x * lean, nrm.y * lean, r.sin_t}};
    };
    const std::size_t wall = static_cast<std::size_t>(steps); // rings[wall], rings[wall + 1]
    for (std::size_t j = 0; j + 1 < rings.size(); ++j) {
        const bool is_wall = j == wall;
        const bool front_bevel = j > wall;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t i1 = (i + 1) % n;
            // Walls are flat along an edge (crisp corners at a notch); bevels shade smoothly.
            const Vec2 na = is_wall ? edge[i] : normal[i], nb = is_wall ? edge[i] : normal[i1];
            const auto [a0, an0] = at(rings[j + 1], i, normal[i]);
            const auto [b0, bn0] = at(rings[j], i, normal[i]);
            const auto [b1, bn1] = at(rings[j], i1, normal[i1]);
            const auto [a1, an1] = at(rings[j + 1], i1, normal[i1]);
            const bool is_spine = edge[i].x < -0.7f && parts.spine != Part::Shell;
            Part part = parts.bevel;
            if (!front_bevel && !is_wall && part == Part::Photo) {
                part = Part::Shell;
            }
            float ua0 = 0, va0 = 0, ub0 = 0, vb0_v = 0, ub1 = 0, vb1_v = 0, ua1 = 0, va1 = 0;
            if (is_spine) {
                part = parts.spine;
                ua0 = ua1 = ring_s[j + 1];
                ub0 = ub1 = ring_s[j];
                const auto spine_v = [&](const Vec3& p) {
                    return std::clamp((in_max_y - (p.y - cy)) / in_bh, 0.0f, 1.0f);
                };
                va0 = spine_v(a0);
                vb0_v = spine_v(b0);
                vb1_v = spine_v(b1);
                va1 = spine_v(a1);
            } else if (front_bevel && parts.front == Part::Photo && parts.bevel == Part::Photo) {
                part = Part::Photo;
                const auto photo_uv = [&](const Vec3& p) {
                    return std::pair{(p.x - cx - min_x) / bw, (max_y - (p.y - cy)) / bh};
                };
                const auto [pu0, pv0] = photo_uv(a0);
                const auto [pu1, pv1] = photo_uv(b0);
                const auto [pu2, pv2] = photo_uv(b1);
                const auto [pu3, pv3] = photo_uv(a1);
                ua0 = pu0; va0 = pv0;
                ub0 = pu1; vb0_v = pv1;
                ub1 = pu2; vb1_v = pv2;
                ua1 = pu3; va1 = pv3;
            } else if (is_wall) {
                part = edge[i].x < -0.7f ? parts.spine : parts.side;
                const float span = std::max(1e-6f, 2.0f * half);
                ua0 = ua1 = half > 0 ? (a0.z - cz + half) / span : 1.0f;
                ub0 = ub1 = half > 0 ? (b0.z - cz + half) / span : 0.0f;
                va0 = vb0_v = (max_y - (a0.y - cy)) / bh;
                va1 = vb1_v = (max_y - (a1.y - cy)) / bh;
            }
            const Vec3 wn0 = is_wall ? Vec3{na.x, na.y, 0} : an0, wn1 = is_wall ? Vec3{nb.x, nb.y, 0} : an1;
            const Vec3 wb0 = is_wall ? Vec3{na.x, na.y, 0} : bn0, wb1 = is_wall ? Vec3{nb.x, nb.y, 0} : bn1;
            const MeshVertex va0_v = V(a0, wn0, ua0, va0, part), vb0_mv = V(b0, wb0, ub0, vb0_v, part);
            const MeshVertex vb1_mv = V(b1, wb1, ub1, vb1_v, part), va1_mv = V(a1, wn1, ua1, va1, part);
            out.insert(out.end(), {va0_v, vb0_mv, vb1_mv, va0_v, vb1_mv, va1_mv});
        }
    }
    // The faces: fans from the middle of the inner outline.
    Vec2 mid;
    for (const Vec2& p : inner) {
        mid.x += p.x / static_cast<float>(n);
        mid.y += p.y / static_cast<float>(n);
    }
    const bool photo_front = parts.front == Part::Photo;
    const auto front_uv = [&](Vec2 p) {
        return photo_front ? std::pair{(p.x - min_x) / bw, (max_y - p.y) / bh}
                           : std::pair{(p.x - in_min_x) / in_bw, (in_max_y - p.y) / in_bh};
    };
    const auto back_uv = [&](Vec2 p) { return std::pair{(p.x - in_min_x) / in_bw, (in_max_y - p.y) / in_bh}; };
    const float zf = cz + depth * 0.5f, zb = cz - depth * 0.5f;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 p0 = inner[i], p1 = inner[(i + 1) % n];
        const auto [fmu, fmv] = front_uv(mid);
        const auto [fu0, fv0] = front_uv(p0);
        const auto [fu1, fv1] = front_uv(p1);
        out.insert(out.end(), {V({cx + mid.x, cy + mid.y, zf}, {0, 0, 1}, fmu, fmv, parts.front),
                               V({cx + p0.x, cy + p0.y, zf}, {0, 0, 1}, fu0, fv0, parts.front),
                               V({cx + p1.x, cy + p1.y, zf}, {0, 0, 1}, fu1, fv1, parts.front)});
        // Seen from the back the image runs the other way: u mirrored.
        const auto [bmu, bmv] = back_uv(mid);
        const auto [bu0, bv0] = back_uv(p0);
        const auto [bu1, bv1] = back_uv(p1);
        out.insert(out.end(), {V({cx + mid.x, cy + mid.y, zb}, {0, 0, -1}, 1 - bmu, bmv, parts.back),
                               V({cx + p1.x, cy + p1.y, zb}, {0, 0, -1}, 1 - bu1, bv1, parts.back),
                               V({cx + p0.x, cy + p0.y, zb}, {0, 0, -1}, 1 - bu0, bv0, parts.back)});
    }
}

void AddRing(std::vector<MeshVertex>& out, float r_in, float r_out, float depth, Part top, Part back, float cz,
             int segments) {
    const float step = 2 * PI / static_cast<float>(segments);
    const float zt = cz + depth * 0.5f, zb = cz - depth * 0.5f;
    const auto uv = [&](float x, float y) { return std::pair{0.5f + x / (2 * r_out), 0.5f - y / (2 * r_out)}; };
    for (int i = 0; i < segments; ++i) {
        const float a0 = step * i, a1 = step * (i + 1);
        const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        const Vec3 o0{c0 * r_out, s0 * r_out, 0}, o1{c1 * r_out, s1 * r_out, 0};
        const Vec3 i0{c0 * r_in, s0 * r_in, 0}, i1{c1 * r_in, s1 * r_in, 0};
        const auto face = [&](Vec3 p, float z, float nz, Part part) {
            const auto [u, v] = uv(p.x, p.y);
            return V({p.x, p.y, z}, {0, 0, nz}, u, v, part);
        };
        out.insert(out.end(), {face(o0, zt, 1, top), face(o1, zt, 1, top), face(i1, zt, 1, top),
                               face(o0, zt, 1, top), face(i1, zt, 1, top), face(i0, zt, 1, top)});
        out.insert(out.end(), {face(o0, zb, -1, back), face(i1, zb, -1, back), face(o1, zb, -1, back),
                               face(o0, zb, -1, back), face(i0, zb, -1, back), face(i1, zb, -1, back)});
        const Vec3 n0{c0, s0, 0}, n1{c1, s1, 0};
        const Part rim_part = (top == Part::Clear || top == Part::DiscHub || top == Part::Smoke) ? top : Part::Shell;
        const auto rim = [&](Vec3 p, float z, Vec3 nrm) { return V({p.x, p.y, z}, nrm, 0, 0, rim_part); };
        out.insert(out.end(), {rim(o0, zt, n0), rim(o0, zb, n0), rim(o1, zb, n1),
                               rim(o0, zt, n0), rim(o1, zb, n1), rim(o1, zt, n1)});
        const Vec3 m0{-c0, -s0, 0}, m1{-c1, -s1, 0};
        out.insert(out.end(), {rim(i0, zt, m0), rim(i1, zb, m1), rim(i0, zb, m0),
                               rim(i0, zt, m0), rim(i1, zt, m1), rim(i1, zb, m1)});
    }
}

void AddGroove(std::vector<MeshVertex>& out, const std::vector<Vec2>& outline, float width, float z, float cx,
               float cy, Part part, bool raised, float photo_width) {
    const std::size_t n = outline.size();
    if (n < 3 || width <= 0) {
        return;
    }
    std::vector<Vec2> edge(n), inner(n);
    for (std::size_t i = 0; i < n; ++i) {
        edge[i] = EdgeNormal(outline[i], outline[(i + 1) % n]);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 e0 = edge[(i + n - 1) % n], e1 = edge[i];
        const Vec2 m = Normalized(Vec2{e0.x + e1.x, e0.y + e1.y});
        const float miter = width / std::max(0.35f, m.x * e1.x + m.y * e1.y);
        inner[i] = Vec2{outline[i].x - m.x * miter, outline[i].y - m.y * miter};
    }
    // A wall going down into a pocket faces its middle; one rising to a ridge faces out.
    const float lean = raised ? 0.72f : -0.72f, up = 0.69f;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const Vec3 nrm{edge[i].x * lean, edge[i].y * lean, up};
        const auto at = [&](Vec2 p) {
            const float u = photo_width > 0 ? (cx + p.x) / photo_width + 0.5f : 0.0f;
            const float v = photo_width > 0 ? 0.5f - (cy + p.y) : 0.0f;
            return V({cx + p.x, cy + p.y, z}, nrm, u, v, part);
        };
        out.insert(out.end(), {at(outline[i]), at(outline[j]), at(inner[j]), at(outline[i]), at(inner[j]), at(inner[i])});
    }
}

void AddBox(std::vector<MeshVertex>& out, float w, float h, float d, Part part, float cx, float cy, float cz) {
    AddSlab(out, RoundedOutline(w, h, 0, 0), d, 0, SlabParts{part, part, part, part, part}, cz, cx, cy, 0);
}

} // namespace den::gfx
