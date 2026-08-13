"""Regenerate the binary assets that ship inside the executable.

Outputs:
    resources/app.ico          multi-resolution application icon
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

SAMPLE_RATE = 48_000

# Xbox green, lightened a little so the shape stays legible on dark backgrounds.
GREEN_TOP = (26, 168, 26, 255)
GREEN_BOTTOM = (11, 106, 11, 255)
DETAIL = (255, 255, 255, 235)


def _vertical_gradient(size, top, bottom):
    ramp = Image.new("RGBA", (1, size))
    for y in range(size):
        t = y / max(size - 1, 1)
        ramp.putpixel((0, y), tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    return ramp.resize((size, size), Image.NEAREST)


def _rotated_ellipse(size, cx, cy, w, h, angle):
    """An ellipse rotated about its own centre, returned as a full-canvas mask."""
    pad = int(max(w, h) * size)
    layer = Image.new("L", (pad * 2, pad * 2), 0)
    ImageDraw.Draw(layer).ellipse(
        [pad - w * size / 2, pad - h * size / 2, pad + w * size / 2, pad + h * size / 2],
        fill=255)
    layer = layer.rotate(angle, resample=Image.BICUBIC, center=(pad, pad))
    out = Image.new("L", (size, size), 0)
    out.paste(layer, (int(cx * size) - pad, int(cy * size) - pad), layer)
    return out


def _union(base, *masks):
    for m in masks:
        base.paste(255, (0, 0), m)
    return base


def _gamepad_masks(size, detail_level):
    """Return (body, details) masks for an Xbox-shaped controller drawn at `size` px.

    detail_level: 0 = silhouette only, 1 = sticks and d-pad, 2 = everything.
    Small icons get a stubbier silhouette rather than a downscale of the large one --
    the real proportions turn to mush below about 24 px.
    """
    def px(*v):
        return [x * size for x in v]

    compact = detail_level == 0
    body = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(body)

    if compact:
        d.rounded_rectangle(px(0.115, 0.280, 0.885, 0.665), radius=0.175 * size, fill=255)
        grip_w, grip_h, grip_x, grip_y, grip_a = 0.330, 0.330, 0.212, 0.590, 15
        dip_y, dip_r = 0.138, 0.155
        notch_y, notch_r = 0.828, 0.248
    else:
        # Shoulder bumpers sit behind and above the main slab.
        d.rounded_rectangle(px(0.278, 0.190, 0.722, 0.325), radius=0.058 * size, fill=255)
        d.rounded_rectangle(px(0.180, 0.252, 0.820, 0.640), radius=0.140 * size, fill=255)
        grip_w, grip_h, grip_x, grip_y, grip_a = 0.318, 0.395, 0.226, 0.598, 20
        dip_y, dip_r = 0.088, 0.132
        notch_y, notch_r = 0.842, 0.218

    _union(body,
           _rotated_ellipse(size, grip_x, grip_y, grip_w, grip_h, -grip_a),
           _rotated_ellipse(size, 1 - grip_x, grip_y, grip_w, grip_h, grip_a))

    # The dip between the bumpers and the notch between the grips are what make the
    # silhouette read as a controller rather than a blob.
    cut = Image.new("L", (size, size), 0)
    cd = ImageDraw.Draw(cut)
    cd.ellipse(px(0.5 - dip_r, dip_y - dip_r, 0.5 + dip_r, dip_y + dip_r), fill=255)
    cd.ellipse(px(0.5 - notch_r, notch_y - notch_r, 0.5 + notch_r, notch_y + notch_r), fill=255)
    body.paste(0, (0, 0), cut)

    details = Image.new("L", (size, size), 0)
    if detail_level >= 1:
        dd = ImageDraw.Draw(details)
        # Xbox layout: left stick high-left, d-pad low-left, face buttons high-right,
        # right stick low-centre-right.
        for cx, cy in ((0.325, 0.398), (0.588, 0.548)):
            dd.ellipse(px(cx - 0.068, cy - 0.068, cx + 0.068, cy + 0.068), fill=255)
            dd.ellipse(px(cx - 0.036, cy - 0.036, cx + 0.036, cy + 0.036), fill=105)
        cx, cy, arm, thick = 0.412, 0.548, 0.072, 0.026
        dd.rounded_rectangle(px(cx - thick, cy - arm, cx + thick, cy + arm),
                             radius=0.011 * size, fill=255)
        dd.rounded_rectangle(px(cx - arm, cy - thick, cx + arm, cy + thick),
                             radius=0.011 * size, fill=255)
    if detail_level >= 2:
        dd = ImageDraw.Draw(details)
        br, bo = 0.031, 0.072
        for ox, oy in ((0, -bo), (0, bo), (-bo, 0), (bo, 0)):
            dd.ellipse(px(0.690 + ox - br, 0.395 + oy - br, 0.690 + ox + br, 0.395 + oy + br),
                       fill=255)
        dd.ellipse(px(0.5 - 0.038, 0.300 - 0.038, 0.5 + 0.038, 0.300 + 0.038), fill=255)

    return body, details


def render_icon(size):
    ss = 8 if size <= 64 else 4          # supersample factor
    work = size * ss
    detail_level = 2 if size >= 40 else (1 if size >= 24 else 0)

    body, details = _gamepad_masks(work, detail_level)
    img = Image.new("RGBA", (work, work), (0, 0, 0, 0))
    img.paste(_vertical_gradient(work, GREEN_TOP, GREEN_BOTTOM), (0, 0), body)
    img.paste(Image.new("RGBA", (work, work), DETAIL), (0, 0), details)
    return img.resize((size, size), Image.LANCZOS)


def build_icon():
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    frames = [render_icon(s) for s in sizes]
    frames[-1].save(RESOURCES / "app.ico", format="ICO",
                    sizes=[(s, s) for s in sizes], append_images=frames[:-1])
    print("wrote {} ({})".format(RESOURCES / "app.ico", ", ".join(str(s) for s in sizes)))


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
