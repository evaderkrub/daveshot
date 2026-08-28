"""Generates assets/icons/daveshot.ico (and a 256px PNG beside it).

The mark is the app's primary action rather than a generic camera: four
selection brackets around a red shutter dot -- "capture this area". Brackets
survive being shrunk to 16px in a way that a lens or an aperture does not,
and the red dot is the colour anchor that makes the icon findable in a
taskbar full of grey.

Each size is drawn at its own geometry rather than downscaled from one large
image: stroke widths and insets that look right at 256px turn to grey mush at
16px. Every size is rendered 8x and reduced with LANCZOS for the antialiasing.

Run from the repo root:  python tools/make_icon.py
Only needed when the artwork changes; the .ico is committed.
"""

from pathlib import Path

from PIL import Image, ImageDraw

# Theme colours, matching src/ui/Theme.cpp's "Wili Dark".
TOP = (35, 37, 44)
BOTTOM = (18, 19, 23)
EDGE = (58, 62, 74)
MARK = (228, 230, 235)
ACCENT = (200, 48, 48)

SIZES = [16, 20, 24, 32, 48, 64, 128, 256]
SS = 8  # supersampling factor


def draw_icon(size: int) -> Image.Image:
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Small sizes need proportionally heavier strokes and a tighter inset, or
    # the brackets disappear into the tile. The arms deliberately stop short
    # of meeting: once they join up the mark stops reading as a selection and
    # becomes a generic framed dot.
    small = size <= 24
    radius = int(s * 0.22)
    inset = int(s * (0.20 if small else 0.24))
    arm = int(s * (0.22 if small else 0.205))
    stroke = max(SS, int(s * (0.095 if small else 0.075)))
    dot = s * (0.115 if small else 0.10)

    # Tile: a vertical gradient, painted as rows then masked to a rounded
    # rectangle. PIL has no gradient fill, and a flat tile reads as flat.
    gradient = Image.new("RGBA", (s, s))
    gd = ImageDraw.Draw(gradient)
    for y in range(s):
        t = y / max(1, s - 1)
        gd.line(
            [(0, y), (s, y)],
            fill=tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3)) + (255,),
        )

    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=radius, fill=255)
    img.paste(gradient, (0, 0), mask)

    # A faint lighter edge, so the tile still has a shape against a dark
    # taskbar or a dark folder background.
    draw.rounded_rectangle(
        [0, 0, s - 1, s - 1], radius=radius, outline=EDGE + (255,), width=max(SS, s // 96)
    )

    # Four selection brackets.
    left, top = inset, inset
    right, bottom = s - 1 - inset, s - 1 - inset
    half = stroke / 2.0
    for x, y, dx, dy in (
        (left, top, 1, 1),
        (right, top, -1, 1),
        (left, bottom, 1, -1),
        (right, bottom, -1, -1),
    ):
        # Horizontal and vertical arms, drawn as rectangles so the corner is a
        # clean square join rather than a rounded line cap.
        draw.rectangle(
            [min(x, x + dx * arm) - half, y - half, max(x, x + dx * arm) + half, y + half],
            fill=MARK + (255,),
        )
        draw.rectangle(
            [x - half, min(y, y + dy * arm) - half, x + half, max(y, y + dy * arm) + half],
            fill=MARK + (255,),
        )

    centre = s / 2.0
    draw.ellipse(
        [centre - dot, centre - dot, centre + dot, centre + dot], fill=ACCENT + (255,)
    )

    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    out_dir = root / "assets" / "icons"
    out_dir.mkdir(parents=True, exist_ok=True)

    frames = [draw_icon(size) for size in SIZES]

    # Pillow writes each listed size into the .ico; Windows picks whichever it
    # needs. The largest is saved separately for documentation and for the
    # about box.
    frames[-1].save(
        out_dir / "daveshot.ico",
        format="ICO",
        sizes=[(s, s) for s in SIZES],
        append_images=frames[:-1],
    )
    frames[-1].save(out_dir / "daveshot.png", format="PNG")

    print(f"wrote {out_dir / 'daveshot.ico'} ({', '.join(str(s) for s in SIZES)})")


if __name__ == "__main__":
    main()
