"""Draws the small foil sticker a ROM hack's cartridge or disc wears.

    python -I tools/make_hack_sticker.py

Writes assets/branding/hack-sticker.png: "HACK" in Google Sans Bold on a silver tag with a
dark border, transparent round its rounded corners. The 3D shader adds the foil's sheen.
"""
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT = ROOT / "assets" / "fonts" / "GoogleSans.ttf"
W, H, SS = 256, 104, 4


def main():
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = 18 * SS
    d.rounded_rectangle((0, 0, W * SS - 1, H * SS - 1), r, fill=(40, 42, 48, 255))
    inset = 7 * SS
    for y in range(inset, H * SS - inset):
        t = (y - inset) / (H * SS - 2 * inset)
        v = int(236 - 50 * t)
        d.line((inset, y, W * SS - inset, y), fill=(v, v, v + 6, 255))
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((inset, inset, W * SS - inset - 1, H * SS - inset - 1), r - inset, fill=255)
    tag = Image.new("RGBA", im.size, (40, 42, 48, 255))
    tag.paste(im, (0, 0), mask)
    outer = Image.new("L", im.size, 0)
    ImageDraw.Draw(outer).rounded_rectangle((0, 0, W * SS - 1, H * SS - 1), r, fill=255)
    tag.putalpha(outer)
    font = ImageFont.truetype(str(FONT), 64 * SS)
    try:
        font.set_variation_by_name("Bold")
    except Exception:
        pass
    d = ImageDraw.Draw(tag)
    box = d.textbbox((0, 0), "HACK", font=font)
    x = (W * SS - (box[2] - box[0])) / 2 - box[0]
    y = (H * SS - (box[3] - box[1])) / 2 - box[1]
    d.text((x, y), "HACK", font=font, fill=(24, 26, 32, 255))
    tag.resize((W, H), Image.LANCZOS).save(ROOT / "assets" / "branding" / "hack-sticker.png")


if __name__ == "__main__":
    main()
