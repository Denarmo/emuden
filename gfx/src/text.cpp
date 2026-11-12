// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/base/utf8.h"
#include "den/gfx/text.h"

namespace den::gfx {

namespace {
constexpr int ATLAS_SIZE = 2048;
constexpr int PADDING = 1;

std::uint64_t Key(std::uint32_t code, int pixel_size, int font) {
    return (static_cast<std::uint64_t>(font) << 48) | (static_cast<std::uint64_t>(pixel_size) << 32) | code;
}
} // namespace

struct Text::Impl {
    Device* device = nullptr;
    FT_Library library = nullptr;
    struct Face {
        FT_Face face = nullptr;
        std::vector<std::uint8_t> data;
        int pixel_size = 0;
    };
    std::vector<Face> faces;
    Texture atlas;
    int pen_x = PADDING, pen_y = PADDING, row_height = 0;
    std::unordered_map<std::uint64_t, Glyph> glyphs;
    float scale = 1.0f;

    void SetSize(Face& face, int pixel_size) {
        if (face.pixel_size != pixel_size) {
            FT_Set_Pixel_Sizes(face.face, 0, static_cast<FT_UInt>(pixel_size));
            face.pixel_size = pixel_size;
        }
    }

    int PixelSize(float size) const {
        return std::max(4, static_cast<int>(std::lround(size * scale)));
    }

    // The requested font first, then the others as fallbacks.
    Face* FaceFor(std::uint32_t code, int font, FT_UInt& index) {
        if (font >= 0 && font < static_cast<int>(faces.size())) {
            index = FT_Get_Char_Index(faces[font].face, code);
            if (index) {
                return &faces[font];
            }
        }
        for (Face& face : faces) {
            index = FT_Get_Char_Index(face.face, code);
            if (index) {
                return &face;
            }
        }
        return nullptr;
    }

