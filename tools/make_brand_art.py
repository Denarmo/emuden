"""Draws every copy of EmuDEN's logo from one definition.

    python -I tools/make_brand_art.py

The mark is a D whose counter is a d-pad, with a dot at its baseline ("D."). It is defined
once below in a 96-unit square and written out as:
  assets/branding/mark-white.png   the mark in white on transparent, tinted by the app's theme
  assets/branding/icon-256.png     the app icon: white mark on a blue tile (window icon)
  shells/linux/emuden.svg          the same icon as SVG (desktop entry, AppImage)
  shells/windows/emuden.ico        the executable's icon, 16 to 256 pixels
  shells/android/.../ic_launcher_*.xml   Android's adaptive and themed icon layers
  shells/linux/steam/*.png         Steam's cover, capsule, banner, logo and icon
The look follows the app: white page, #0000ff ink, soft blue tints, Google Sans.
"""
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT = ROOT / "assets" / "fonts" / "GoogleSans.ttf"
BLUE = (0, 0, 255, 255)
WHITE = (255, 255, 255, 255)
TINT = (234, 236, 255, 255)
TINT_DEEP = (214, 218, 255, 255)
SS = 4  # drawn this many times larger, then reduced, for smooth edges

# The mark in a 96-unit square: the D, the d-pad cut out of it, and the dot.
D_PATH = "M8 14h28a34 34 0 0 1 0 68H8z"
PAD_PATH = "M31 33h10v10h10v10H41v10H31V53H21V43h10z"
PAD_ROUND = 1.5  # the d-pad's corners are rounded by this much
DOT = (84, 74, 8)  # centre x, centre y, radius
TILE_RADIUS = 22
TILE_INSET, TILE_SCALE = 15, 0.69  # where the mark sits on an icon tile


def mark_mask(px):
    """The mark as an L mask `px` pixels square (255 = mark)."""
    s = px * SS / 96
    m = Image.new("L", (px * SS, px * SS), 0)
    d = ImageDraw.Draw(m)
    d.rectangle([8 * s, 14 * s, 36 * s, 82 * s], fill=255)
    d.pieslice([2 * s, 14 * s, 70 * s, 82 * s], -90, 90, fill=255)
    r = PAD_ROUND * s
    d.rounded_rectangle([21 * s - r, 43 * s - r, 51 * s + r, 53 * s + r], radius=2 * r, fill=0)
    d.rounded_rectangle([31 * s - r, 33 * s - r, 41 * s + r, 63 * s + r], radius=2 * r, fill=0)
    cx, cy, cr = DOT
    d.ellipse([(cx - cr) * s, (cy - cr) * s, (cx + cr) * s, (cy + cr) * s], fill=255)
    return m.resize((px, px), Image.LANCZOS)


def mark(px, color=BLUE):
    img = Image.new("RGBA", (px, px), color[:3] + (0,))
    img.putalpha(mark_mask(px))
    return img


