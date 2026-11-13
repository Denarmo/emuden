"""Draws every copy of EmuDEN's logo from one definition.

    python -I tools/make_brand_art.py

The mark is a lowercase d with a d-pad in its bowl and two face buttons cut into its lower
right corner. It is defined once below in a 96-unit square and written out as:
  assets/branding/mark-white.png   the mark in white on transparent, tinted by the app's theme
  assets/branding/icon-256.png     the app icon: white mark on a blue tile (window icon)
  shells/linux/emuden.svg          the same icon as SVG (desktop entry, AppImage)
  shells/windows/emuden.ico        the executable's icon, 16 to 256 pixels
  shells/android/.../ic_launcher_*.xml   Android's adaptive and themed icon layers
  shells/linux/steam/*.png         Steam's cover, capsule, banner, logo and icon
The look follows the app: white page, #0000ff ink, soft blue tints, Google Sans.
"""
import math
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT = ROOT / "assets" / "fonts" / "GoogleSans.ttf"
BLUE = (0, 0, 255, 255)
WHITE = (255, 255, 255, 255)
TINT = (234, 236, 255, 255)
TINT_DEEP = (214, 218, 255, 255)
SS = 4  # drawn this many times larger, then reduced, for smooth edges

# The mark in a 96-unit square: a lowercase d (a round bowl and a rounded stem) with a d-pad
# in the bowl and two face buttons outside its lower right corner, in a rounded cut.
BOWL = (38, 58, 28)  # centre x, centre y, radius
BUTTONS = ((66.66, 67.34), (55.34, 78.66))
BUTTON_R = 6
CUT_R, FILLET = 10.5, 3  # the cut keeps CUT_R from each button's centre; its edges are rounded
PAD = (38, 58, 0.78)  # centre and scale of the d-pad (arms 12 wide, 30 long before scaling)
OFFSET = (6.7, 1)  # moves the mark's box to the middle of the square
TILE_RADIUS = 22
TILE_INSET, TILE_SCALE = 15, 0.69  # where the mark sits on an icon tile


def _outline():
    """The d's outline (the stem spans x 52..70 from y 8): (svg path data, list of points)."""
    bx, by, br = BOWL
    (e1, e2), r = BUTTONS, CUT_R
    k = 1 / math.sqrt(2)
    t1 = (e1[0] - r * k, e1[1] - r * k)  # the cut's straight side
    t2 = (e2[0] - r * k, e2[1] - r * k)
    p1 = (70, e1[1] - math.sqrt(r * r - (70 - e1[0]) ** 2))  # the cut meets the stem's side
    # ... and the bowl, at the left one of the two points where the circles cross.
    dx, dy = e2[0] - bx, e2[1] - by
    d = math.hypot(dx, dy)
    a = (br * br - r * r + d * d) / (2 * d)
    h = math.sqrt(br * br - a * a)
    mx, my = bx + a * dx / d, by + a * dy / d
    p2 = min((mx - h * dy / d, my + h * dx / d), (mx + h * dy / d, my - h * dx / d))

    def ang(c, p):
        return math.atan2(p[1] - c[1], p[0] - c[0])

    def on(c, rr, t):
        return (c[0] + rr * math.cos(t), c[1] + rr * math.sin(t))

    a1 = ang(e1, p1) - FILLET / r
    q1 = on(e1, r, a1)
    a2 = ang(e2, p2) + FILLET / r
    if a2 > -3 * math.pi / 4:
        a2 -= 2 * math.pi  # the cut runs clockwise from its straight side to the bowl
    q2 = on(e2, r, a2)
    a3 = ang((bx, by), p2) + FILLET / br
    q3 = on((bx, by), br, a3)
    top = by - math.sqrt(br * br - (52 - bx) ** 2)  # where the bowl meets the stem's left side

    def f(v):
        return f"{v:.2f}"

    path = (f"M52 17a9 9 0 0 1 18 0V{f(p1[1] - FILLET)}Q70 {f(p1[1])} {f(q1[0])} {f(q1[1])}"
            f"A{r} {r} 0 0 0 {f(t1[0])} {f(t1[1])}L{f(t2[0])} {f(t2[1])}"
            f"A{r} {r} 0 0 0 {f(q2[0])} {f(q2[1])}Q{f(p2[0])} {f(p2[1])} {f(q3[0])} {f(q3[1])}"
            f"A{br} {br} 0 1 1 52 {f(top)}z")

    def arc(c, rr, s, e, n=48):
        return [on(c, rr, s + (e - s) * i / n) for i in range(n + 1)]

    def quad(p0, c, p3, n=12):
        return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * c[0] + t * t * p3[0],
                 (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * c[1] + t * t * p3[1])
                for t in (i / n for i in range(n + 1))]

    pts = arc((61, 17), 9, math.pi, 2 * math.pi)
    pts += quad((70, p1[1] - FILLET), p1, q1)
    pts += arc(e1, r, a1, -3 * math.pi / 4)
    pts += arc(e2, r, -3 * math.pi / 4, a2)
    pts += quad(q2, p2, q3)
    pts += arc((bx, by), br, a3, ang((bx, by), (52, top)) + 2 * math.pi, 160)
    return path, pts


