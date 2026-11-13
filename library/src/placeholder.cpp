// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "den/base/files.h"
#include "den/gfx/images.h"
#include "den/library/artwork.h"
#include "den/library/names.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

// 5x7 glyphs for A-Z and 0-9, one byte per row, bit 4 is the left column.
constexpr std::uint8_t FONT[36][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
};

int GlyphIndex(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= '0' && c <= '9') {
        return 26 + c - '0';
    }
    return -1;
}

// Up to two characters that stand for a title: "The Legend of Zelda" -> "LZ", "Tetris" -> "TE".
std::string Initials(std::string_view title) {
    static constexpr std::array<std::string_view, 9> SMALL{"the", "a",  "an", "of", "and",
                                                           "no",  "de", "in", "to"};
    const std::string base = ParseGameName(title).base;
    std::vector<std::string> words;
    std::string word;
    for (const char c : base + " ") {
        if (GlyphIndex(c) >= 0) {
            word.push_back(c);
        } else if (!word.empty()) {
            if (std::find(SMALL.begin(), SMALL.end(), ToLower(word)) == SMALL.end()) {
                words.push_back(word);
            }
            word.clear();
        }
    }
    if (words.empty()) {
        return "?";
    }
    if (words.size() == 1 || GlyphIndex(words[1].front()) >= 26) {
        // One word, or a sequel number: "Portal 2" -> "P2", "Tetris" -> "TE".
        std::string out = words[0].substr(0, words.size() == 1 ? 2 : 1);
        if (words.size() > 1) {
            out.push_back(words[1].front());
        }
        return out;
    }
    return std::string{words[0].front(), words[1].front()};
}