def tile(px, bg=BLUE, fg=WHITE):
    img = Image.new("RGBA", (px * SS, px * SS), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle([0, 0, px * SS - 1, px * SS - 1],
                                          radius=TILE_RADIUS * px * SS / 96, fill=bg)
    img = img.resize((px, px), Image.LANCZOS)
    inner = round(px * TILE_SCALE)
    img.alpha_composite(mark(inner, fg), (round(px * TILE_INSET / 96),) * 2)
    return img


def font(size, weight):
    face = ImageFont.truetype(str(FONT), size)
    face.set_variation_by_name(weight)
    return face


def text(label, size, weight="Bold", ink=BLUE):
    face = font(size * SS, weight)
    box = face.getbbox(label)
    img = Image.new("RGBA", (box[2] - box[0], box[3] - box[1]), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((-box[0], -box[1]), label, font=face, fill=ink)
    return img.resize((max(1, img.width // SS), max(1, img.height // SS)), Image.LANCZOS)


def wordmark(size, ink=BLUE):
    """"Emu" in medium weight followed by "DEN" in bold, on one baseline."""
    face_m, face_b = font(size * SS, "Medium"), font(size * SS, "Bold")
    w = face_m.getlength("Emu") + face_b.getlength("DEN")
    asc, desc = face_b.getmetrics()
    img = Image.new("RGBA", (round(w) + 8, asc + desc), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.text((0, 0), "Emu", font=face_m, fill=ink)
    d.text((face_m.getlength("Emu"), 0), "DEN", font=face_b, fill=ink)
    img = img.crop(img.getbbox())
    return img.resize((max(1, img.width // SS), max(1, img.height // SS)), Image.LANCZOS)


def lockup(height, ink=BLUE):
    """The mark beside the wordmark, the wordmark's capitals as tall as the D."""
    m = mark(height, ink)
    word = wordmark(round(height * 0.62), ink)
    gap = round(height * 0.12)
    img = Image.new("RGBA", (m.width + gap + word.width, height), (0, 0, 0, 0))
    img.alpha_composite(m, (0, 0))
    # The D spans 14..82 of the mark's 96 units: the word is centred on it.
    centre = round(height * 48 / 96)
    img.alpha_composite(word, (m.width + gap, centre - word.height // 2))
    return img


def gradient(w, h, top, bottom):
    col = Image.new("RGBA", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        col.putpixel((0, y), tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(4)))
    return col.resize((w, h))


def faint_marks(img, places):
    """Large pale copies of the mark as a backdrop: (centre x, centre y, size, alpha)."""
    for cx, cy, size, alpha in places:
        m = mark(size, (0, 0, 255, 255))
        m.putalpha(m.getchannel("A").point(lambda a: a * alpha // 255))
        img.alpha_composite(m, (round(cx - size / 2), round(cy - size / 2)))


def steam(out):
    out.mkdir(parents=True, exist_ok=True)
    w, h = 600, 900
    cover = gradient(w, h, WHITE, TINT)
    m = mark(330)
    cover.alpha_composite(m, ((w - m.width) // 2, round(h * 0.40 - m.height / 2)))
    word = wordmark(96)
    cover.alpha_composite(word, ((w - word.width) // 2, round(h * 0.70)))
    line = text("Every console, one place", 30, "Medium", (0, 0, 255, 170))
    cover.alpha_composite(line, ((w - line.width) // 2, round(h * 0.70) + word.height + 30))
    cover.save(out / "cover.png", optimize=True)

    w, h = 920, 430
    capsule = gradient(w, h, WHITE, TINT)
    faint_marks(capsule, [(w * 0.97, h * 0.86, 300, 18)])
    lock = lockup(190)
    capsule.alpha_composite(lock, ((w - lock.width) // 2, (h - lock.height) // 2))
    capsule.save(out / "capsule.png", optimize=True)

    w, h = 3840, 1240
    hero = gradient(w, h, TINT, TINT_DEEP)
    faint_marks(hero, [(w * 0.80, h * 0.55, 1500, 26), (w * 0.08, h * 0.95, 700, 16)])
    hero.save(out / "hero.png", optimize=True)

    lock = lockup(330)
    logo = Image.new("RGBA", (lock.width + 48, lock.height + 48), (0, 0, 0, 0))
    logo.alpha_composite(lock, (24, 24))
    logo.save(out / "logo.png", optimize=True)
    tile(256).save(out / "icon.png", optimize=True)


def icon_svg():
    """The app icon as SVG: the d-pad is drawn in the tile's blue over the white D."""
    s, t = TILE_SCALE, TILE_INSET
    cx, cy, r = DOT
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 96 96">
  <rect width="96" height="96" rx="{TILE_RADIUS}" fill="#0000ff"/>
  <g transform="translate({t} {t}) scale({s})">
    <path d="{D_PATH}" fill="#ffffff"/>
    <path d="{PAD_PATH}" fill="#0000ff" stroke="#0000ff" stroke-width="{PAD_ROUND * 2}" stroke-linejoin="round"/>
    <circle cx="{cx}" cy="{cy}" r="{r}" fill="#ffffff"/>
  </g>
</svg>
"""


def android(res):
    """Adaptive icon: a blue background and the white mark in the 66 dp safe circle."""
    cx, cy, r = DOT
    dot = f"M{cx - r},{cy}a{r},{r} 0,1 0,{2 * r},0a{r},{r} 0,1 0,{-2 * r},0z"
    d = "M8,14h28a34,34 0,0 1,0 68H8z"
    pad = "M31,33h10v10h10v10H41v10H31V53H21V43h10z"
    scale, tx, ty = 0.62, 54 - 50 * 0.62, 54 - 48 * 0.62
    head = ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
            '    android:width="108dp"\n    android:height="108dp"\n'
            '    android:viewportWidth="108"\n    android:viewportHeight="108">\n'
            f'    <group android:scaleX="{scale}" android:scaleY="{scale}"\n'
            f'        android:translateX="{tx:.2f}" android:translateY="{ty:.2f}">\n')
    tail = "    </group>\n</vector>\n"
    fg = (head +
          f'        <path android:fillColor="#FFFFFF" android:pathData="{d}" />\n'
          f'        <path android:fillColor="#0000FF" android:strokeColor="#0000FF"\n'
          f'            android:strokeWidth="{PAD_ROUND * 2}" android:strokeLineJoin="round"\n'
          f'            android:pathData="{pad}" />\n'
          f'        <path android:fillColor="#FFFFFF" android:pathData="{dot}" />\n' + tail)
    # Themed icons tint every opaque pixel alike: the d-pad must be a hole here.
    mono = (head +
            f'        <path android:fillColor="#FFFFFF" android:fillType="evenOdd"\n'
            f'            android:pathData="{d} {pad} {dot}" />\n' + tail)
    (res / "drawable" / "ic_launcher_foreground.xml").write_text(fg, encoding="utf-8", newline="\n")
    (res / "drawable" / "ic_launcher_monochrome.xml").write_text(mono, encoding="utf-8", newline="\n")
    (res / "mipmap-anydpi-v26" / "ic_launcher.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@color/ic_launcher_background" />\n'
        '    <foreground android:drawable="@drawable/ic_launcher_foreground" />\n'
        '    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />\n'
        '</adaptive-icon>\n', encoding="utf-8", newline="\n")
    (res / "values" / "ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        '    <color name="ic_launcher_background">#0000FF</color>\n</resources>\n',
        encoding="utf-8", newline="\n")


def main():
    branding = ROOT / "assets" / "branding"
    branding.mkdir(parents=True, exist_ok=True)
    mark(256, WHITE).save(branding / "mark-white.png", optimize=True)
    tile(256).save(branding / "icon-256.png", optimize=True)
    (ROOT / "shells" / "linux" / "emuden.svg").write_text(icon_svg(), encoding="utf-8", newline="\n")
    windows = ROOT / "shells" / "windows"
    windows.mkdir(parents=True, exist_ok=True)
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    tile(256).save(windows / "emuden.ico", sizes=[(s, s) for s in sizes])
    android(ROOT / "shells" / "android" / "app" / "src" / "main" / "res")
    steam(ROOT / "shells" / "linux" / "steam")
    print("written")


if __name__ == "__main__":
    main()
