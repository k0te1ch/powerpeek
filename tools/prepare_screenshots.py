"""Turn raw window captures into README-ready images.

A screen capture of the app includes the transparent margin the window reserves for its own
drop shadow, and whatever happened to be on the desktop behind it shows through there. This
crops to the window body, restores its rounded corners, and composites a synthetic shadow on
a transparent background so the result sits well on both the light and the dark GitHub theme.

    python tools/prepare_screenshots.py docs/images/*.png

Requires Pillow (development-only).
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

# Must match ui::Metrics in src/ui/Theme.h, times the display scale the capture was taken at.
SHADOW_MARGIN = 24
CORNER_RADIUS = 8

PAD = 34
SHADOW_BLUR = 16
SHADOW_OFFSET_Y = 8
SHADOW_ALPHA = 90


def prepare(path: Path) -> None:
    source = Image.open(path).convert("RGBA")
    body = source.crop((SHADOW_MARGIN, SHADOW_MARGIN,
                        source.width - SHADOW_MARGIN, source.height - SHADOW_MARGIN))

    mask = Image.new("L", body.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, body.width - 1, body.height - 1],
                                           radius=CORNER_RADIUS, fill=255)
    body.putalpha(mask)

    canvas = Image.new("RGBA", (body.width + PAD * 2, body.height + PAD * 2), (0, 0, 0, 0))

    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(
        [PAD, PAD + SHADOW_OFFSET_Y, PAD + body.width, PAD + body.height + SHADOW_OFFSET_Y],
        radius=CORNER_RADIUS, fill=(0, 0, 0, SHADOW_ALPHA))
    canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(SHADOW_BLUR)))
    canvas.alpha_composite(body, (PAD, PAD))

    canvas.save(path)
    print(f"{path}: {source.width}x{source.height} -> {canvas.width}x{canvas.height}")


def main(argv: list[str]) -> int:
    paths = [Path(a) for a in argv[1:]]
    if not paths:
        print(__doc__)
        return 2
    for path in paths:
        prepare(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
