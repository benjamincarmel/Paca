#pragma once

#include <Arduino.h>
#include <M5Unified.h>

// -----------------------------------------------------------------------------
// Paca Sprite Definitions (64x80 RGB565, 6 Expressions)
// Run `python3 make_sprites.py -i Paca.png -o sprites.h` to overwrite this
// file with the exact pixel data from your Paca.png tileset.
// -----------------------------------------------------------------------------
static constexpr int SPRITE_W = 64;
static constexpr int SPRITE_H = 80;
static constexpr int SPRITE_COUNT = 6;
static constexpr uint16_t TRANSPARENT_KEY = 0xF81F; // Magenta key color

enum PacaExpression : uint8_t {
  EXPR_WAVE    = 0, // alert smile / greet
  EXPR_CONTENT = 1, // closed happy eyes, small smile
  EXPR_STARS   = 2, // happy arched eyes, open mouth + pink heart
  EXPR_SLEEPY  = 3, // drowsy yawn + floating zZ
  EXPR_WORRIED = 4, // wide worried eyes, frown + sweat drop
  EXPR_NEUTRAL = 5  // surprised/alert wide eyes + exclamation mark
};

static const char* EXPR_NAMES[SPRITE_COUNT] = {
  "WAVE", "CONTENT", "STARS", "SLEEPY", "WORRIED", "NEUTRAL"
};