void HsvToRgb(double hue, double saturation, double value, std::uint8_t out[3]) {
    const double c = value * saturation;
    const double h = std::fmod(hue, 360.0) / 60.0;
    const double x = c * (1.0 - std::fabs(std::fmod(h, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    switch (static_cast<int>(h)) {
    case 0:
        r = c, g = x;
        break;
    case 1:
        r = x, g = c;
        break;
    case 2:
        g = c, b = x;
        break;
    case 3:
        g = x, b = c;
        break;
    case 4:
        r = x, b = c;
        break;
    default:
        r = c, b = x;
        break;
    }
    const double m = value - c;
    out[0] = static_cast<std::uint8_t>((r + m) * 255.0);
    out[1] = static_cast<std::uint8_t>((g + m) * 255.0);
    out[2] = static_cast<std::uint8_t>((b + m) * 255.0);
}

void AppendBe32(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

void AppendChunk(std::string& png, const char type[4], std::string_view data) {
    AppendBe32(png, static_cast<std::uint32_t>(data.size()));
    std::string body(type, 4);
    body += data;
    png += body;
    AppendBe32(png, Crc32(body.data(), body.size()));
}

// 8-bit RGB rows (each led by a filter byte) as a PNG with stored, uncompressed data.
std::string EncodePng(unsigned width, unsigned height, std::string_view rows) {
    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::string header;
    AppendBe32(header, width);
    AppendBe32(header, height);
    header += std::string("\x08\x02\x00\x00\x00", 5);
    AppendChunk(png, "IHDR", header);

    std::string zlib("\x78\x01", 2);
    std::uint32_t a = 1, b = 0;
    for (std::size_t at = 0; at < rows.size();) {
        const std::size_t size = std::min<std::size_t>(rows.size() - at, 0xFFFF);
        const bool last = at + size == rows.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<char>(size & 0xFF));
        zlib.push_back(static_cast<char>(size >> 8));
        zlib.push_back(static_cast<char>(~size & 0xFF));
        zlib.push_back(static_cast<char>((~size >> 8) & 0xFF));
        zlib.append(rows.substr(at, size));
        for (std::size_t i = at; i < at + size; ++i) {
            a = (a + static_cast<unsigned char>(rows[i])) % 65521;
            b = (b + a) % 65521;
        }
        at += size;
    }
    AppendBe32(zlib, (b << 16) | a);
    AppendChunk(png, "IDAT", zlib);
    AppendChunk(png, "IEND", {});
    return png;
}

} // namespace

bool WritePlaceholderCover(const fs::path& file, std::string_view title,
                           std::string_view system_id) {
    constexpr unsigned WIDTH = 200, HEIGHT = 300, CELL = 13;
    const std::uint64_t system_hash = Fnv1a(FNV_OFFSET, system_id.data(), system_id.size());
    const std::uint64_t title_hash = Fnv1a(FNV_OFFSET, title.data(), title.size());
    // A hue per system, nudged by the title so neighbours differ a little.
    const double hue = static_cast<double>(system_hash % 360) +
                       static_cast<double>(title_hash % 31) - 15.0 + 360.0;

    const std::string initials = Initials(title);
    const unsigned glyphs = static_cast<unsigned>(initials.size());
    const unsigned text_width = glyphs * 5 * CELL + (glyphs - 1) * CELL;
    const unsigned text_left = (WIDTH - text_width) / 2;
    const unsigned text_top = (HEIGHT - 7 * CELL) / 2;

    std::string rows;
    rows.reserve((WIDTH * 3 + 1) * HEIGHT);
    for (unsigned y = 0; y < HEIGHT; ++y) {
        rows.push_back(0);
        std::uint8_t colour[3];
        HsvToRgb(hue, 0.55, 0.62 - 0.30 * y / HEIGHT, colour);
        for (unsigned x = 0; x < WIDTH; ++x) {
            bool lit = false;
            if (y >= text_top && y < text_top + 7 * CELL && x >= text_left &&
                x < text_left + text_width) {
                const unsigned column = (x - text_left) / CELL;
                const unsigned glyph = column / 6;
                const unsigned bit = column % 6;
                const int index = GlyphIndex(initials[glyph]);
                lit = bit < 5 && index >= 0 &&
                      (FONT[index][(y - text_top) / CELL] >> (4 - bit)) & 1;
            }
            for (int channel = 0; channel < 3; ++channel) {
                rows.push_back(static_cast<char>(lit ? 0xF4 : colour[channel]));
            }
        }
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    return WriteFileAtomic(file, EncodePng(WIDTH, HEIGHT, rows));
}

bool WriteHackMarkedCover(const fs::path& image, const fs::path& out) {
    auto loaded = gfx::LoadImageFile(image);
    if (!loaded || loaded->width < 64 || loaded->height < 64) {
        return false;
    }
    gfx::ImageData& picture = *loaded;
    gfx::ShrinkImage(picture, 512);
    // A dark tag in the bottom left corner that says HACK, sized with the picture. The top
    // corners are where tiles put their own badges.
    static constexpr std::string_view TEXT = "HACK";
    const int cell = std::max(2, picture.width / 80);
    const int columns = static_cast<int>(TEXT.size()) * 6 - 1;
    const int tag_w = std::min(picture.width, (columns + 4) * cell);
    const int tag_h = std::min(picture.height, 11 * cell);
    for (int y = 0; y < tag_h; ++y) {
        for (int x = 0; x < tag_w; ++x) {
            const int column = x / cell - 2, row = y / cell - 2;
            bool lit = false;
            if (column >= 0 && column < columns && row >= 0 && row < 7 && column % 6 < 5) {
                const int index = GlyphIndex(TEXT[static_cast<std::size_t>(column / 6)]);
                lit = index >= 0 && ((FONT[index][row] >> (4 - column % 6)) & 1);
            }
            const auto line = static_cast<std::size_t>(picture.height - tag_h + y);
            std::uint8_t* pixel = &picture.rgba[(line * picture.width + x) * 4];
            for (int channel = 0; channel < 3; ++channel) {
                // The tag is 85% opaque, so the box still shows through a little.
                pixel[channel] = lit ? 0xF4 : static_cast<std::uint8_t>(pixel[channel] * 15 / 100 + 16);
            }
            pixel[3] = 0xFF;
        }
    }
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    return gfx::SavePng(out, picture.width, picture.height, picture.rgba.data()).ok;
}

} // namespace den::library
