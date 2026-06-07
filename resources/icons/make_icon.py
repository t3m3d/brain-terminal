"""Make a multi-resolution Windows .ico from the source brain.png.

Run after editing the source PNG:
    python resources/icons/make_icon.py

Embeds the standard set (16, 24, 32, 48, 64, 128, 256) so Windows can
pick the right size for the title bar / taskbar / alt-tab thumbnails /
File Explorer icon view without rescaling at runtime.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
SRC  = HERE / "brain.png"
OUT  = HERE / "brain.ico"
SIZES = [(s, s) for s in (16, 24, 32, 48, 64, 128, 256)]

# Corner radius as a fraction of the icon's edge length. The rounded
# square the artist drew looks ~11 % of the edge; 12 % gives a hair more
# clearance so we don't clip into the dark rounded panel itself.
CORNER_RADIUS_FRAC = 0.12

def rounded_mask(size: int, radius: int) -> Image.Image:
    """Solid-white rounded square on a transparent canvas of `size x size`.
    Used as the alpha mask for the icon — anything outside the rounded
    rectangle becomes transparent, so the corner padding the artist left
    in the source PNG no longer shows as a mismatched square frame on a
    light title bar / taskbar.
    """
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=255)
    return m

def main():
    if not SRC.exists():
        sys.exit(f"missing source: {SRC}")
    img = Image.open(SRC).convert("RGBA")
    w, h = img.size
    if w != h:
        sys.exit(f"source must be square, got {w}x{h}")

    mask = rounded_mask(w, int(round(w * CORNER_RADIUS_FRAC)))
    img.putalpha(mask)

    img.save(OUT, format="ICO", sizes=SIZES)
    kb = OUT.stat().st_size // 1024
    print(f"wrote {OUT}  ({kb} KB, {len(SIZES)} resolutions, corners masked)")

if __name__ == "__main__":
    main()
