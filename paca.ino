/*
 * Paca - A cozy purple alpaca virtual pet for M5Stack StickS3
 *
 * Paca, a remix of StickPet by deflockohio, https://github.com/msm187/StickPet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// M5Unified MUST be included before M5PM1 to avoid i2c_config_t conflicts on ESP-IDF >= 5.3
#include <M5Unified.h>
#include <M5PM1.h>
#include <Preferences.h>
#include <math.h>
#include "sprites.h"

// -----------------------------------------------------------------------------
// Display & Layout Constants (240x135 Landscape)
// -----------------------------------------------------------------------------
static constexpr int SCREEN_W        = 240;
static constexpr int SCREEN_H        = 135;
static constexpr int HEADER_H        = 16;   // y = 0..16
static constexpr int PLAYFIELD_BOT   = 101;  // y = 16..101 (85px tall)
static constexpr int FLOOR_Y         = 96;   // Ground line for Paca & poops
static constexpr int STATBAR_Y       = 101;  // y = 101..116 (15px tall)
static constexpr int ACTIONBAR_Y     = 116;  // y = 116..135 (19px tall)

static constexpr uint32_t FRAME_MS         = 45;    // ~22 FPS target
static constexpr uint32_t SAVE_INTERVAL_MS = 30000; // NVS auto-save interval

M5Canvas canvas(&M5.Display);
M5PM1 pm1;
Preferences prefs;
bool pm1Ready = false;

// -----------------------------------------------------------------------------
// Palette (RGB565) - Lavender, Purple, Cream, Grass-Green, Coral-Red
// -----------------------------------------------------------------------------
static constexpr uint16_t COL_SKY_TOP      = 0x28D7; // Deep twilight purple
static constexpr uint16_t COL_SKY_BOT      = 0x49B0; // Soft dusk violet
static constexpr uint16_t COL_HEADER_BG    = 0x20B5; // Dark plum
static constexpr uint16_t COL_PANEL_BG     = 0x3138; // Muted purple card
static constexpr uint16_t COL_LAVENDER     = 0xC519; // Lavender highlight
static constexpr uint16_t COL_LAVENDER_DK  = 0x7A91; // Deep lavender border
static constexpr uint16_t COL_CREAM        = 0xFFDC; // Warm cream
static constexpr uint16_t COL_CREAM_DIM    = 0xCE59; // Soft cream-gray
static constexpr uint16_t COL_MTN_FAR      = 0x5A32; // Distant purple mountain
static constexpr uint16_t COL_MTN_NEAR     = 0x41B0; // Closer violet ridge
static constexpr uint16_t COL_GRASS_LIGHT  = 0x5EC9; // Meadow grass green
static constexpr uint16_t COL_GRASS_DARK   = 0x3D46; // Shaded grass green
static constexpr uint16_t COL_BAR_GOOD     = 0x4EE8; // Grass-green healthy bar
static constexpr uint16_t COL_BAR_MID      = 0xFE48; // Warm amber mid bar
static constexpr uint16_t COL_BAR_CRIT     = 0xF9CE; // Coral-red critical bar
static constexpr uint16_t COL_BAR_TRACK    = 0x18B3; // Dark bar background
static constexpr uint16_t COL_EGG_SHELL    = 0xF79D; // Cream-lavender egg
static constexpr uint16_t COL_EGG_SPOT     = 0x9357; // Purple speckles
static constexpr uint16_t COL_POOP         = 0x61C4; // Earthy brown
static constexpr uint16_t COL_HAY          = 0xFEA6; // Golden hay
static constexpr uint16_t COL_HEART        = 0xFA71; // Soft rose heart

// -----------------------------------------------------------------------------
// Life Stages, Actions & Pet State
// -----------------------------------------------------------------------------
enum LifeStage : uint8_t {
  STAGE_EGG   = 0,
  STAGE_BABY  = 1,
  STAGE_CHILD = 2,
  STAGE_ADULT = 3
};

enum ActionId : uint8_t {
  ACT_FEED  = 0,
  ACT_PLAY  = 1,
  ACT_REST  = 2,
  ACT_CLEAN = 3,
  ACT_HEAL  = 4,
  ACT_COUNT = 5
};

static const char* ACTION_LABELS[ACT_COUNT] = {
  "FEED", "PLAY", "REST", "CLEAN", "HEAL"
};

static const char* STAGE_LABELS[4] = {
  "EGG", "BABY", "CHILD", "ADULT"
};

// Stage progression thresholds (in active pet seconds)
static constexpr uint32_t EGG_HATCH_SEC      = 45;      // 45s -> Baby
static constexpr uint32_t BABY_TO_CHILD_SEC  = 15 * 60; // 15m -> Child
static constexpr uint32_t CHILD_TO_ADULT_SEC = 60 * 60; // 60m -> Adult

struct PetState {
  char name[12];
  uint8_t stage;
  float hunger;        // 0..100 (FOOD)
  float happiness;     // 0..100 (FUN)
  float energy;        // 0..100 (REST)
  float health;        // 0..100 (vitality)
  uint8_t poopCount;   // 0..4
  bool isSick;
  bool isSleeping;
  bool isDead;
  bool muted;
  uint32_t ageSeconds;
  uint32_t poopTimerSec;
};

PetState pet;

// UI & Loop Timing State
uint8_t selectedAction = ACT_FEED;
uint32_t lastFrameMs   = 0;
uint32_t lastSecTickMs = 0;
uint32_t lastSaveMs    = 0;

// Expression & Animation Timers
uint16_t waveTimerMs    = 0;    // Hatch / evolution / wake wave
uint16_t effectTimerMs  = 0;    // Action effect window (STARS for FEED/PLAY)
uint8_t lastActionDone  = ACT_FEED;
uint16_t blinkTimerMs   = 0;
uint16_t nextBlinkInMs  = 3200;
bool isBlinking         = false;

// Serial expression demo override (-1 = normal mood logic, 0..5 = pinned, 99 = auto-cycle)
int8_t forcedExpr         = -1;
uint8_t exprCycleIdx      = 0;
uint16_t exprCycleTimerMs = 0;

// Non-blocking Serial Line Buffer
char serialBuf[64];
uint8_t serialLen = 0;

// Button & Power Guard State
bool btnBHoldHandled     = false;
uint32_t deadHoldStartMs = 0;
bool quietChargeMode     = false;

// Particle System (hay, hearts, wool puffs, sleep Z's, sparkles)
struct Particle {
  float x, y;
  float vx, vy;
  uint16_t lifeMs;
  uint8_t kind; // 0=hay, 1=heart, 2=wool, 3=Z, 4=sparkle
};
static constexpr int MAX_PARTICLES = 16;
Particle particles[MAX_PARTICLES];

// Battery Telemetry Cache
int batteryPct         = 100;
bool batteryCharging   = false;
uint32_t lastBattPollMs = 0;

// -----------------------------------------------------------------------------
// Non-Blocking Tone Sequencer (Never stalls the 45ms frame loop)
// -----------------------------------------------------------------------------
struct ToneNote {
  uint16_t freq;
  uint16_t durMs;
  uint16_t stepMs; // Total time before next note starts
};
static constexpr int MAX_TONE_QUEUE = 8;
ToneNote toneQueue[MAX_TONE_QUEUE];
uint8_t toneHead = 0;
uint8_t toneTail = 0;
uint32_t nextToneMs = 0;

void clearToneQueue() {
  toneHead = toneTail = 0;
  nextToneMs = 0;
}

void enqueueTone(uint16_t freq, uint16_t durMs, uint16_t stepMs) {
  if (pet.muted || quietChargeMode) return;
  uint8_t nextTail = (toneTail + 1) % MAX_TONE_QUEUE;
  if (nextTail == toneHead) return; // Queue full
  toneQueue[toneTail] = {freq, durMs, stepMs};
  toneTail = nextTail;
}

void updateAudio(uint32_t now) {
  if (pet.muted || quietChargeMode) {
    clearToneQueue();
    return;
  }
  if (toneHead != toneTail && now >= nextToneMs) {
    ToneNote n = toneQueue[toneHead];
    toneHead = (toneHead + 1) % MAX_TONE_QUEUE;
    M5.Speaker.tone(n.freq, n.durMs);
    nextToneMs = now + n.stepMs;
  }
}

void sfxCursor() {
  clearToneQueue();
  enqueueTone(740, 20, 25);
}

void sfxAction(uint8_t act) {
  clearToneQueue();
  switch (act) {
    case ACT_FEED:
      enqueueTone(523, 45, 55);
      enqueueTone(659, 60, 65);
      break;
    case ACT_PLAY:
      enqueueTone(659, 40, 48);
      enqueueTone(880, 45, 52);
      enqueueTone(1046, 70, 75);
      break;
    case ACT_REST:
      enqueueTone(440, 55, 65);
      enqueueTone(330, 80, 85);
      break;
    case ACT_CLEAN:
      enqueueTone(587, 35, 45);
      enqueueTone(784, 55, 60);
      break;
    case ACT_HEAL:
      enqueueTone(523, 40, 48);
      enqueueTone(659, 40, 48);
      enqueueTone(784, 40, 48);
      enqueueTone(1046, 75, 80);
      break;
  }
}

void sfxEvolve() {
  clearToneQueue();
  enqueueTone(523, 60, 70);
  enqueueTone(659, 60, 70);
  enqueueTone(784, 60, 70);
  enqueueTone(1046, 95, 100);
}

void sfxSad() {
  clearToneQueue();
  enqueueTone(330, 85, 95);
  enqueueTone(247, 130, 135);
}

// -----------------------------------------------------------------------------
// Particle Spawner & Updater
// -----------------------------------------------------------------------------
void spawnParticles(uint8_t kind, int count, float cx, float cy) {
  int spawned = 0;
  for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
    if (particles[i].lifeMs == 0) {
      particles[i].x = cx + random(-18, 19);
      particles[i].y = cy + random(-10, 11);
      particles[i].vx = (random(-12, 13)) / 10.0f;
      particles[i].vy = (kind == 3) ? -0.55f : -((random(8, 18)) / 10.0f);
      particles[i].lifeMs = (kind == 3) ? 1400 : 850;
      particles[i].kind = kind;
      spawned++;
    }
  }
}

void updateParticles(uint32_t dtMs) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].lifeMs > 0) {
      particles[i].x += particles[i].vx;
      particles[i].y += particles[i].vy;
      if (particles[i].kind == 0) {
        particles[i].vy += 0.08f; // Hay flutters down after initial toss
      }
      if (particles[i].lifeMs <= dtMs) {
        particles[i].lifeMs = 0;
      } else {
        particles[i].lifeMs -= dtMs;
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Persistence (Preferences NVS)
// -----------------------------------------------------------------------------
void resetPetToEgg(bool keepMute) {
  bool prevMute = pet.muted;
  memset(&pet, 0, sizeof(PetState));
  strncpy(pet.name, "PACA", sizeof(pet.name) - 1);
  pet.stage        = STAGE_EGG;
  pet.hunger       = 80.0f;
  pet.happiness    = 80.0f;
  pet.energy       = 90.0f;
  pet.health       = 100.0f;
  pet.poopCount    = 0;
  pet.isSick       = false;
  pet.isSleeping   = false;
  pet.isDead       = false;
  pet.muted        = keepMute ? prevMute : false;
  pet.ageSeconds   = 0;
  pet.poopTimerSec = 0;

  waveTimerMs    = 0;
  effectTimerMs  = 0;
  selectedAction = ACT_FEED;
  memset(particles, 0, sizeof(particles));
}

void savePet() {
  prefs.begin("paca", false);
  prefs.putBool("valid", true);
  prefs.putString("name", pet.name);
  prefs.putUChar("stage", pet.stage);
  prefs.putFloat("hunger", pet.hunger);
  prefs.putFloat("happy", pet.happiness);
  prefs.putFloat("energy", pet.energy);
  prefs.putFloat("health", pet.health);
  prefs.putUChar("poop", pet.poopCount);
  prefs.putBool("sick", pet.isSick);
  prefs.putBool("sleep", pet.isSleeping);
  prefs.putBool("dead", pet.isDead);
  prefs.putBool("mute", pet.muted);
  prefs.putULong("age", pet.ageSeconds);
  prefs.putULong("pooptmr", pet.poopTimerSec);
  prefs.end();
}

void loadPet() {
  prefs.begin("paca", true);
  bool valid = prefs.getBool("valid", false);
  if (!valid) {
    prefs.end();
    resetPetToEgg(false);
    savePet();
    return;
  }
  String savedName = prefs.getString("name", "PACA");
  strncpy(pet.name, savedName.c_str(), sizeof(pet.name) - 1);
  pet.name[sizeof(pet.name) - 1] = '\0';
  pet.stage        = prefs.getUChar("stage", STAGE_EGG);
  pet.hunger       = prefs.getFloat("hunger", 80.0f);
  pet.happiness    = prefs.getFloat("happy", 80.0f);
  pet.energy       = prefs.getFloat("energy", 90.0f);
  pet.health       = prefs.getFloat("health", 100.0f);
  pet.poopCount    = prefs.getUChar("poop", 0);
  pet.isSick       = prefs.getBool("sick", false);
  pet.isSleeping   = prefs.getBool("sleep", false);
  pet.isDead       = prefs.getBool("dead", false);
  pet.muted        = prefs.getBool("mute", false);
  pet.ageSeconds   = prefs.getULong("age", 0);
  pet.poopTimerSec = prefs.getULong("pooptmr", 0);
  prefs.end();
}

// -----------------------------------------------------------------------------
// Power & Battery Telemetry (M5PM1 + M5Unified)
// -----------------------------------------------------------------------------
int mvToPercent(uint16_t mv) {
  if (mv >= 4150) return 100;
  if (mv <= 3350) return 0;
  return (int)((mv - 3350) * 100L / (4150 - 3350));
}

void pollBattery() {
  uint16_t vbatMv = 0;
  uint16_t vinMv  = 0;
  uint16_t v5Mv   = 0;

  if (pm1Ready && pm1.readVbat(&vbatMv) == M5PM1_OK && vbatMv > 2500) {
    batteryPct = mvToPercent(vbatMv);
    bool usbVin = (pm1.readVin(&vinMv) == M5PM1_OK && vinMv > 4200);
    bool usb5v  = (pm1.read5VInOut(&v5Mv) == M5PM1_OK && v5Mv > 4200);
    batteryCharging = usbVin || usb5v ||
                      (M5.Power.isCharging() == m5::Power_Class::is_charging);
  } else {
    int lvl = M5.Power.getBatteryLevel();
    if (lvl >= 0 && lvl <= 100) batteryPct = lvl;
    batteryCharging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
  }
}

// -----------------------------------------------------------------------------
// Pet Care Simulation & Actions
// -----------------------------------------------------------------------------
float clampStat(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 100.0f) return 100.0f;
  return v;
}

void checkStageProgression() {
  uint8_t prevStage = pet.stage;
  if (pet.stage == STAGE_EGG && pet.ageSeconds >= EGG_HATCH_SEC) {
    pet.stage = STAGE_BABY;
  } else if (pet.stage == STAGE_BABY && pet.ageSeconds >= BABY_TO_CHILD_SEC) {
    pet.stage = STAGE_CHILD;
  } else if (pet.stage == STAGE_CHILD && pet.ageSeconds >= CHILD_TO_ADULT_SEC) {
    pet.stage = STAGE_ADULT;
  }

  if (pet.stage != prevStage) {
    waveTimerMs = 2800;
    spawnParticles(4, 8, SCREEN_W / 2, 60);
    sfxEvolve();
    savePet();
    Serial.printf("[PACA] Evolved to stage %s!\n", STAGE_LABELS[pet.stage]);
  }
}

void tickPetOneSecond() {
  if (pet.isDead) return;

  pet.ageSeconds++;
  checkStageProgression();

  if (pet.stage == STAGE_EGG) {
    return;
  }

  // Stat decay / recovery per second
  if (pet.isSleeping) {
    pet.energy    = clampStat(pet.energy + 0.45f);
    pet.hunger    = clampStat(pet.hunger - 0.04f);
    pet.happiness = clampStat(pet.happiness - 0.03f);
    if (pet.energy >= 100.0f) {
      pet.isSleeping = false;
      waveTimerMs = 1500;
    }
  } else {
    pet.hunger    = clampStat(pet.hunger - 0.11f);
    pet.happiness = clampStat(pet.happiness - 0.09f);
    pet.energy    = clampStat(pet.energy - 0.07f);
  }

  // Poop accumulation every ~4 minutes while awake
  if (!pet.isSleeping) {
    pet.poopTimerSec++;
    if (pet.poopTimerSec >= 240) {
      pet.poopTimerSec = 0;
      if (pet.poopCount < 4) {
        pet.poopCount++;
        sfxSad();
      }
    }
  }

  // Uncleaned poop causes unhappiness and sickness risk
  if (pet.poopCount > 0) {
    pet.happiness = clampStat(pet.happiness - 0.08f * pet.poopCount);
    if (!pet.isSick && pet.poopCount >= 2 && random(0, 120) == 0) {
      pet.isSick = true;
      sfxSad();
    }
  }

  // Starvation or exhaustion can cause sickness
  if (!pet.isSick && (pet.hunger < 10.0f || pet.energy < 10.0f) && random(0, 90) == 0) {
    pet.isSick = true;
    sfxSad();
  }

  // Health vitality calculation
  float healthDelta = 0.05f;
  if (pet.isSick)          healthDelta -= 0.35f;
  if (pet.hunger <= 5.0f)  healthDelta -= 0.30f;
  if (pet.energy <= 5.0f)  healthDelta -= 0.15f;
  if (pet.poopCount >= 3)  healthDelta -= 0.15f;
  pet.health = clampStat(pet.health + healthDelta);

  if (pet.health <= 0.0f) {
    pet.isDead = true;
    pet.isSleeping = false;
    sfxSad();
    savePet();
    Serial.println("[PACA] Paca has passed away.");
  }
}

void performSelectedAction() {
  if (pet.isDead) return;

  // Tapping action on an egg warms it up and shaves 5s off hatch time
  if (pet.stage == STAGE_EGG) {
    pet.ageSeconds += 5;
    spawnParticles(1, 4, SCREEN_W / 2, 72);
    enqueueTone(660, 40, 45);
    checkStageProgression();
    return;
  }

  lastActionDone = selectedAction;

  switch (selectedAction) {
    case ACT_FEED:
      if (pet.isSleeping) pet.isSleeping = false;
      pet.hunger    = clampStat(pet.hunger + 26.0f);
      pet.happiness = clampStat(pet.happiness + 4.0f);
      effectTimerMs = 1500;
      spawnParticles(0, 7, SCREEN_W / 2, 58); // Golden hay stalks
      sfxAction(ACT_FEED);
      break;

    case ACT_PLAY:
      if (pet.isSleeping) pet.isSleeping = false;
      if (pet.energy < 12.0f || pet.isSick) {
        sfxSad();
        return;
      }
      pet.happiness = clampStat(pet.happiness + 24.0f);
      pet.energy    = clampStat(pet.energy - 12.0f);
      pet.hunger    = clampStat(pet.hunger - 6.0f);
      effectTimerMs = 1500;
      spawnParticles(1, 7, SCREEN_W / 2, 54); // Hearts
      sfxAction(ACT_PLAY);
      break;

    case ACT_REST:
      pet.isSleeping = !pet.isSleeping;
      if (pet.isSleeping) {
        spawnParticles(3, 2, SCREEN_W / 2 + 16, 40);
      } else {
        waveTimerMs = 1000;
      }
      sfxAction(ACT_REST);
      break;

    case ACT_CLEAN:
      if (pet.poopCount > 0) {
        pet.poopCount    = 0;
        pet.poopTimerSec = 0;
        pet.happiness    = clampStat(pet.happiness + 10.0f);
      }
      effectTimerMs = 1200;
      spawnParticles(2, 8, SCREEN_W / 2, 76); // Wool puffs
      sfxAction(ACT_CLEAN);
      break;

    case ACT_HEAL:
      if (pet.isSick) {
        pet.isSick    = false;
        pet.health    = clampStat(pet.health + 30.0f);
        effectTimerMs = 1300;
        spawnParticles(4, 7, SCREEN_W / 2, 56);
        sfxAction(ACT_HEAL);
      } else {
        pet.health = clampStat(pet.health + 8.0f);
        spawnParticles(4, 4, SCREEN_W / 2, 56);
        enqueueTone(659, 40, 45);
      }
      break;
  }

  savePet();
}

// -----------------------------------------------------------------------------
// Rendering Helpers
// -----------------------------------------------------------------------------
void drawMeadowBackground(int topY, int botY) {
  int skyH = botY - topY;
  for (int y = topY; y < botY; y++) {
    uint16_t col = (y < topY + skyH / 2) ? COL_SKY_TOP : COL_SKY_BOT;
    canvas.drawFastHLine(0, y, SCREEN_W, col);
  }

  // Soft purple mountain silhouettes
  canvas.fillTriangle(8, botY - 12, 58, topY + 18, 112, botY - 12, COL_MTN_FAR);
  canvas.fillTriangle(118, botY - 12, 178, topY + 14, 236, botY - 12, COL_MTN_FAR);
  canvas.fillTriangle(62, botY - 12, 120, topY + 26, 178, botY - 12, COL_MTN_NEAR);

  // Rolling meadow grass
  canvas.fillRect(0, botY - 15, SCREEN_W, 15, COL_GRASS_LIGHT);
  canvas.drawFastHLine(0, botY - 15, SCREEN_W, COL_CREAM_DIM);
  for (int x = 0; x < SCREEN_W; x += 24) {
    canvas.fillEllipse(x + 12, botY - 4, 14, 5, COL_GRASS_DARK);
  }
}

void drawTopHeader() {
  canvas.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER_BG);
  canvas.drawFastHLine(0, HEADER_H - 1, SCREEN_W, COL_LAVENDER_DK);

  // Name + Stage pill
  canvas.setTextSize(1);
  canvas.setTextColor(COL_CREAM);
  canvas.setCursor(4, 4);
  canvas.print(pet.name);

  int badgeX = 35;
  canvas.fillRoundRect(badgeX, 2, 36, 11, 3, COL_LAVENDER_DK);
  canvas.setTextColor(COL_CREAM);
  canvas.setCursor(badgeX + 3, 4);
  canvas.print(STAGE_LABELS[pet.stage]);

  // Pet Age (replaces StickPet's gold counter)
  uint32_t totalHours = pet.ageSeconds / 3600;
  uint32_t days  = totalHours / 24;
  uint32_t hours = totalHours % 24;
  char ageBuf[16];
  if (days > 0) {
    snprintf(ageBuf, sizeof(ageBuf), "%lud %luh", (unsigned long)days, (unsigned long)hours);
  } else {
    uint32_t mins = (pet.ageSeconds % 3600) / 60;
    snprintf(ageBuf, sizeof(ageBuf), "0d %luh%02lum", (unsigned long)hours, (unsigned long)mins);
  }
  canvas.setTextColor(COL_LAVENDER);
  canvas.setCursor(78, 4);
  canvas.print(ageBuf);

  // Mute speaker icon
  int spkX = 165;
  canvas.fillRect(spkX, 5, 3, 6, COL_CREAM);
  canvas.fillTriangle(spkX + 3, 5, spkX + 7, 2, spkX + 7, 13, COL_CREAM);
  if (pet.muted || quietChargeMode) {
    canvas.drawLine(spkX + 9, 4, spkX + 14, 11, COL_BAR_CRIT);
    canvas.drawLine(spkX + 14, 4, spkX + 9, 11, COL_BAR_CRIT);
  } else {
    canvas.drawPixel(spkX + 9, 6, COL_BAR_GOOD);
    canvas.drawPixel(spkX + 10, 8, COL_BAR_GOOD);
    canvas.drawPixel(spkX + 9, 10, COL_BAR_GOOD);
  }

  // Battery gauge + percentage
  int bx = 185;
  canvas.drawRect(bx, 3, 21, 9, COL_CREAM);
  canvas.fillRect(bx + 21, 6, 2, 3, COL_CREAM);
  int fillW = (batteryPct * 17) / 100;
  uint16_t battCol = batteryCharging ? COL_BAR_MID :
                     (batteryPct <= 20 ? COL_BAR_CRIT : COL_BAR_GOOD);
  if (fillW > 0) {
    canvas.fillRect(bx + 2, 5, fillW, 5, battCol);
  }
  canvas.setTextColor(COL_CREAM);
  canvas.setCursor(bx + 25, 4);
  canvas.printf("%d%%", batteryPct);
}

void drawEgg(int cx, int cy) {
  canvas.fillEllipse(cx, cy + 19, 14, 4, COL_GRASS_DARK);
  canvas.fillEllipse(cx, cy, 15, 20, COL_EGG_SHELL);
  canvas.drawEllipse(cx, cy, 15, 20, COL_LAVENDER_DK);
  // Purple speckles
  canvas.fillCircle(cx - 6, cy - 5, 3, COL_EGG_SPOT);
  canvas.fillCircle(cx + 5, cy - 2, 2, COL_EGG_SPOT);
  canvas.fillCircle(cx - 2, cy + 7, 3, COL_EGG_SPOT);
  canvas.fillCircle(cx + 6, cy + 8, 2, COL_EGG_SPOT);
}

void drawPoops(int floorY) {
  const int offsets[4] = {-68, -46, 48, 70};
  for (uint8_t i = 0; i < pet.poopCount && i < 4; i++) {
    int px = SCREEN_W / 2 + offsets[i];
    int py = floorY - 3;
    canvas.fillEllipse(px, py, 7, 3, COL_POOP);
    canvas.fillEllipse(px, py - 3, 5, 3, COL_POOP);
    canvas.fillCircle(px, py - 6, 2, COL_POOP);
  }
}

void drawParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].lifeMs == 0) continue;
    int x = (int)particles[i].x;
    int y = (int)particles[i].y;
    switch (particles[i].kind) {
      case 0: // Golden hay stalks
        canvas.drawLine(x - 3, y - 2, x + 3, y + 2, COL_HAY);
        canvas.drawLine(x - 2, y + 2, x + 2, y - 2, COL_CREAM);
        break;
      case 1: // Hearts
        canvas.fillCircle(x - 2, y, 2, COL_HEART);
        canvas.fillCircle(x + 2, y, 2, COL_HEART);
        canvas.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, COL_HEART);
        break;
      case 2: // Fluffy cream wool puffs
        canvas.fillCircle(x, y, 4, COL_CREAM);
        canvas.fillCircle(x - 3, y + 1, 3, COL_LAVENDER);
        canvas.fillCircle(x + 3, y + 1, 3, COL_CREAM);
        break;
      case 3: // Floating sleep Z (transparent text bg)
        canvas.setTextSize(1);
        canvas.setTextColor(COL_CREAM);
        canvas.setCursor(x, y);
        canvas.print("Z");
        break;
      case 4: // Evolution / heal sparkles
        canvas.drawFastHLine(x - 3, y, 7, COL_CREAM);
        canvas.drawFastVLine(x, y - 3, 7, COL_HAY);
        break;
    }
  }
}

// Mood mapping -> chooses which of the 6 Paca expressions to display
uint8_t resolveCurrentExpression() {
  if (forcedExpr >= 0 && forcedExpr < SPRITE_COUNT) {
    return (uint8_t)forcedExpr;
  }
  if (forcedExpr == 99) {
    return exprCycleIdx % SPRITE_COUNT;
  }

  // Just ate or played (during the effect animation) -> STARS
  if (effectTimerMs > 0 &&
      (lastActionDone == ACT_FEED || lastActionDone == ACT_PLAY)) {
    return EXPR_STARS;
  }
  // Just hatched or evolved -> WAVE
  if (waveTimerMs > 0) {
    return EXPR_WAVE;
  }
  // Sleeping -> SLEEPY
  if (pet.isSleeping) {
    return EXPR_SLEEPY;
  }
  // Sick, hungry, or sad -> WORRIED
  if (pet.isSick || pet.hunger < 30.0f || pet.happiness < 30.0f) {
    return EXPR_WORRIED;
  }
  // Blink -> briefly flash CONTENT
  if (isBlinking) {
    return EXPR_CONTENT;
  }
  // Happy -> CONTENT, OK -> NEUTRAL
  if (pet.happiness >= 70.0f && pet.hunger >= 50.0f && pet.energy >= 40.0f) {
    return EXPR_CONTENT;
  }
  return EXPR_NEUTRAL;
}

void drawCreature(int floorY) {
  float t = millis() / 260.0f;
  float bounce = pet.isSleeping ? sinf(t * 0.5f) * 1.2f : fabsf(sinf(t)) * -4.0f;
  int cx = SCREEN_W / 2;

  if (pet.stage == STAGE_EGG && forcedExpr == -1) {
    float wobble = sinf(millis() / 180.0f) * 2.0f;
    drawEgg(cx + (int)wobble, floorY - 19);
    return;
  }

  // Stage scale: baby = small, child = medium, adult = full playfield height (no header clip)
  float scale = 0.90f; // Adult: 57x72 px -> top at y=20..24 (safely below 16px header)
  if (pet.stage == STAGE_BABY)  scale = 0.52f;
  if (pet.stage == STAGE_CHILD) scale = 0.72f;

  int drawW = (int)(SPRITE_W * scale);
  int drawH = (int)(SPRITE_H * scale);
  int dstX  = cx - drawW / 2;
  int dstY  = floorY - drawH + (int)bounce;

  // Ground shadow under Paca
  canvas.fillEllipse(cx, floorY + 1, drawW / 3, 3, COL_GRASS_DARK);

  uint8_t expr = resolveCurrentExpression();
  pushSpriteScaledTransparent(canvas, PACA_SPRITES[expr], SPRITE_W, SPRITE_H,
                              dstX, dstY, scale, TRANSPARENT_KEY);

  // Sickness indicator badge next to Paca
  if (pet.isSick) {
    int sx = dstX + drawW + 5;
    int sy = dstY + 10;
    canvas.fillCircle(sx, sy, 6, COL_BAR_CRIT);
    canvas.fillRect(sx - 1, sy - 4, 3, 8, COL_CREAM);
    canvas.fillRect(sx - 4, sy - 1, 8, 3, COL_CREAM);
  }
}

void drawStatBar(int x, int y, int w, const char* label, float val) {
  canvas.setTextSize(1);
  canvas.setTextColor(COL_CREAM);
  canvas.setCursor(x, y);
  canvas.print(label);

  int barX = x + 27;
  int barW = w - 29;
  canvas.fillRoundRect(barX, y + 1, barW, 6, 2, COL_BAR_TRACK);

  uint16_t col = COL_BAR_GOOD;
  if (val < 30.0f)      col = COL_BAR_CRIT;
  else if (val < 60.0f) col = COL_BAR_MID;

  int fillW = (int)((barW - 2) * (clampStat(val) / 100.0f));
  if (fillW > 0) {
    canvas.fillRoundRect(barX + 1, y + 2, fillW, 4, 1, col);
  }
}

void drawHudBars() {
  // Stat bar strip (y = 101..116)
  canvas.fillRect(0, STATBAR_Y, SCREEN_W, ACTIONBAR_Y - STATBAR_Y, COL_PANEL_BG);
  canvas.drawFastHLine(0, STATBAR_Y, SCREEN_W, COL_LAVENDER_DK);

  drawStatBar(4,   STATBAR_Y + 4, 76, "FOOD", pet.hunger);
  drawStatBar(82,  STATBAR_Y + 4, 76, "FUN",  pet.happiness);
  drawStatBar(160, STATBAR_Y + 4, 76, "REST", pet.energy);

  // Action bar (y = 116..135)
  canvas.fillRect(0, ACTIONBAR_Y, SCREEN_W, SCREEN_H - ACTIONBAR_Y, COL_HEADER_BG);
  canvas.drawFastHLine(0, ACTIONBAR_Y, SCREEN_W, COL_LAVENDER_DK);

  int slotW = SCREEN_W / ACT_COUNT; // 48px each
  for (uint8_t i = 0; i < ACT_COUNT; i++) {
    int sx = i * slotW + 2;
    int sw = slotW - 4;
    bool sel = (i == selectedAction);
    if (sel) {
      canvas.fillRoundRect(sx, ACTIONBAR_Y + 2, sw, 15, 4, COL_LAVENDER);
      canvas.setTextColor(COL_HEADER_BG);
    } else {
      canvas.setTextColor(COL_CREAM_DIM);
    }
    int textW = strlen(ACTION_LABELS[i]) * 6;
    canvas.setCursor(sx + (sw - textW) / 2, ACTIONBAR_Y + 6);
    canvas.print(ACTION_LABELS[i]);
  }
}

void drawGravestoneScreen() {
  drawMeadowBackground(HEADER_H, SCREEN_H);
  drawTopHeader();

  int cx = SCREEN_W / 2;
  // Gravestone monument
  canvas.fillRoundRect(cx - 26, 34, 52, 54, 14, COL_CREAM_DIM);
  canvas.fillRect(cx - 26, 60, 52, 28, COL_CREAM_DIM);
  canvas.drawRoundRect(cx - 26, 34, 52, 54, 14, COL_LAVENDER_DK);

  canvas.setTextSize(1);
  canvas.setTextColor(COL_HEADER_BG);
  canvas.setCursor(cx - 18, 44);
  canvas.print("R.I.P.");
  canvas.setCursor(cx - (int)(strlen(pet.name) * 3), 56);
  canvas.print(pet.name);

  uint32_t hrs = pet.ageSeconds / 3600;
  char lived[20];
  snprintf(lived, sizeof(lived), "Age %lud %luh", (unsigned long)(hrs / 24), (unsigned long)(hrs % 24));
  canvas.setCursor(cx - (int)(strlen(lived) * 3), 70);
  canvas.print(lived);

  // Centered hatch prompt banner
  canvas.fillRoundRect(20, 98, 200, 26, 5, COL_HEADER_BG);
  canvas.drawRoundRect(20, 98, 200, 26, 5, COL_LAVENDER);
  canvas.setTextColor(COL_CREAM);
  const char* prompt = "Hold button to hatch new egg";
  canvas.setCursor((SCREEN_W - (int)(strlen(prompt) * 6)) / 2, 107);
  canvas.print(prompt);
}

void renderHome() {
  if (pet.isDead) {
    drawGravestoneScreen();
    canvas.pushSprite(0, 0);
    return;
  }

  drawMeadowBackground(HEADER_H, PLAYFIELD_BOT);
  drawTopHeader();
  drawPoops(FLOOR_Y);
  drawCreature(FLOOR_Y);
  drawParticles();
  drawHudBars();
  canvas.pushSprite(0, 0);
}

// -----------------------------------------------------------------------------
// Splash Screen
// -----------------------------------------------------------------------------
void runSplashScreen() {
  uint32_t startMs = millis();
  while (millis() - startMs < 2200) {
    M5.update();
    float t = (millis() - startMs) / 220.0f;
    float bounce = fabsf(sinf(t)) * -7.0f;

    drawMeadowBackground(0, SCREEN_H);

    // Title banner (transparent text background)
    canvas.setTextSize(2);
    canvas.setTextColor(COL_CREAM);
    canvas.setCursor(96, 10);
    canvas.print("PACA");

    canvas.setTextSize(1);
    canvas.setTextColor(COL_LAVENDER);
    canvas.setCursor(72, 29);
    canvas.print("Cozy Alpaca Care");

    // Full-size bouncing waving alpaca in the middle
    int dstX = (SCREEN_W - SPRITE_W) / 2;
    int dstY = 44 + (int)bounce;
    canvas.fillEllipse(SCREEN_W / 2, 122, 22, 4, COL_GRASS_DARK);
    pushSpriteScaledTransparent(canvas, PACA_SPRITES[EXPR_WAVE], SPRITE_W, SPRITE_H,
                                dstX, dstY, 1.0f, TRANSPARENT_KEY);

    canvas.pushSprite(0, 0);
    delay(30);
  }
}

// -----------------------------------------------------------------------------
// Non-Blocking Serial Dev Cheats
// -----------------------------------------------------------------------------
void processSerialCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;

  if (cmd == "help") {
    Serial.println("=== PACA Serial Dev Commands ===");
    Serial.println("status   - print pet stats & battery");
    Serial.println("kill     - set health to 0 (gravestone)");
    Serial.println("sick     - toggle sickness");
    Serial.println("fill     - refill FOOD/FUN/REST/health & clean");
    Serial.println("age      - advance to next life stage (+6h if adult)");
    Serial.println("poop     - spawn a poop pile");
    Serial.println("new egg  - hatch a fresh egg (also: egg)");
    Serial.println("expr     - cycle all 6 expressions (or 'expr 0..5', 'expr off')");
  } else if (cmd == "status") {
    Serial.printf("Name=%s Stage=%s Age=%lus Food=%.1f Fun=%.1f Rest=%.1f Health=%.1f Poop=%u Sick=%d Sleep=%d Dead=%d Batt=%d%% Chg=%d\n",
                  pet.name, STAGE_LABELS[pet.stage], (unsigned long)pet.ageSeconds,
                  pet.hunger, pet.happiness, pet.energy, pet.health,
                  pet.poopCount, pet.isSick, pet.isSleeping, pet.isDead,
                  batteryPct, batteryCharging);
  } else if (cmd == "kill") {
    pet.health     = 0.0f;
    pet.isDead     = true;
    pet.isSleeping = false;
    savePet();
    Serial.println("[CHEAT] Pet killed.");
  } else if (cmd == "sick") {
    pet.isSick = !pet.isSick;
    savePet();
    Serial.printf("[CHEAT] isSick = %d\n", pet.isSick);
  } else if (cmd == "fill") {
    pet.hunger     = 100.0f;
    pet.happiness  = 100.0f;
    pet.energy     = 100.0f;
    pet.health     = 100.0f;
    pet.poopCount  = 0;
    pet.isSick     = false;
    pet.isDead     = false;
    effectTimerMs  = 1500;
    lastActionDone = ACT_PLAY;
    savePet();
    Serial.println("[CHEAT] Stats filled & cleaned.");
  } else if (cmd == "age") {
    if (pet.stage == STAGE_EGG) {
      pet.ageSeconds = EGG_HATCH_SEC;
    } else if (pet.stage == STAGE_BABY) {
      pet.ageSeconds = BABY_TO_CHILD_SEC;
    } else if (pet.stage == STAGE_CHILD) {
      pet.ageSeconds = CHILD_TO_ADULT_SEC;
    } else {
      pet.ageSeconds += 6 * 3600;
    }
    checkStageProgression();
    savePet();
    Serial.printf("[CHEAT] Age now %lus (%s)\n", (unsigned long)pet.ageSeconds, STAGE_LABELS[pet.stage]);
  } else if (cmd == "poop") {
    if (pet.poopCount < 4) pet.poopCount++;
    savePet();
    Serial.printf("[CHEAT] Poop count = %u\n", pet.poopCount);
  } else if (cmd == "new egg" || cmd == "egg") {
    resetPetToEgg(true);
    savePet();
    Serial.println("[CHEAT] Reset to fresh egg.");
  } else if (cmd == "expr") {
    forcedExpr       = 99;
    exprCycleIdx     = 0;
    exprCycleTimerMs = 1300;
    Serial.printf("[CHEAT] Cycling expressions: 0 (%s)\n", EXPR_NAMES[0]);
  } else if (cmd == "expr off") {
    forcedExpr = -1;
    Serial.println("[CHEAT] Expression override cleared.");
  } else if (cmd.startsWith("expr ")) {
    String arg = cmd.substring(5);
    arg.trim();
    if (arg.length() == 1 && arg[0] >= '0' && arg[0] < ('0' + SPRITE_COUNT)) {
      int idx = arg[0] - '0';
      forcedExpr = (int8_t)idx;
      Serial.printf("[CHEAT] Pinned expression %d (%s)\n", idx, EXPR_NAMES[idx]);
    } else {
      Serial.println("[CHEAT] Usage: expr | expr 0..5 | expr off");
    }
  }
}

void pollSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLen > 0) {
        serialBuf[serialLen] = '\0';
        processSerialCommand(String(serialBuf));
        serialLen = 0;
      }
    } else if (serialLen < sizeof(serialBuf) - 1) {
      serialBuf[serialLen++] = c;
    }
  }
}

// -----------------------------------------------------------------------------
// Button Input Handling (2 Buttons: BtnA front, BtnB side)
// -----------------------------------------------------------------------------
void handleButtons() {
  // First press wakes from charge-quietly mode without bleeding into menu actions
  if (quietChargeMode && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())) {
    quietChargeMode = false;
    M5.Display.setBrightness(180);
    if (M5.BtnB.isPressed()) {
      btnBHoldHandled = true;
    }
    return;
  }

  // Dead state: hold either button for 1.0s to hatch a new egg
  if (pet.isDead) {
    if (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
      if (deadHoldStartMs == 0) {
        deadHoldStartMs = millis();
      } else if (millis() - deadHoldStartMs >= 1000) {
        deadHoldStartMs = 0;
        if (M5.BtnB.isPressed()) {
          btnBHoldHandled = true; // Prevent immediate mute toggle / tap on release
        }
        resetPetToEgg(true);
        savePet();
        sfxEvolve();
        Serial.println("[PACA] New egg hatched after gravestone hold!");
      }
    } else {
      deadHoldStartMs = 0;
    }
    return;
  }

  // Front button (BtnA): immediate tap advances action cursor (front hold unused)
  if (M5.BtnA.wasPressed()) {
    selectedAction = (selectedAction + 1) % ACT_COUNT;
    sfxCursor();
  }

  // Side button (BtnB): hold >= 650ms toggles mute, release tap performs action
  if (M5.BtnB.isPressed() && !btnBHoldHandled && M5.BtnB.pressedFor(650)) {
    btnBHoldHandled = true;
    pet.muted = !pet.muted;
    savePet();
    if (!pet.muted) {
      enqueueTone(880, 55, 60);
    } else {
      clearToneQueue();
    }
    Serial.printf("[PACA] Mute = %d\n", pet.muted);
  }

  if (M5.BtnB.wasReleased()) {
    if (!btnBHoldHandled) {
      performSelectedAction();
    }
    btnBHoldHandled = false;
  }
}

// -----------------------------------------------------------------------------
// Arduino Setup & Main Loop
// -----------------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Speaker.setVolume(96);
  Serial.begin(115200);

  // Landscape 240x135
  M5.Display.setRotation(1);
  M5.Display.setBrightness(180);

  // Initialize M5PM1 power management IC on internal I2C
  pm1Ready = (pm1.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K) == M5PM1_OK);
  pollBattery();

  // Charge-quietly guard: dim & mute at boot when plugged into USB with no button held
  M5.update();
  if (batteryCharging && !M5.BtnA.isPressed() && !M5.BtnB.isPressed()) {
    quietChargeMode = true;
    M5.Display.setBrightness(64);
    Serial.println("[PACA] Charge-quietly guard active (press any button to wake audio/brightness).");
  }

  initSprites();
  loadPet();

  canvas.setColorDepth(16);
  canvas.createSprite(SCREEN_W, SCREEN_H);

  runSplashScreen();

  uint32_t now  = millis();
  lastFrameMs   = now;
  lastSecTickMs = now;
  lastSaveMs    = now;
  lastBattPollMs = now;

  Serial.println("[PACA] Ready! Type 'help' for serial dev commands.");
}

void loop() {
  uint32_t now = millis();
  updateAudio(now);

  if (now - lastFrameMs < FRAME_MS) {
    delay(1);
    return;
  }
  uint32_t dtMs = now - lastFrameMs;
  lastFrameMs = now;

  M5.update();
  pollSerialCommands();
  handleButtons();

  // Expression & effect timers
  if (waveTimerMs > 0) {
    waveTimerMs = (waveTimerMs <= dtMs) ? 0 : (waveTimerMs - dtMs);
  }
  if (effectTimerMs > 0) {
    effectTimerMs = (effectTimerMs <= dtMs) ? 0 : (effectTimerMs - dtMs);
  }

  // Blink controller
  if (isBlinking) {
    if (blinkTimerMs <= dtMs) {
      isBlinking = false;
      nextBlinkInMs = random(2400, 4800);
    } else {
      blinkTimerMs -= dtMs;
    }
  } else {
    if (nextBlinkInMs <= dtMs) {
      isBlinking = true;
      blinkTimerMs = 160;
    } else {
      nextBlinkInMs -= dtMs;
    }
  }

  // Serial expression cycle showcase
  if (forcedExpr == 99) {
    if (exprCycleTimerMs <= dtMs) {
      exprCycleIdx++;
      if (exprCycleIdx >= SPRITE_COUNT) {
        forcedExpr = -1;
        Serial.println("[CHEAT] Expression cycle complete.");
      } else {
        exprCycleTimerMs = 1300;
        Serial.printf("[CHEAT] Cycling expressions: %u (%s)\n",
                      exprCycleIdx, EXPR_NAMES[exprCycleIdx]);
      }
    } else {
      exprCycleTimerMs -= dtMs;
    }
  }

  // Periodic sleep Z spawner
  if (!pet.isDead && pet.isSleeping && random(0, 28) == 0) {
    spawnParticles(3, 1, SCREEN_W / 2 + 16, 38);
  }

  updateParticles(dtMs);

  // 1-second pet simulation tick
  if (now - lastSecTickMs >= 1000) {
    lastSecTickMs += 1000;
    tickPetOneSecond();
  }

  // Poll battery every 5 seconds
  if (now - lastBattPollMs >= 5000) {
    lastBattPollMs = now;
    pollBattery();
  }

  // Periodic NVS auto-save every 30 seconds
  if (now - lastSaveMs >= SAVE_INTERVAL_MS) {
    lastSaveMs = now;
    savePet();
  }

  renderHome();
}