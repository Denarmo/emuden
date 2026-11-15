// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Small lit 3D objects drawn among the canvas's 2D content: game boxes, cartridges, discs.
// They are drawn offscreen with a depth buffer at twice the resolution (smooth edges), then
// laid onto the canvas like an image, optionally fading out (a floor reflection).
//
// Coordinates are the canvas's units with y down; z comes towards the viewer. The camera
// looks at the canvas from the front, so an object at z = 0 is drawn at its canvas size.

#pragma once

#include <vector>

#include "den/base/result.h"
#include "den/gfx/types.h"

namespace den::gfx {

class Canvas;
class Device;

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // column-major

    static Mat4 Translate(float x, float y, float z);
    static Mat4 Scale(float x, float y, float z);
    static Mat4 RotateX(float radians);
    static Mat4 RotateY(float radians);
    static Mat4 RotateZ(float radians);
    Mat4 operator*(const Mat4& o) const;
};

/// What a surface is made of. The fragment shader picks colour, gloss and texture by it.
enum class Part : int {
    Shell = 0,     // moulded plastic in the material's shell colour
    Label = 1,     // a matte paper label: the cover through the label's uv rectangle
    Spine = 2,     // a printed spine under clear plastic: the spine's art, else a slice of the cover
    DiscPrint = 3, // a disc's printed side: the disc's art, else the cover with a clear hub
    Dark = 4,      // the shell colour, darker: grooves, a hinge, a tray
    Gloss = 5,     // the cover under a clear sleeve: shiny, with a sheen at grazing angles
    Metal = 6,     // gold contacts
    Clear = 7,     // clear plastic (drawn in the transparent pass)
    DiscData = 8,  // a disc's data side: silver, with a rainbow sheen
    Paper = 9,     // plain white card
    Smoke = 10,    // tinted see-through plastic in the shell colour, as opaque as its alpha
                   // (drawn in the transparent pass)
    Board = 11,    // a green circuit board
    Steel = 12,    // brushed metal: a battery, a label's foil edge
    Back = 13,     // a box's or cartridge's back: the back's art, else the shell colour
    Photo = 14,    // a cartridge's front as photographed: the cart's art over the shell colour
    Screen = 15,   // a console's screen: dark glass, or lit and showing the boot (Material::boot)
    Led = 16,      // a power light: dark, red once the console is on
    Print = 17,    // the cover printed on cardboard: fine grain, a satin sheen, or foil
    Paint = 18,    // plastic in the vertex's own colour: u = red * 256 + green, v = blue (0..255)
};

struct MeshVertex {
    float x, y, z;    // y up in the mesh; Renderer3D::Model turns it to the canvas's y down
    float nx, ny, nz;
    float u, v;       // 0..1, mapped into the part's uv rectangle
    float part;
};

class Mesh3D {
public:
    Mesh3D() = default;
    ~Mesh3D();
    Mesh3D(const Mesh3D&) = delete;
    Mesh3D& operator=(const Mesh3D&) = delete;
    /// Uploads the vertices (triangles, counter-clockwise from outside).
    void Set(const std::vector<MeshVertex>& vertices);
    bool Empty() const {
        return count_ == 0;
    }

private:
    friend class Renderer3D;
    unsigned int vao_ = 0, vbo_ = 0;
    int count_ = 0;
};

struct Material {
    Color shell{90, 90, 100, 255}; // its alpha is how opaque Smoke is
    Texture cover;             // the label, spine, sleeve and disc print; none: plain card
    Rect label_uv{0, 0, 1, 1}; // the part of the cover on the label or sleeve
    Rect spine_uv{0, 0, 0.08f, 1};
    // Pictures of the other faces, each optional: what Back, Spine, Photo and DiscPrint show.
    Texture back_art, spine_art, cart_art, disc_art;
    Rect cart_uv{0, 0, 1, 1}; // the part of cart_art the cartridge fills
    // A sticker over the photographed cartridge's own label (a ROM hack on the cartridge of the
    // game it was made from): the cover through label_uv, over this part of the front, 0..1.
    Rect sticker{0, 0, 0, 0};
    // A console switching on: below 0 off; 0..1 the screen lighting up, the cover (label_uv)
    // sliding down onto it from 1 to 2 and resting there after.
    float boot = -1;
    bool foil = false; // Print as holographic foil instead of satin card
    float light = 1.0f; // multiplies the result: side objects are darker
};

class Renderer3D {
public:
    Status Init(Device& device);
    /// Flushes the canvas and starts drawing the part `clip` (canvas units) of the 3D view.
    void Begin(Canvas& canvas, const Rect& clip);
    /// Opaque parts first; then DrawClear for the clear plastic of the same objects.
    /// `mirrored` draws a reflection (the model flipped in y by the caller).
    void Draw(const Mesh3D& mesh, const Mat4& model, const Material& material, bool mirrored = false);
    void DrawClear(const Mesh3D& mesh, const Mat4& model, const Material& material, bool mirrored = false);
    /// Lays what was drawn onto the canvas over `clip`, its opacity going from `top` to
    /// `bottom`, and gives the canvas its state back.
    void End(Canvas& canvas, float top = 1.0f, float bottom = 1.0f);

    /// Puts a mesh at canvas point (x, y, z), turned by yaw (about the vertical) and pitch,
    /// with y flipped from the mesh's y-up.
    static Mat4 Model(float x, float y, float z, float yaw, float pitch = 0, float roll = 0);

private:
    void Target(int width, int height);
    void Submit(const Mesh3D& mesh, const Mat4& model, const Material& material, bool mirrored);

    unsigned int program_ = 0, fbo_ = 0, color_ = 0, depth_ = 0;
    int fbo_w_ = 0, fbo_h_ = 0, draw_w_ = 0, draw_h_ = 0;
    int u_view_proj_ = -1, u_model_ = -1, u_rot_ = -1, u_shell_ = -1, u_label_ = -1, u_spine_ = -1,
        u_light_ = -1, u_has_tex_ = -1, u_tex_ = -1, u_clear_ = -1, u_arts_ = -1, u_cart_uv_ = -1, u_sticker_ = -1, u_boot_ = -1, u_foil_ = -1;
    int u_art_[4] = {-1, -1, -1, -1};
    Rect clip_;
    Mat4 view_proj_;
};

} // namespace den::gfx