def _pad_arms():
    """The d-pad's two arms as (left, top, right, bottom), and their corner radius."""
    cx, cy, s = PAD

    def m(x, y):
        return (cx + (x - 36) * s, cy + (y - 48) * s)

    return [m(30, 33) + m(42, 63), m(21, 42) + m(51, 54)], 2.5 * s


def _pad_path():
    """The d-pad as svg path data, outer corners rounded."""
    (v, hz), q = _pad_arms()
    l, t, r, b = v
    hl, ht, hr, hb = hz

    def f(x):
        return f"{x:.2f}"

    c = f"a{f(q)} {f(q)} 0 0 1"
    return (f"M{f(l)} {f(t + q)}{c} {f(q)} {f(-q)}H{f(r - q)}{c} {f(q)} {f(q)}V{f(ht)}"
            f"H{f(hr - q)}{c} {f(q)} {f(q)}V{f(hb - q)}{c} {f(-q)} {f(q)}H{f(r)}"
            f"V{f(b - q)}{c} {f(-q)} {f(q)}H{f(l + q)}{c} {f(-q)} {f(-q)}V{f(hb)}"
            f"H{f(hl + q)}{c} {f(-q)} {f(-q)}V{f(ht + q)}{c} {f(q)} {f(-q)}H{f(l)}z")


def mark_mask(px):
    """The mark as an L mask `px` pixels square (255 = mark)."""
    s = px * SS / 96
    ox, oy = OFFSET

    def tr(x, y):
        return ((x + ox) * s, (y + oy) * s)

    m = Image.new("L", (px * SS, px * SS), 0)
    d = ImageDraw.Draw(m)
    d.polygon([tr(x, y) for x, y in _outline()[1]], fill=255)
    arms, q = _pad_arms()
    for l, t, r, b in arms:
        d.rounded_rectangle([*tr(l, t), *tr(r, b)], radius=q * s, fill=0)
    for x, y in BUTTONS:
        d.ellipse([*tr(x - BUTTON_R, y - BUTTON_R), *tr(x + BUTTON_R, y + BUTTON_R)], fill=255)
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


def _mark_svg(ink, hole):
    """The mark's shapes as svg elements in the 96-unit square."""
    ox, oy = OFFSET
    dots = "".join(f'<circle cx="{x}" cy="{y}" r="{BUTTON_R}" fill="{ink}"/>' for x, y in BUTTONS)
    return (f'<g transform="translate({ox} {oy})"><path d="{_outline()[0]}" fill="{ink}"/>'
            f'<path d="{_pad_path()}" fill="{hole}"/>{dots}</g>')


def icon_svg():
    """The app icon as SVG: the mark in white on the blue tile."""
    s, t = TILE_SCALE, TILE_INSET
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 96 96">
  <rect width="96" height="96" rx="{TILE_RADIUS}" fill="#0000ff"/>
  <g transform="translate({t} {t}) scale({s})">{_mark_svg("#ffffff", "#0000ff")}</g>
</svg>
"""


def android(res):
    """Adaptive icon: a blue background and the white mark in the 66 dp safe circle."""
    outline, pad = _outline()[0], _pad_path()
    dots = " ".join(f"M{x - BUTTON_R:.2f},{y}a{BUTTON_R},{BUTTON_R} 0,1 0,{2 * BUTTON_R},0"
                    f"a{BUTTON_R},{BUTTON_R} 0,1 0,{-2 * BUTTON_R},0z" for x, y in BUTTONS)
    scale = 0.7
    tx, ty = 54 - (48 - OFFSET[0]) * scale, 54 - (48 - OFFSET[1]) * scale
    head = ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
            '    android:width="108dp"\n    android:height="108dp"\n'
            '    android:viewportWidth="108"\n    android:viewportHeight="108">\n'
            f'    <group android:scaleX="{scale}" android:scaleY="{scale}"\n'
            f'        android:translateX="{tx:.2f}" android:translateY="{ty:.2f}">\n')
    tail = "    </group>\n</vector>\n"
    fg = (head +
          f'        <path android:fillColor="#FFFFFF" android:pathData="{outline}" />\n'
          f'        <path android:fillColor="#0000FF" android:pathData="{pad}" />\n'
          f'        <path android:fillColor="#FFFFFF" android:pathData="{dots}" />\n' + tail)
    # Themed icons tint every opaque pixel alike: the d-pad must be a hole here.
    mono = (head +
            f'        <path android:fillColor="#FFFFFF" android:fillType="evenOdd"\n'
            f'            android:pathData="{outline} {pad} {dots}" />\n' + tail)
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
