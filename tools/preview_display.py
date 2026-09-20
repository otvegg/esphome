#!/usr/bin/env python3
"""Mock renderer for esp32-screensensor.yaml's display lambda.

Not part of the firmware - a standalone visual approximation so the panel
layout can be eyeballed without a full compile+flash cycle. Coordinates and
drawing order are hand-copied from the `display: lambda:` block in
esp32-screensensor.yaml, so if that lambda changes, this script needs to be
updated to match (it will silently drift out of sync otherwise). Font is
Liberation Sans Bold as a stand-in for the real Roboto Bold gfont - metrics
won't be pixel-identical to what the panel actually renders.

Usage: .venv/bin/python tools/preview_display.py
Outputs PNGs into tools/preview_out/.
"""

import datetime
import math
import os

from PIL import Image, ImageDraw, ImageFont

# Logical canvas after rotation: 90 swaps the physical panel's 240x416.
WIDTH = 416
HEIGHT = 240

WHITE = 255  # blank paper
BLACK = 0  # ink

FONT_PATH = "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"
FONT_PATH_REGULAR = "/usr/share/fonts/liberation/LiberationSans-Regular.ttf"
FONT1 = ImageFont.truetype(FONT_PATH, 20)  # id: font1
FONT2 = ImageFont.truetype(FONT_PATH, 16)  # id: font2
FONT3 = ImageFont.truetype(FONT_PATH_REGULAR, 10)  # id: font3 (graph axis labels)

OUT_DIR = os.path.join(os.path.dirname(__file__), "preview_out")

WEEKDAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]


def text_center(draw, cx, top_y, font, text):
    bbox = draw.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0]
    draw.text((cx - w / 2, top_y), text, font=font, fill=BLACK)


def draw_text_align(draw, x, y, font, text, halign="left", valign="top"):
    """Mirrors ESPHome's TextAlign anchors (LEFT/CENTER/RIGHT x TOP/BOTTOM)."""
    bbox = draw.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    if halign == "center":
        x -= w / 2
    elif halign == "right":
        x -= w
    if valign == "bottom":
        y -= h
    draw.text((x - bbox[0], y - bbox[1]), text, font=font, fill=BLACK)


def draw_duration_ticks(draw, x, y, w, duration_hours, step_hours):
    n = duration_hours // step_hours
    for i in range(n + 1):
        hours_ago = duration_hours - i * step_hours
        tx = x + i * (w - 1) // n
        label = f"{hours_ago}t"
        halign = "left" if i == 0 else "right" if i == n else "center"
        draw_text_align(draw, tx, y, FONT3, label, halign=halign, valign="top")


def draw_wifi_bars(draw, rssi):
    bars = 4 if rssi >= -50 else 3 if rssi >= -60 else 2 if rssi >= -70 else 1 if rssi >= -80 else 0
    heights = [6, 10, 14, 18]
    base_x = WIDTH - 42
    baseline_y = 22
    for i in range(4):
        bx = base_x + i * 9
        by = baseline_y - heights[i]
        if i < bars:
            draw.rectangle([bx, by, bx + 6, by + heights[i]], fill=BLACK)
        else:
            draw.rectangle([bx, by, bx + 6, by + heights[i]], outline=BLACK)


def draw_line_graph(draw, x, y, w, h, series):
    """series: list of (values 0..1 normalized, dash_pattern or None)."""
    for values, dash in series:
        n = len(values)
        pts = [(x + i * w / (n - 1), y + h - v * h) for i, v in enumerate(values)]
        if dash is None:
            draw.line(pts, fill=BLACK, width=2)
        else:
            on, off = dash
            for i in range(len(pts) - 1):
                x1, y1 = pts[i]
                x2, y2 = pts[i + 1]
                seg_len = math.hypot(x2 - x1, y2 - y1) or 1
                steps = max(1, int(seg_len))
                for s in range(steps):
                    t0 = s / steps
                    t1 = (s + 1) / steps
                    cycle = (i * steps + s) % (on + off)
                    if cycle < on:
                        draw.line(
                            [
                                (x1 + (x2 - x1) * t0, y1 + (y2 - y1) * t0),
                                (x1 + (x2 - x1) * t1, y1 + (y2 - y1) * t1),
                            ],
                            fill=BLACK,
                            width=2,
                        )


def fake_temp_series(n=48):
    return [0.5 + 0.35 * math.sin(i / 6.0) for i in range(n)]


def fake_weight_series(n=48, phase=0, spikes=(10, 25, 40)):
    base = [0.05 + 0.02 * math.sin(i / 4.0 + phase) for i in range(n)]
    for s in spikes:
        for d in range(-2, 3):
            if 0 <= s + d < n:
                base[s + d] = max(base[s + d], 0.9 - abs(d) * 0.15)
    return base


