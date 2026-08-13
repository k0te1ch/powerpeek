"""Regenerate the binary assets that ship inside the executable.

Outputs:
    resources/app.ico          multi-resolution application icon
    docs/images/icon.png       the 256 px frame of that icon, for the documentation
    resources/sounds/*.wav     the five built-in notification sounds

The assets are committed, so this script only needs to run when a design changes.

    python tools/generate_assets.py

Requires Pillow and NumPy (development-only; the application itself has no dependencies).
"""

from __future__ import annotations

import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
RESOURCES = ROOT / "resources"
SOUNDS = RESOURCES / "sounds"
DOCS_IMAGES = ROOT / "docs" / "images"

SAMPLE_RATE = 48_000

# A bright emerald, not the Xbox green this icon used to wear: the app reports the
# charge of headphones, mice, pens and pads, so a console brand colour points at the
# wrong thing. Emerald also survives both taskbars -- #107C10 sinks into a dark one.
GREEN_TOP = (61, 220, 132, 255)
GREEN_BOTTOM = (14, 150, 78, 255)

ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def _vertical_gradient(size, top, bottom):
    ramp = Image.new("RGBA", (1, size))
    for y in range(size):
        t = y / max(size - 1, 1)
        ramp.putpixel((0, y), tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    return ramp.resize((size, size), Image.NEAREST)


def _rounded_rect(draw, box, radius):
    """Fill a rounded rectangle over the half-open box [x0, x1) x [y0, y1).

    Pillow's own rectangle includes its far edge, which would spill one supersampled
    row past a snapped edge and leave a 12%-alpha sliver down the right of the 16 px
    frame. It also rejects a box whose sub-pixel edges round the wrong way, so round
    before handing it over.
    """
    x0, y0, x1, y1 = (round(v) for v in box)
    draw.rounded_rectangle([x0, y0, x1 - 1, y1 - 1], radius=round(radius), fill=255)


def _bolt(draw, size, cy, extent, waist, lean):
    """A lightning bolt centred horizontally, as a polygon in unit coordinates."""
    x, y = size / 2, cy * size
    hw, hh = extent[0] * size / 2, extent[1] * size / 2
    draw.polygon([
        (x + hw * lean, y - hh),
        (x - hw, y + hh * waist),
        (x - hw * waist * 0.5, y + hh * waist),
        (x - hw * lean, y + hh),
        (x + hw, y - hh * waist),
        (x + hw * waist * 0.5, y - hh * waist),
    ], fill=255)


def _cell_masks(size, ss, compact):
    """Return (body, bolt) masks for the charge cell, drawn at `size * ss` px.

    The straight edges are snapped to the *final* pixel grid before being supersampled.
    Left to itself an edge such as 0.195 lands mid-pixel at 16 px and the whole mark
    downsamples to a smudge; snapped, the silhouette comes out sharp.

    compact: below 24 px the mark gets a wider body and a fatter, more upright bolt.
    The large proportions leave a one-pixel wall beside the bolt at that scale, and a
    one-pixel wall is what antialiasing eats first.
    """
    work = size * ss

    def q(u):
        return round(u * size) * ss

    body = Image.new("L", (work, work), 0)
    d = ImageDraw.Draw(body)
    if compact:
        _rounded_rect(d, [q(0.375), q(0.045), q(0.625), q(0.175)], 0.05 * work)
        _rounded_rect(d, [q(0.1875), q(0.145), q(0.8125), q(0.955)], 0.155 * work)
    else:
        _rounded_rect(d, [q(0.385), q(0.040), q(0.615), q(0.165)], 0.05 * work)
        _rounded_rect(d, [q(0.205), q(0.140), q(0.795), q(0.955)], 0.170 * work)

    bolt = Image.new("L", (work, work), 0)
    if compact:
        _bolt(ImageDraw.Draw(bolt), work, 0.55, (0.46, 0.66), waist=0.24, lean=0.06)
    else:
        _bolt(ImageDraw.Draw(bolt), work, 0.545, (0.40, 0.63), waist=0.23, lean=0.09)
    return body, bolt


def render_icon(size):
    ss = 8 if size <= 64 else 4          # supersample factor
    work = size * ss

    body, bolt = _cell_masks(size, ss, compact=size < 24)
    img = Image.new("RGBA", (work, work), (0, 0, 0, 0))
    img.paste(_vertical_gradient(work, GREEN_TOP, GREEN_BOTTOM), (0, 0), body)
    # The bolt is knocked out rather than painted white, so the mark is one solid colour
    # and cannot half-vanish against a light taskbar.
    img.paste((0, 0, 0, 0), (0, 0), bolt)
    # BOX, not LANCZOS: with uniform supersampling this is an exact area average, so a
    # grid-snapped edge stays a hard edge. Lanczos rings and leaves a pale halo down the
    # side of the cell, which is exactly what ruins the 16 px frame.
    return img.resize((size, size), Image.BOX)


def build_icon():
    frames = [render_icon(s) for s in ICON_SIZES]
    frames[-1].save(RESOURCES / "app.ico", format="ICO",
                    sizes=[(s, s) for s in ICON_SIZES], append_images=frames[:-1])
    print("wrote {} ({})".format(RESOURCES / "app.ico",
                                 ", ".join(str(s) for s in ICON_SIZES)))

    DOCS_IMAGES.mkdir(parents=True, exist_ok=True)
    frames[-1].save(DOCS_IMAGES / "icon.png")
    print("wrote {} (256)".format(DOCS_IMAGES / "icon.png"))


def _voice(freq, start, duration, gain, total, brightness=1.0):
    """One struck-bell voice: a few partials under a fast-attack exponential decay."""
    n = int(total * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE - start
    live = t >= 0
    tt = np.where(live, t, 0.0)

    attack = 1.0 - np.exp(-tt / 0.004)
    decay = np.exp(-tt / (duration * 0.42))
    env = attack * decay * live

    partials = ((1.00, 1.00), (2.00, 0.32 * brightness), (3.00, 0.11 * brightness),
                (4.00, 0.05 * brightness), (2.76, 0.09 * brightness))
    out = np.zeros(n)
    for ratio, amp in partials:
        # Higher partials die away faster, which is what makes it read as a bell.
        out += amp * np.sin(2 * np.pi * freq * ratio * tt) * np.exp(-tt * (ratio - 1) * 2.2)
    return out * env * gain


def _lowpass(signal, a=0.55):
    """One-pole low pass, vectorised as an IIR via lfilter-style cumulative product."""
    # scipy is not a dependency, so run the recurrence directly; the buffers are short.
    out = np.empty_like(signal)
    acc = 0.0
    for i, s in enumerate(signal):
        acc = a * acc + (1.0 - a) * s
        out[i] = acc
    return out


def _reverb(mono, amount=0.28):
    """Cheap smooth tail: a handful of decaying, low-passed taps."""
    out = mono.copy()
    for delay_ms, gain in ((37, 0.55), (61, 0.38), (97, 0.26), (149, 0.16)):
        d = int(delay_ms * SAMPLE_RATE / 1000)
        tap = np.zeros_like(mono)
        tap[d:] = mono[:-d]
        out += _lowpass(tap) * gain * amount
    return out


def _stereo(mono, width=0.35):
    d = int(0.006 * SAMPLE_RATE)
    delayed = np.zeros_like(mono)
    delayed[d:] = mono[:-d]
    left = mono * (1 - width * 0.5) + delayed * width * 0.5
    right = mono * (1 - width * 0.5) - delayed * width * 0.5 + delayed * width
    return np.stack([left, right], axis=1)


def _write_wav(path, stereo, peak_dbfs=-3.0):
    peak = float(np.max(np.abs(stereo)))
    if peak > 0:
        stereo = stereo / peak * (10 ** (peak_dbfs / 20))
    stereo = np.tanh(stereo * 1.08) * 0.98            # gentle limiting, never hard clipping
    data = np.clip(stereo * 32767.0, -32768, 32767).astype("<i2")

    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(data.tobytes())
    print("wrote {} ({:.0f} KiB, {:.2f}s)".format(
        path, path.stat().st_size / 1024, len(data) / SAMPLE_RATE))


NOTE = {name: 440.0 * 2 ** ((midi - 69) / 12) for name, midi in {
    "D5": 74, "E5": 76, "F5": 77, "G5": 79, "A5": 81, "B5": 83,
    "C6": 84, "D6": 86, "E6": 88, "G6": 91, "A6": 93, "C7": 96,
}.items()}

# (filename, [(note, start, duration, gain, brightness)], total seconds, reverb amount)
SCORES = [
    ("connected.wav", [("G5", 0.000, 0.30, 0.60, 0.9),
                       ("C6", 0.075, 0.34, 0.75, 1.0),
                       ("E6", 0.150, 0.55, 0.85, 1.0)], 1.10, 0.30),
    ("disconnected.wav", [("E6", 0.000, 0.28, 0.75, 0.9),
                          ("C6", 0.080, 0.32, 0.65, 0.8),
                          ("G5", 0.160, 0.60, 0.70, 0.7)], 1.10, 0.30),
    ("low.wav", [("A5", 0.000, 0.34, 0.80, 0.65),
                 ("A5", 0.220, 0.46, 0.70, 0.65)], 1.00, 0.24),
    ("critical.wav", [("F5", 0.000, 0.26, 0.85, 1.35),
                      ("E5", 0.185, 0.26, 0.85, 1.35),
                      ("D5", 0.370, 0.60, 0.90, 1.25)], 1.25, 0.22),
    ("charged.wav", [("C6", 0.000, 0.26, 0.55, 1.0),
                     ("E6", 0.070, 0.28, 0.62, 1.0),
                     ("G6", 0.140, 0.32, 0.68, 1.0),
                     ("C7", 0.215, 0.75, 0.80, 1.1)], 1.40, 0.34),
]


def build_sounds():
    for filename, notes, total, verb in SCORES:
        mono = np.zeros(int(total * SAMPLE_RATE))
        for name, start, duration, gain, brightness in notes:
            mono += _voice(NOTE[name], start, duration, gain, total, brightness)
        mono = _reverb(mono, verb)
        tail = int(0.02 * SAMPLE_RATE)          # never end on a discontinuity
        mono[-tail:] *= np.linspace(1.0, 0.0, tail)
        _write_wav(SOUNDS / filename, _stereo(mono))


if __name__ == "__main__":
    build_icon()
    build_sounds()