// -----------------------------------------------------------------------------
// Transparent Scaled Sprite Blitter (skips TRANSPARENT_KEY)
// Nearest-neighbor scaling keeps Paca.png pixel art crisp across all stages:
// Adult (0.90x / 1.0x), Child (0.72x), and Baby (0.52x)
// -----------------------------------------------------------------------------
static inline void pushSpriteScaledTransparent(
    M5Canvas &dst,
    const uint16_t *src,
    int srcW,
    int srcH,
    int dstX,
    int dstY,
    float scale,
    uint16_t keyColor = TRANSPARENT_KEY) {
  int outW = (int)(srcW * scale);
  int outH = (int)(srcH * scale);
  if (outW <= 0 || outH <= 0) return;

  for (int y = 0; y < outH; y++) {
    int sy = (y * srcH) / outH;
    if (sy >= srcH) sy = srcH - 1;
    const uint16_t *row = src + sy * srcW;
    for (int x = 0; x < outW; x++) {
      int sx = (x * srcW) / outW;
      if (sx >= srcW) sx = srcW - 1;
      uint16_t c = pgm_read_word(&row[sx]);
      if (c != keyColor) {
        dst.drawPixel(dstX + x, dstY + y, c);
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Built-in Pixel-Art Starter Sprites matching Paca.png (replaced by make_sprites.py)
// -----------------------------------------------------------------------------
static uint16_t PACA_SPRITES[SPRITE_COUNT][SPRITE_W * SPRITE_H];

static inline void sprFillRect(uint16_t *buf, int x, int y, int w, int h, uint16_t col) {
  for (int yy = y; yy < y + h; yy++) {
    if (yy < 0 || yy >= SPRITE_H) continue;
    for (int xx = x; xx < x + w; xx++) {
      if (xx < 0 || xx >= SPRITE_W) continue;
      buf[yy * SPRITE_W + xx] = col;
    }
  }
}

static inline void initSprites() {
  const uint16_t OUTLINE    = 0x2847; // Dark plum pixel outline
  const uint16_t WOOL_MAIN  = 0x8299; // Vibrant purple alpaca wool
  const uint16_t WOOL_LIGHT = 0xAC1E; // Soft lavender wool highlight
  const uint16_t WOOL_DARK  = 0x6194; // Deep purple wool shadow
  const uint16_t FACE_CREAM = 0xF77F; // Pale lavender-white face
  const uint16_t SCARF_PINK = 0xEC36; // Cozy pink scarf
  const uint16_t SCARF_SHAD = 0xB24F; // Scarf shadow
  const uint16_t EYE_DARK   = 0x1823; // Crisp dark pixel eyes
  const uint16_t HEART_PINK = 0xE3B4; // Floating pink heart
  const uint16_t SLEEP_LIL  = 0xA49C; // Floating zZ lilac
  const uint16_t SWEAT_CYAN = 0x66DF; // Light blue sweat drop
  const uint16_t ALERT_GOLD = 0xF563; // Exclamation mark gold
  const uint16_t MOUTH_ROSE = 0xD2D1; // Open happy mouth

  for (int f = 0; f < SPRITE_COUNT; f++) {
    uint16_t *b = PACA_SPRITES[f];
    for (int i = 0; i < SPRITE_W * SPRITE_H; i++) {
      b[i] = TRANSPARENT_KEY;
    }

    int earTop = (f == EXPR_NEUTRAL) ? 6 : 10;
    int earH   = (f == EXPR_NEUTRAL) ? 14 : 10;

    // Pixel ears (left & right)
    sprFillRect(b, 14, earTop, 8, earH, OUTLINE);
    sprFillRect(b, 36, earTop, 8, earH, OUTLINE);
    sprFillRect(b, 16, earTop + 2, 4, earH - 2, WOOL_LIGHT);
    sprFillRect(b, 38, earTop + 2, 4, earH - 2, WOOL_LIGHT);

    // Head block with rounded pixel corners
    sprFillRect(b, 10, 18, 38, 28, OUTLINE);
    sprFillRect(b, 8,  22, 42, 20, OUTLINE);
    sprFillRect(b, 12, 20, 34, 24, WOOL_MAIN);
    sprFillRect(b, 10, 24, 38, 16, WOOL_MAIN);
    sprFillRect(b, 14, 20, 30, 6,  WOOL_LIGHT);

    // Pale scalloped face
    sprFillRect(b, 14, 26, 30, 14, FACE_CREAM);
    sprFillRect(b, 12, 28, 34, 10, FACE_CREAM);
    // Little wool bangs dipping onto forehead
    sprFillRect(b, 20, 26, 4, 2, WOOL_LIGHT);
    sprFillRect(b, 28, 26, 4, 2, WOOL_LIGHT);
    sprFillRect(b, 36, 26, 4, 2, WOOL_LIGHT);

    // Pink scarf around neck
    sprFillRect(b, 12, 46, 34, 8, OUTLINE);
    sprFillRect(b, 14, 48, 30, 4, SCARF_PINK);
    sprFillRect(b, 14, 51, 30, 2, SCARF_SHAD);

    // Body + stubby tail sticking out to the right
    sprFillRect(b, 12, 54, 34, 18, OUTLINE);
    sprFillRect(b, 14, 54, 30, 16, WOOL_MAIN);
    sprFillRect(b, 16, 56, 24, 10, WOOL_LIGHT);
    sprFillRect(b, 44, 58, 10, 8,  OUTLINE);
    sprFillRect(b, 44, 60, 8,  4,  WOOL_MAIN);

    // Scarf hanging tail on front-left chest
    sprFillRect(b, 18, 52, 6, 10, SCARF_PINK);
    sprFillRect(b, 18, 60, 6, 2,  SCARF_SHAD);

    // Three little pixel legs at bottom
    sprFillRect(b, 14, 70, 8,  8, OUTLINE);
    sprFillRect(b, 25, 70, 8,  8, OUTLINE);
    sprFillRect(b, 36, 70, 8,  8, OUTLINE);
    sprFillRect(b, 16, 70, 4,  6, WOOL_DARK);
    sprFillRect(b, 27, 70, 4,  6, WOOL_DARK);
    sprFillRect(b, 38, 70, 4,  6, WOOL_DARK);

    // Expression-specific face details & side icons
    switch (f) {
      case EXPR_WAVE: // Cute open eyes, small smile
        sprFillRect(b, 18, 32, 4, 4, EYE_DARK);
        sprFillRect(b, 36, 32, 4, 4, EYE_DARK);
        sprFillRect(b, 26, 34, 6, 2, EYE_DARK);
        sprFillRect(b, 24, 36, 2, 2, EYE_DARK);
        sprFillRect(b, 32, 36, 2, 2, EYE_DARK);
        break;

      case EXPR_CONTENT: // Closed horizontal eyes (- -), small smile
        sprFillRect(b, 17, 34, 6, 2, EYE_DARK);
        sprFillRect(b, 35, 34, 6, 2, EYE_DARK);
        sprFillRect(b, 26, 34, 6, 2, EYE_DARK);
        sprFillRect(b, 24, 36, 2, 2, EYE_DARK);
        sprFillRect(b, 32, 36, 2, 2, EYE_DARK);
        break;

      case EXPR_STARS: // Happy arched eyes (^ ^), open rose mouth + pink heart
        sprFillRect(b, 18, 32, 4, 2, EYE_DARK);
        sprFillRect(b, 16, 34, 2, 2, EYE_DARK);
        sprFillRect(b, 22, 34, 2, 2, EYE_DARK);
        sprFillRect(b, 36, 32, 4, 2, EYE_DARK);
        sprFillRect(b, 34, 34, 2, 2, EYE_DARK);
        sprFillRect(b, 40, 34, 2, 2, EYE_DARK);
        sprFillRect(b, 26, 34, 6, 5, EYE_DARK);
        sprFillRect(b, 27, 35, 4, 3, MOUTH_ROSE);
        // Floating pink heart top-right
        sprFillRect(b, 51, 14, 3, 3, HEART_PINK);
        sprFillRect(b, 55, 14, 3, 3, HEART_PINK);
        sprFillRect(b, 52, 17, 5, 2, HEART_PINK);
        sprFillRect(b, 53, 19, 3, 2, HEART_PINK);
        break;

      case EXPR_SLEEPY: // Round drowsy eyes, 'o' yawn + floating zZ
        sprFillRect(b, 18, 32, 4, 4, EYE_DARK);
        sprFillRect(b, 36, 32, 4, 4, EYE_DARK);
        sprFillRect(b, 27, 35, 4, 4, EYE_DARK);
        // Small 'z' and big 'Z'
        sprFillRect(b, 49, 20, 4, 2, SLEEP_LIL);
        sprFillRect(b, 50, 22, 2, 2, SLEEP_LIL);
        sprFillRect(b, 49, 24, 4, 2, SLEEP_LIL);
        sprFillRect(b, 54, 12, 6, 2, SLEEP_LIL);
        sprFillRect(b, 56, 14, 2, 2, SLEEP_LIL);
        sprFillRect(b, 54, 16, 6, 2, SLEEP_LIL);
        break;

      case EXPR_WORRIED: // Wide worried eyes, frown + cyan sweat drop
        sprFillRect(b, 17, 31, 4, 5, EYE_DARK);
        sprFillRect(b, 37, 31, 4, 5, EYE_DARK);
        sprFillRect(b, 26, 36, 6, 2, EYE_DARK);
        sprFillRect(b, 24, 38, 2, 2, EYE_DARK);
        sprFillRect(b, 32, 38, 2, 2, EYE_DARK);
        // Cyan sweat drop
        sprFillRect(b, 53, 20, 2, 2, SWEAT_CYAN);
        sprFillRect(b, 51, 22, 6, 4, SWEAT_CYAN);
        sprFillRect(b, 52, 26, 4, 2, SWEAT_CYAN);
        break;

      case EXPR_NEUTRAL: // Surprised wide dot eyes, small 'o' mouth + gold '!'
      default:
        sprFillRect(b, 19, 32, 3, 3, EYE_DARK);
        sprFillRect(b, 36, 32, 3, 3, EYE_DARK);
        sprFillRect(b, 27, 35, 4, 4, EYE_DARK);
        // Gold exclamation mark
        sprFillRect(b, 54, 10, 4, 12, ALERT_GOLD);
        sprFillRect(b, 54, 24, 4, 4,  ALERT_GOLD);
        break;
    }
  }
}