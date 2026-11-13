"""Draws EmuDEN's Steam library artwork into shells/linux/steam/.

    python -I tools/make_steam_art.py

Steam shows a non-Steam shortcut with these files from userdata/<id>/config/grid/:
cover.png (<appid>p.png, 600x900), capsule.png (<appid>.png, 920x430),
hero.png (<appid>_hero.png, 3840x1240), logo.png (<appid>_logo.png, drawn over the hero)
and icon.png (<appid>_icon.png). tools/linux/install-steamos.sh copies them in.
The mark is the one in shells/linux/emuden.svg; the look follows the app: white page,
#0000ff ink, soft blue tints, Google Sans.
"""
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "shells" / "linux" / "steam"
FONT = ROOT / "assets" / "fonts" / "GoogleSans.ttf"
INK = (0, 0, 255, 255)
PAGE = (255, 255, 255, 255)
TINT = (234, 236, 255, 255)
TINT_DEEP = (214, 218, 255, 255)
SS = 4  # drawn this many times larger, then reduced, for smooth edges


def font(size):
    face = ImageFont.truetype(str(FONT), size)
    face.set_variation_by_name("Bold")
    return face


def gradient(w, h, top, bottom):
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(4))
        for x in range(w):
            px[x, y] = c
    return img


def mark(size, ink=INK, tile=PAGE):
    """The pad mark of emuden.svg at `size` pixels, on its rounded white tile (tile=None: no tile)."""
    s = size * SS / 256
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if tile:
        d.rounded_rectangle([0, 0, 256 * s - 1, 256 * s - 1], radius=56 * s, fill=tile)
    d.rounded_rectangle([40 * s, 84 * s, 216 * s, 172 * s], radius=44 * s, outline=ink, width=round(14 * s))
    w = round(12 * s)
    for (x0, y0, x1, y1) in ((84, 110, 84, 146), (66, 128, 102, 128)):
        d.line([x0 * s, y0 * s, x1 * s, y1 * s], fill=ink, width=w)
        for (x, y) in ((x0, y0), (x1, y1)):
            d.ellipse([x * s - w / 2, y * s - w / 2, x * s + w / 2, y * s + w / 2], fill=ink)
    for (cx, cy) in ((160, 140), (184, 116)):
        d.ellipse([(cx - 9) * s, (cy - 9) * s, (cx + 9) * s, (cy + 9) * s], fill=ink)
    return img.resize((size, size), Image.LANCZOS)


def text(label, size, ink=INK):
    face = font(size * SS)
    box = face.getbbox(label)
    img = Image.new("RGBA", (box[2] - box[0], box[3] - box[1]), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((-box[0], -box[1]), label, font=face, fill=ink)
    return img.resize((max(1, img.width // SS), max(1, img.height // SS)), Image.LANCZOS)


def lockup(height, gap=0.28):
    """Mark beside the wordmark, both `height` pixels tall, on a transparent page."""
    m = mark(height)
    word = text("EmuDEN", round(height * 0.62))
    w = m.width + round(height * gap) + word.width
    img = Image.new("RGBA", (w, height), (0, 0, 0, 0))
    img.alpha_composite(m, (0, 0))
    img.alpha_composite(word, (m.width + round(height * gap), (height - word.height) // 2))
    return img


def rings(img, cx, cy, radius, count, ink, width):
    """Soft concentric rounded frames, the pad outline repeated: the backdrop pattern."""
    layer = Image.new("RGBA", (img.width * SS // 2, img.height * SS // 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    k = SS / 2
    for i in range(count):
        r = radius * (1 + i * 0.55)
        d.rounded_rectangle([(cx - r) * k, (cy - r / 2) * k, (cx + r) * k, (cy + r / 2) * k],
                            radius=r / 2 * k, outline=ink, width=round(width * k))
    img.alpha_composite(layer.resize(img.size, Image.LANCZOS))


def cover():
    w, h = 600, 900
    img = gradient(w, h, PAGE, TINT)
    rings(img, w / 2, h * 0.40, 150, 4, (0, 0, 255, 22), 10)
    m = mark(300, tile=None)
    img.alpha_composite(m, ((w - m.width) // 2, round(h * 0.40 - m.height / 2)))
    word = text("EmuDEN", 104)
    img.alpha_composite(word, ((w - word.width) // 2, round(h * 0.70)))
    line = text("Every console, one place", 30, (0, 0, 255, 170))
    img.alpha_composite(line, ((w - line.width) // 2, round(h * 0.70) + word.height + 28))
    return img


def capsule():
    w, h = 920, 430
    img = gradient(w, h, PAGE, TINT)
    rings(img, w * 0.82, h * 0.5, 140, 3, (0, 0, 255, 20), 10)
    lock = lockup(150)
    img.alpha_composite(lock, ((w - lock.width) // 2, (h - lock.height) // 2))
    return img


def hero():
    w, h = 3840, 1240
    img = gradient(w, h, TINT, TINT_DEEP)
    rings(img, w * 0.78, h * 0.5, 420, 5, (0, 0, 255, 26), 26)
    rings(img, w * 0.12, h * 0.95, 260, 3, (0, 0, 255, 16), 20)
    return img


def logo():
    lock = lockup(300)
    pad = 24
    img = Image.new("RGBA", (lock.width + pad * 2, lock.height + pad * 2), (0, 0, 0, 0))
    img.alpha_composite(lock, (pad, pad))
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, make in (("cover", cover), ("capsule", capsule), ("hero", hero), ("logo", logo)):
        make().save(OUT / f"{name}.png", optimize=True)
    mark(256).save(OUT / "icon.png", optimize=True)
    for f in sorted(OUT.glob("*.png")):
        print(f.relative_to(ROOT), Image.open(f).size)


if __name__ == "__main__":
    main()