    const Glyph* Rasterise(std::uint32_t code, int pixel_size, int font) {
        FT_UInt index = 0;
        Face* face = FaceFor(code, font, index);
        if (!face) {
            return nullptr;
        }
        SetSize(*face, pixel_size);
        if (FT_Load_Glyph(face->face, index, FT_LOAD_RENDER | FT_LOAD_NO_HINTING)) {
            return nullptr;
        }
        const FT_GlyphSlot slot = face->face->glyph;
        const int w = static_cast<int>(slot->bitmap.width);
        const int h = static_cast<int>(slot->bitmap.rows);
        if (pen_x + w + PADDING > ATLAS_SIZE) {
            pen_x = PADDING;
            pen_y += row_height + PADDING;
            row_height = 0;
        }
        if (pen_y + h + PADDING > ATLAS_SIZE) {
            // Full: start over. Glyphs already queued this frame may draw wrong once.
            log::Warn("gfx", "glyph atlas full, cleared");
            glyphs.clear();
            pen_x = pen_y = PADDING;
            row_height = 0;
            std::vector<std::uint8_t> empty(ATLAS_SIZE * ATLAS_SIZE, 0);
            device->UpdateTexture(atlas, 0, 0, ATLAS_SIZE, ATLAS_SIZE, PixelFormat::R8, empty.data());
        }
        Glyph glyph;
        glyph.width = w;
        glyph.height = h;
        glyph.left = slot->bitmap_left;
        glyph.top = slot->bitmap_top;
        glyph.advance = static_cast<float>(slot->advance.x) / 64.0f;
        if (w > 0 && h > 0) {
            std::vector<std::uint8_t> tight(static_cast<std::size_t>(w) * h);
            for (int y = 0; y < h; ++y) {
                std::copy_n(slot->bitmap.buffer + y * slot->bitmap.pitch, w, tight.data() + y * w);
            }
            device->UpdateTexture(atlas, pen_x, pen_y, w, h, PixelFormat::R8, tight.data());
            glyph.u0 = static_cast<float>(pen_x) / ATLAS_SIZE;
            glyph.v0 = static_cast<float>(pen_y) / ATLAS_SIZE;
            glyph.u1 = static_cast<float>(pen_x + w) / ATLAS_SIZE;
            glyph.v1 = static_cast<float>(pen_y + h) / ATLAS_SIZE;
            pen_x += w + PADDING;
            row_height = std::max(row_height, h);
        }
        return &(glyphs[Key(code, pixel_size, font)] = glyph);
    }
};

Text::Text() : impl_(std::make_unique<Impl>()) {}

Text::~Text() {
    for (auto& face : impl_->faces) {
        FT_Done_Face(face.face);
    }
    if (impl_->library) {
        FT_Done_FreeType(impl_->library);
    }
}

Status Text::Init(Device& device) {
    impl_->device = &device;
    if (FT_Init_FreeType(&impl_->library)) {
        return Status::Fail("FreeType could not start");
    }
    std::vector<std::uint8_t> empty(ATLAS_SIZE * ATLAS_SIZE, 0);
    impl_->atlas = device.CreateTexture(ATLAS_SIZE, ATLAS_SIZE, PixelFormat::R8, empty.data());
    return {};
}

Result<int> Text::AddFont(const std::filesystem::path& file, int weight) {
    auto data = ReadBinaryFile(file);
    if (!data) {
        return Status::Fail("font not found: " + PathToUtf8(file));
    }
    Impl::Face face;
    face.data = std::move(*data);
    if (FT_New_Memory_Face(impl_->library, face.data.data(), static_cast<FT_Long>(face.data.size()), 0,
                           &face.face)) {
        return Status::Fail("not a font: " + PathToUtf8(file));
    }
    FT_MM_Var* axes = nullptr;
    if (FT_Get_MM_Var(face.face, &axes) == 0 && axes) {
        std::vector<FT_Fixed> coords(axes->num_axis);
        for (FT_UInt i = 0; i < axes->num_axis; ++i) {
            coords[i] = axes->axis[i].def;
            if (axes->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
                coords[i] = std::clamp<FT_Fixed>(static_cast<FT_Fixed>(weight) << 16,
                                                 axes->axis[i].minimum, axes->axis[i].maximum);
            }
        }
        FT_Set_Var_Design_Coordinates(face.face, axes->num_axis, coords.data());
        FT_Done_MM_Var(impl_->library, axes);
    }
    impl_->faces.push_back(std::move(face));
    return static_cast<int>(impl_->faces.size()) - 1;
}

void Text::SetScale(float scale) {
    impl_->scale = scale > 0.0f ? scale : 1.0f;
}

float Text::Scale() const {
    return impl_->scale;
}

const Glyph* Text::GetGlyph(std::uint32_t code, int pixel_size, int font) {
    const auto it = impl_->glyphs.find(Key(code, pixel_size, font));
    if (it != impl_->glyphs.end()) {
        return &it->second;
    }
    return impl_->Rasterise(code, pixel_size, font);
}

Texture Text::Atlas() const {
    return impl_->atlas;
}

float Text::Measure(std::string_view text, float size, int font) {
    const int pixel_size = impl_->PixelSize(size);
    float width = 0;
    for (std::size_t at = 0; at < text.size();) {
        if (const Glyph* glyph = GetGlyph(NextCodePoint(text, at), pixel_size, font)) {
            width += glyph->advance;
        }
    }
    return width / impl_->scale;
}

FontMetrics Text::Metrics(float size, int font) {
    FontMetrics metrics;
    if (font < 0 || font >= static_cast<int>(impl_->faces.size())) {
        metrics.ascent = size * 0.8f;
        metrics.descent = size * 0.2f;
        metrics.line_height = size * 1.25f;
        return metrics;
    }
    Impl::Face& face = impl_->faces[font];
    impl_->SetSize(face, impl_->PixelSize(size));
    const FT_Size_Metrics& m = face.face->size->metrics;
    metrics.ascent = static_cast<float>(m.ascender) / 64.0f / impl_->scale;
    metrics.descent = static_cast<float>(-m.descender) / 64.0f / impl_->scale;
    metrics.line_height = static_cast<float>(m.height) / 64.0f / impl_->scale;
    return metrics;
}

std::string Text::Elide(std::string_view text, float size, float max_width, int font) {
    if (Measure(text, size, font) <= max_width) {
        return std::string(text);
    }
    const std::string dots = "\xE2\x80\xA6";
    const float room = max_width - Measure(dots, size, font);
    const int pixel_size = impl_->PixelSize(size);
    float width = 0;
    std::size_t at = 0, keep = 0;
    while (at < text.size()) {
        const Glyph* glyph = GetGlyph(NextCodePoint(text, at), pixel_size, font);
        width += glyph ? glyph->advance / impl_->scale : 0.0f;
        if (width > room) {
            break;
        }
        keep = at;
    }
    std::string out(text.substr(0, keep));
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out + dots;
}

} // namespace den::gfx
