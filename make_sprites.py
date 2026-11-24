#!/usr/bin/env python3
"""
make_sprites.py - Convert a 6-expression Paca sprite sheet (PNG or JPEG) into sprites.h

Designed for Paca.png (pixel-art tileset with 6 expressions left-to-right):
  0 WAVE    - greet / open eyes
  1 CONTENT - closed happy eyes, small smile
  2 STARS   - arched happy eyes, open mouth + heart
  3 SLEEPY  - drowsy yawn + zZ
  4 WORRIED - worried eyes, frown + sweat drop
  5 NEUTRAL - alert eyes + exclamation mark

Features:
  - Preserves native PNG alpha transparency automatically (no color erosion).
  - Falls back to border BFS flood-fill + 1px halo cleanup for opaque PNG/JPEG sheets.
  - Supports both automatic 6-silhouette detection (merging detached icons like hearts,
    zZ, sweat drops, and '!') and strict equal 6-column grid slicing (--mode grid).
  - Aligns all 6 frames to a shared bottom baseline so Paca never jumps vertically.
  - Defaults to nearest-neighbor scaling (--resample nearest) for crisp pixel art.

Usage:
  pip install pillow
  python3 make_sprites.py --input Paca.png --output sprites.h
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
from PIL import Image

SPRITE_W = 64
SPRITE_H = 80
SPRITE_COUNT = 6
TRANSPARENT_KEY = 0xF81F  # Magenta key color (RGB565)

EXPR_NAMES = ["WAVE", "CONTENT", "STARS", "SLEEPY", "WORRIED", "NEUTRAL"]


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    """Convert 8-bit RGB to 16-bit RGB565, avoiding accidental TRANSPARENT_KEY collisions."""
    val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    if val == TRANSPARENT_KEY:
        val = 0xF81E  # Nudge 1 LSB off magenta key
    return val


def has_real_transparency(rgba: Image.Image) -> bool:
    """Return True if the image already contains transparent pixels in its alpha channel."""
    alpha_extrema = rgba.getchannel("A").getextrema()
    return alpha_extrema is not None and alpha_extrema[0] < 250


def remove_border_background(rgba: Image.Image, tol: int = 45) -> Image.Image:
    """
    Flood-fill remove solid/near-solid background starting from image borders.
    Used only when the input image has no existing alpha transparency (e.g., JPEG).
    Preserves interior white pixels such as Paca's face, eye highlights, and teeth.
    """
    rgba = rgba.copy()
    w, h = rgba.size
    px = rgba.load()

    # Sample top-left corner as background color reference
    bg_r, bg_g, bg_b, _ = px[0, 0]

    def is_bg_color(x: int, y: int, tolerance: int) -> bool:
        r, g, b, a = px[x, y]
        if a < 128:
            return True
        return (
            abs(r - bg_r) <= tolerance
            and abs(g - bg_g) <= tolerance
            and abs(b - bg_b) <= tolerance
        )

    visited = [[False] * w for _ in range(h)]
    q: deque[tuple[int, int]] = deque()

    def try_seed(x: int, y: int) -> None:
        if not visited[y][x] and is_bg_color(x, y, tol):
            visited[y][x] = True
            q.append((x, y))

    for x in range(w):
        try_seed(x, 0)
        try_seed(x, h - 1)
    for y in range(h):
        try_seed(0, y)
        try_seed(w - 1, y)

    while q:
        x, y = q.popleft()
        px[x, y] = (0, 0, 0, 0)
        if x > 0:
            try_seed(x - 1, y)
        if x + 1 < w:
            try_seed(x + 1, y)
        if y > 0:
            try_seed(x, y - 1)
        if y + 1 < h:
            try_seed(x, y + 1)

    # 1px fringe cleanup: strip pale JPEG halo pixels adjacent to transparency
    fringe_tol = tol + 35
    to_clear: list[tuple[int, int]] = []
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            if (
                px[x - 1, y][3] == 0
                or px[x + 1, y][3] == 0
                or px[x, y - 1][3] == 0
                or px[x, y + 1][3] == 0
            ):
                if (
                    abs(r - bg_r) <= fringe_tol
                    and abs(g - bg_g) <= fringe_tol
                    and abs(b - bg_b) <= fringe_tol
                ):
                    to_clear.append((x, y))
    for x, y in to_clear:
        px[x, y] = (0, 0, 0, 0)

    return rgba


def find_sprite_columns(rgba: Image.Image, mode: str = "auto") -> list[tuple[int, int]]:
    """
    Locate 6 horizontal sprite spans.
      - mode='grid': equal 6-column tileset slicing.
      - mode='auto': X-projection of opaque pixels with small-gap merging (keeps
        detached hearts, zZ, sweat drops, and '!' grouped with their alpaca),
        falling back to equal 6-column slicing if span count != 6.
    """
    w, h = rgba.size
    if mode == "grid":
        frame_w = w // SPRITE_COUNT
        return [
            (i * frame_w, (i + 1) * frame_w if i < SPRITE_COUNT - 1 else w)
            for i in range(SPRITE_COUNT)
        ]

    alpha = rgba.getchannel("A").load()
    col_has_fg = [False] * w
    for x in range(w):
        for y in range(h):
            if alpha[x, y] >= 128:
                col_has_fg[x] = True
                break

    raw_spans: list[tuple[int, int]] = []
    in_span = False
    start = 0
    for x, fg in enumerate(col_has_fg):
        if fg and not in_span:
            start = x
            in_span = True
        elif not fg and in_span:
            raw_spans.append((start, x))
            in_span = False
    if in_span:
        raw_spans.append((start, w))

    # Merge tiny gaps so floating icons stay attached without merging neighbors
    merge_gap = max(2, w // 48)
    merged: list[tuple[int, int]] = []
    for x0, x1 in raw_spans:
        if merged and (x0 - merged[-1][1]) <= merge_gap:
            merged[-1] = (merged[-1][0], x1)
        else:
            merged.append((x0, x1))

    spans = [(x0, x1) for (x0, x1) in merged if (x1 - x0) >= 4]
    if len(spans) == SPRITE_COUNT:
        print(f"[INFO] Auto-detected {SPRITE_COUNT} sprite spans: {spans}")
        return spans

    print(
        f"[INFO] Found {len(spans)} spans; using equal {SPRITE_COUNT}-column grid slicing."
    )
    frame_w = w // SPRITE_COUNT
    return [
        (i * frame_w, (i + 1) * frame_w if i < SPRITE_COUNT - 1 else w)
        for i in range(SPRITE_COUNT)
    ]


def process_sheet(
    input_path: str,
    bg_tol: int = 45,
    resample_mode: str = "nearest",
    slice_mode: str = "auto",
) -> list[list[int]]:
    """Load tileset, slice 6 expressions, baseline-align, scale to 64x80, and encode RGB565."""
    img = Image.open(input_path).convert("RGBA")

    if has_real_transparency(img):
        print("[INFO] Native PNG transparency detected; preserving alpha channel.")
    else:
        print(f"[INFO] Opaque background detected; running border flood-fill (tol={bg_tol}).")
        img = remove_border_background(img, tol=bg_tol)

    spans = find_sprite_columns(img, mode=slice_mode)
    resample = (
        Image.Resampling.NEAREST
        if resample_mode == "nearest"
        else Image.Resampling.LANCZOS
    )

    # First pass: determine shared vertical bounds and widest sprite so all 6
    # expressions keep identical scale and foot baseline without vertical jitter.
    crops: list[Image.Image] = []
    min_y = img.height
    max_y = 0
    max_w = 1

    for x0, x1 in spans:
        col_slice = img.crop((x0, 0, x1, img.height))
        bbox = col_slice.getbbox()
        if bbox is None:
            crops.append(col_slice)
            continue
        bx0, by0, bx1, by1 = bbox
        min_y = min(min_y, by0)
        max_y = max(max_y, by1)
        max_w = max(max_w, bx1 - bx0)
        crops.append(col_slice.crop((bx0, 0, bx1, img.height)))

    if max_y <= min_y:
        min_y, max_y = 0, img.height

    shared_h = max(1, max_y - min_y)
    # Fit inside 64x80 with 2px breathing room
    scale = min((SPRITE_W - 2) / max_w, (SPRITE_H - 2) / shared_h)

    encoded_frames: list[list[int]] = []
    for crop in crops:
        trimmed = crop.crop((0, min_y, crop.width, max_y))
        new_w = max(1, int(round(trimmed.width * scale)))
        new_h = max(1, int(round(trimmed.height * scale)))
        resized = trimmed.resize((new_w, new_h), resample)

        # Anchor bottom-center on 64x80 canvas
        canvas = Image.new("RGBA", (SPRITE_W, SPRITE_H), (0, 0, 0, 0))
        paste_x = (SPRITE_W - new_w) // 2
        paste_y = SPRITE_H - new_h
        canvas.paste(resized, (paste_x, paste_y), resized)

        px = canvas.load()
        pixels_565: list[int] = []
        for y in range(SPRITE_H):
            for x in range(SPRITE_W):
                r, g, b, a = px[x, y]
                if a < 128:
                    pixels_565.append(TRANSPARENT_KEY)
                else:
                    pixels_565.append(rgb888_to_rgb565(r, g, b))
        encoded_frames.append(pixels_565)

    return encoded_frames


def write_header(frames: list[list[int]], output_path: str) -> None:
    """Emit a complete, drop-in sprites.h compatible with paca.ino."""
    lines: list[str] = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "#include <M5Unified.h>",
        "",
        "// Auto-generated by make_sprites.py (64x80 RGB565, 6 Expressions)",
        f"static constexpr int SPRITE_W = {SPRITE_W};",
        f"static constexpr int SPRITE_H = {SPRITE_H};",
        f"static constexpr int SPRITE_COUNT = {SPRITE_COUNT};",
        f"static constexpr uint16_t TRANSPARENT_KEY = 0x{TRANSPARENT_KEY:04X};",
        "",
        "enum PacaExpression : uint8_t {",
        "  EXPR_WAVE    = 0,",
        "  EXPR_CONTENT = 1,",
        "  EXPR_STARS   = 2,",
        "  EXPR_SLEEPY  = 3,",
        "  EXPR_WORRIED = 4,",
        "  EXPR_NEUTRAL = 5",
        "};",
        "",
        'static const char* EXPR_NAMES[SPRITE_COUNT] = {',
        '  "WAVE", "CONTENT", "STARS", "SLEEPY", "WORRIED", "NEUTRAL"',
        "};",
        "",
        "static inline void initSprites() {",
        "  // No-op: sprites are stored statically in PROGMEM",
        "}",
        "",
        "static inline void pushSpriteScaledTransparent(",
        "    M5Canvas &dst,",
        "    const uint16_t *src,",
        "    int srcW,",
        "    int srcH,",
        "    int dstX,",
        "    int dstY,",
        "    float scale,",
        "    uint16_t keyColor = TRANSPARENT_KEY) {",
        "  int outW = (int)(srcW * scale);",
        "  int outH = (int)(srcH * scale);",
        "  if (outW <= 0 || outH <= 0) return;",
        "",
        "  for (int y = 0; y < outH; y++) {",
        "    int sy = (y * srcH) / outH;",
        "    if (sy >= srcH) sy = srcH - 1;",
        "    const uint16_t *row = src + sy * srcW;",
        "    for (int x = 0; x < outW; x++) {",
        "      int sx = (x * srcW) / outW;",
        "      if (sx >= srcW) sx = srcW - 1;",
        "      uint16_t c = pgm_read_word(&row[sx]);",
        "      if (c != keyColor) {",
        "        dst.drawPixel(dstX + x, dstY + y, c);",
        "      }",
        "    }",
        "  }",
        "}",
        "",
    ]

    for idx, pixels in enumerate(frames):
        name = EXPR_NAMES[idx]
        lines.append(
            f"static const uint16_t PACA_SPRITE_{name}[SPRITE_W * SPRITE_H] PROGMEM = {{"
        )
        for row_start in range(0, len(pixels), 12):
            chunk = pixels[row_start : row_start + 12]
            row_str = ", ".join(f"0x{c:04X}" for c in chunk)
            lines.append(f"  {row_str},")
        lines.append("};")
        lines.append("")

    lines.append("static const uint16_t* const PACA_SPRITES[SPRITE_COUNT] = {")
    for idx in range(SPRITE_COUNT):
        comma = "," if idx < SPRITE_COUNT - 1 else ""
        lines.append(f"  PACA_SPRITE_{EXPR_NAMES[idx]}{comma}")
    lines.append("};")
    lines.append("")

    Path(output_path).write_text("\n".join(lines), encoding="utf-8")
    print(f"[OK] Wrote {output_path} ({SPRITE_COUNT} frames @ {SPRITE_W}x{SPRITE_H} RGB565)")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert a 6-expression Paca tileset into sprites.h (RGB565)."
    )
    parser.add_argument(
        "-i", "--input", default="Paca.png", help="Input sprite sheet (default: Paca.png)"
    )
    parser.add_argument(
        "-o", "--output", default="sprites.h", help="Output C header (default: sprites.h)"
    )
    parser.add_argument(
        "--bg-tol",
        type=int,
        default=45,
        help="RGB tolerance for opaque-sheet border flood-fill (default: 45)",
    )
    parser.add_argument(
        "--resample",
        choices=["nearest", "lanczos"],
        default="nearest",
        help="Resize filter: nearest for pixel art (default), lanczos for smooth art",
    )
    parser.add_argument(
        "--mode",
        choices=["auto", "grid"],
        default="auto",
        help="Slicing mode: auto span detection (default) or equal 6-column grid",
    )
    args = parser.parse_args()

    frames = process_sheet(
        args.input,
        bg_tol=args.bg_tol,
        resample_mode=args.resample,
        slice_mode=args.mode,
    )
    write_header(frames, args.output)


if __name__ == "__main__":
    main()