def render(*, quiet_hours=False, stale=False, path):
    img = Image.new("L", (WIDTH, HEIGHT), WHITE)
    draw = ImageDraw.Draw(img)

    # Outer border.
    draw.rectangle([0, 0, WIDTH - 1, HEIGHT - 1], outline=BLACK)

    now = datetime.datetime.now()

    # Header row: uptime (left), title/frozen-notice (center), wifi bars (right).
    draw.text((6, 8), "up 2h 15m", font=FONT2, fill=BLACK)
    if quiet_hours:
        draw.ellipse([WIDTH // 2 - 34 - 10, 12 - 10, WIDTH // 2 - 34 + 10, 12 + 10], fill=BLACK)
        draw.ellipse([WIDTH // 2 - 29 - 10, 8 - 10, WIDTH // 2 - 29 + 10, 8 + 10], fill=WHITE)
        text_center(draw, WIDTH // 2 - 16 + 30, 4, FONT2, f"Frozen {now.hour:02d}:{now.minute:02d}")
    else:
        text_center(draw, WIDTH // 2, 4, FONT1, "Vær & Vekt")
    draw_wifi_bars(draw, -58)
    draw.line([4, 28, WIDTH - 4, 28], fill=BLACK, width=1)

    # Sensor lines.
    draw.text((10, 34), "AHT20:    21.0 C  62% RH", font=FONT2, fill=BLACK)
    draw.text((10, 52), "BMP280:   21.0 C  994.3 hPa", font=FONT2, fill=BLACK)
    draw.text((10, 70), "Outdoor:  cloud 14.2 C", font=FONT2, fill=BLACK)

    # Load cell threshold counts.
    draw.line([255, 32, 255, 32 + 74], fill=BLACK, width=1)
    draw.text((265, 34), ">500g today:", font=FONT2, fill=BLACK)
    draw.text((265, 52), "Cell1: 3", font=FONT2, fill=BLACK)
    draw.text((265, 70), "Cell2: 1", font=FONT2, fill=BLACK)
    draw.line([4, 108, WIDTH - 4, 108], fill=BLACK, width=1)

    # 3-day forecast with real weekday abbreviations.
    dow = now.weekday()  # Monday=0 - convert to Sunday=0 to match WEEKDAYS
    dow_sun0 = (dow + 1) % 7
    d1, d2, d3 = (WEEKDAYS[(dow_sun0 + off) % 7] for off in (1, 2, 3))
    draw.text(
        (10, 114),
        f"{d1}: 12C sunny | {d2}: 10C cloud | {d3}: 9C rain",
        font=FONT2,
        fill=BLACK,
    )

    if stale:
        draw.text((10, 132), "! No HA update in 7 min", font=FONT2, fill=BLACK)
    draw.line([4, 150, WIDTH - 4, 150], fill=BLACK, width=1)

    # Temperature graph - box shifted right 16px and shortened 6px to leave
    # a gutter for y-axis min/max labels and a strip below for duration
    # ticks, both drawn outside the plot area.
    draw_text_align(draw, 10, 158, FONT3, "23", halign="left", valign="top")
    draw_text_align(draw, 10, 214, FONT3, "18", halign="left", valign="top")
    draw.rectangle([25, 156, 25 + 186, 156 + 70], outline=BLACK)
    draw_line_graph(draw, 26, 157, 184, 64, [(fake_temp_series(), None)])
    draw_duration_ticks(draw, 26, 227, 184, 4, 1)

    # Weight graph - same left-gutter/below-strip layout, plus legend.
    draw_text_align(draw, 220, 158, FONT3, "480", halign="left", valign="top")
    draw_text_align(draw, 220, 214, FONT3, "0", halign="left", valign="top")
    draw.rectangle([235, 156, 235 + 124, 156 + 70], outline=BLACK)
    draw_line_graph(
        draw,
        236,
        157,
        144,
        64,
        [
            (fake_weight_series(spikes=(10, 25, 40)), None),
            (fake_weight_series(phase=1.5, spikes=(15, 35)), (3, 2)),
        ],
    )
    draw_duration_ticks(draw, 236, 227, 144, 8, 2)
    draw.line([364, 178, 364 + 12, 178], fill=BLACK, width=2)
    draw.text((380, 172), "Cell1", font=FONT2, fill=BLACK)
    x = 364
    while x < 376:
        draw.line([x, 210, x + 2, 210], fill=BLACK, width=2)
        x += 3
    draw.text((380, 204), "Cell2", font=FONT2, fill=BLACK)

    # Saved 2x upscaled + antialiased (not 1-bit) purely for on-screen
    # readability while reviewing layout - the real panel is strictly
    # black/white, 1:1 pixels, with no antialiasing, so small text will look
    # a bit crisper here than it will in person.
    img = img.resize((WIDTH * 2, HEIGHT * 2), Image.LANCZOS)
    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, path)
    img.save(out_path)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    render(path="normal.png")
    render(stale=True, path="stale_warning.png")
    render(quiet_hours=True, path="quiet_hours.png")
