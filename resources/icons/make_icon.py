"""Make a multi-resolution Windows .ico from the source brain.png.

Run after editing the source PNG:
    python resources/icons/make_icon.py

Embeds the standard set (16, 24, 32, 48, 64, 128, 256) so Windows can
pick the right size for the title bar / taskbar / alt-tab thumbnails /
File Explorer icon view without rescaling at runtime.
"""
import sys
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
SRC  = HERE / "brain.png"
OUT  = HERE / "brain.ico"
SIZES = [(s, s) for s in (16, 24, 32, 48, 64, 128, 256)]

def main():
    if not SRC.exists():
        sys.exit(f"missing source: {SRC}")
    img = Image.open(SRC).convert("RGBA")
    img.save(OUT, format="ICO", sizes=SIZES)
    kb = OUT.stat().st_size // 1024
    print(f"wrote {OUT}  ({kb} KB, {len(SIZES)} resolutions)")

if __name__ == "__main__":
    main()
