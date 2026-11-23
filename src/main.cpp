/*
 * CYD Love Gift  -  ESP32-2432S028R
 * HOME   : pulsing heart, floating hearts, days-together counter, tap for hearts burst
 * NOTES  : sweet messages
 * COUPONS: tap to reveal / redeem (saved)
 * GAME   : catch the falling hearts
 */
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"

// ---------- pins ----------
#define BL_PIN 21
#define LDR_PIN 34
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_CLK 25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TS_MINX 200
#define TS_MAXX 3800
#define TS_MINY 240
#define TS_MAXY 3800

constexpr int AREA_H = 212, STRIP_H = 106, NAV_Y = 212;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t C_ROSE  = rgb(240, 84, 132);
const uint16_t C_DEEP  = rgb(196, 38, 98);
const uint16_t C_BLUSH = rgb(255, 186, 208);
const uint16_t C_LAV   = rgb(206, 182, 255);
const uint16_t C_GOLD  = rgb(255, 196, 60);
const uint16_t C_CREAM = rgb(255, 250, 246);
const uint16_t C_TEXT  = rgb(122, 40, 82);
const uint16_t C_SOFT  = rgb(196, 124, 154);
const uint16_t C_PEACH = rgb(255, 170, 190);

TFT_eSPI tft;
TFT_eSprite spr(&tft);
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;

enum Page { P_HOME, P_NOTES, P_COUPON, P_GAME };
Page page = P_HOME;

uint16_t gradLut[AREA_H];
bool timeOk = false, hasDays = false;
long daysTogether = 0;
String greet = "Hello";
int tx = 160, ty = 100;
bool touchDown = false;
uint32_t lastTouchMs = 0, lastFrame = 0, lastSlow = 0;
float lightSmooth = 50;
int blNow = 80;
float ledKick = 0;
int noteIdx = -1;
uint32_t cpnBits = 0;

// ---------- color utils ----------
uint16_t mix(uint16_t a, uint16_t b, uint8_t t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + ((br - ar) * t) / 255;
  int g = ag + ((bg - ag) * t) / 255;
  int bl = ab + ((bb - ab) * t) / 255;
  return (r << 11) | (g << 5) | bl;
}

void buildGrad() {
  uint16_t top = rgb(255, 241, 247), bot = rgb(255, 210, 228);
  for (int y = 0; y < AREA_H; y++) gradLut[y] = mix(top, bot, y * 255 / (AREA_H - 1));
}

// ---------- drawing helpers ----------
void heart(TFT_eSPI &d, int cx, int cy, int r, uint16_t col) {
  if (r < 2) r = 2;
  int o = (r * 19) / 20;
  d.fillCircle(cx - o, cy, r, col);
  d.fillCircle(cx + o, cy, r, col);
  d.fillTriangle(cx - (19 * r) / 10, cy + r / 3, cx + (19 * r) / 10, cy + r / 3,
                 cx, cy + (26 * r) / 10, col);
  if (r >= 9) d.fillCircle(cx - o - r / 3, cy - r / 3, max(2, r / 4), mix(col, 0xFFFF, 150));
}

void putF(TFT_eSPI &d, const GFXfont *f, const String &s, int x, int y, uint16_t col,
          uint8_t datum = MC_DATUM) {
  d.setFreeFont(f); d.setTextDatum(datum); d.setTextColor(col); d.drawString(s, x, y, 1);
}
void putB(TFT_eSPI &d, uint8_t font, const String &s, int x, int y, uint16_t col,
          uint8_t datum = MC_DATUM) {
  d.setFreeFont(nullptr); d.setTextDatum(datum); d.setTextColor(col); d.drawString(s, x, y, font);
}

void drawBgTFT() {
  for (int y = 0; y < AREA_H; y++) tft.drawFastHLine(0, y, 320, gradLut[y]);
}

int wrapText(const char *s, int maxW, uint8_t font, String *out, int maxL) {
  int n = 0; String line, word, str = String(s) + " ";
  for (size_t i = 0; i < str.length(); i++) {
    char ch = str[i];
    if (ch == ' ') {
      if (word.length()) {
        String t = line.length() ? line + " " + word : word;
        if (tft.textWidth(t, font) <= maxW) line = t;
        else { if (line.length() && n < maxL) out[n++] = line; line = word; }
        word = "";
      }
    } else word += ch;
  }
  if (line.length() && n < maxL) out[n++] = line;
  return n;
}

// ---------- LED / backlight ----------
void setBacklight(int pct) { ledcWrite(0, map(constrain(pct, 8, 100), 0, 100, 0, 255)); }
void setLedPink(float v) {
#if LED_GLOW
  v = constrain(v, 0.0f, 1.0f);
  ledcWrite(1, 255 - (int)(v * 170));
  ledcWrite(2, 255 - (int)(v * 22));
  ledcWrite(3, 255 - (int)(v * 85));
#else
  ledcWrite(1, 255); ledcWrite(2, 255); ledcWrite(3, 255);
#endif
}
int readLightPct() {
  uint32_t s = 0;
  for (int i = 0; i < 8; i++) s += analogRead(LDR_PIN);
  int p = map(s / 8, 0, 4095, 0, 100);
#if LDR_INVERT
  p = 100 - p;
#endif
  return constrain(p, 0, 100);
}

// heartbeat "lub-dub" 0..1
float beat() {
  float p = (millis() % 1100) / 1100.0f;
  if (p < 0.12f) return sinf(p / 0.12f * PI);
  if (p > 0.20f && p < 0.34f) return 0.7f * sinf((p - 0.20f) / 0.14f * PI);
  return 0;
}

// ---------- particles ----------
struct Part { float x, y, vx, vy, ph; int8_t r; uint16_t c; int16_t life; };
Part amb[14], brs[24];
const uint16_t PAL[] = {C_BLUSH, C_ROSE, C_LAV, C_PEACH, rgb(255, 205, 220), rgb(232, 160, 220)};

void spawnAmb(Part &p, bool initial) {
  p.x = random(10, 310); p.y = initial ? random(0, 230) : 232;
  p.r = random(4, 11); p.vy = -random(30, 90) / 100.0f; p.vx = 0;
  p.ph = random(0, 628) / 100.0f; p.c = PAL[random(0, 6)]; p.life = -1;
}
void burst(int x, int y, int n, uint16_t c1, uint16_t c2) {
  for (int i = 0; i < n; i++)
    for (auto &p : brs) if (p.life <= 0) {
      p.x = x; p.y = y; p.vx = random(-30, 30) / 10.0f; p.vy = random(-45, -5) / 10.0f;
      p.r = random(3, 7); p.life = random(22, 34); p.c = random(2) ? c1 : c2; break;
    }
}
void updateParticles() {
  if (page == P_HOME)
    for (auto &a : amb) {
      a.y += a.vy; a.x += sinf(a.y * 0.04f + a.ph) * 0.6f;
      if (a.y < -20) spawnAmb(a, false);
    }
  for (auto &p : brs) if (p.life > 0) { p.x += p.vx; p.y += p.vy; p.vy += 0.18f; p.life--; }
}
void drawParticles(int yo) {
  if (page == P_HOME)
    for (auto &a : amb)
      if (a.y + a.r * 3 >= yo && a.y - a.r * 2 < yo + STRIP_H) heart(spr, a.x, a.y - yo, a.r, a.c);
  for (auto &p : brs)
    if (p.life > 0) heart(spr, p.x, p.y - yo, p.life < 10 ? max(2, p.r * p.life / 10) : p.r, p.c);
}

// ---------- clock ----------
void refreshClock() {
  static uint32_t last = 0;
  if (last && millis() - last < 1000) return;
  last = millis();
  struct tm t;
  if (!timeOk || !getLocalTime(&t, 0)) { hasDays = false; greet = "Hello"; return; }
  struct tm a = {}; a.tm_year = ANNIV_Y - 1900; a.tm_mon = ANNIV_M - 1; a.tm_mday = ANNIV_D;
  a.tm_hour = 12; a.tm_isdst = -1;
  struct tm n = t; n.tm_hour = 12; n.tm_min = 0; n.tm_sec = 0; n.tm_isdst = -1;
  daysTogether = (long)((mktime(&n) - mktime(&a)) / 86400);
  hasDays = daysTogether >= 0;
  int h = t.tm_hour;
  greet = h < 5 ? "Sweet dreams" : h < 12 ? "Good morning" : h < 18 ? "Hello" : h < 22 ? "Good evening" : "Sweet dreams";
}

// ---------- HOME ----------
void drawHome(int yo) {
  drawParticles(yo);
  putB(spr, 4, greet + ", " + HER_NAME, 160, 22 - yo, C_TEXT);
  float b = beat();
  int r = 20 + (int)(4 * b);
  heart(spr, 160, 80 - yo, r + 6, mix(C_BLUSH, 0xFFFF, 90));
  heart(spr, 160, 80 - yo, r, C_ROSE);
  putF(spr, &FreeSerifBoldItalic18pt7b, TITLE_TEXT, 160, 156 - yo, C_DEEP);
  spr.fillRoundRect(40, 177 - yo, 240, 26, 13, C_CREAM);
  spr.drawRoundRect(40, 177 - yo, 240, 26, 13, C_BLUSH);
  putB(spr, 2, hasDays ? String(daysTogether) + " days of loving you" : String("Forever & always"),
       160, 190 - yo, C_TEXT);
}

// ---------- NOTES ----------
void drawNote() {
  drawBgTFT();
  tft.fillRoundRect(14, 14, 292, 150, 16, C_CREAM);
  tft.drawRoundRect(14, 14, 292, 150, 16, C_BLUSH);
  tft.drawRoundRect(15, 15, 290, 148, 15, C_BLUSH);
  heart(tft, 160, 36, 6, C_ROSE);
  tft.setFreeFont(&FreeSerifItalic12pt7b);
  String lines[5];
  int n = wrapText(NOTES[noteIdx], 262, 1, lines, 5);
  int lh = 28, y0 = 100 - (n - 1) * lh / 2 + 10;
  for (int i = 0; i < n; i++) putF(tft, &FreeSerifItalic12pt7b, lines[i], 160, y0 + i * lh, C_TEXT);
  tft.fillRoundRect(84, 174, 152, 30, 15, C_ROSE);
  putB(tft, 2, "Another note", 160, 189, 0xFFFF);
}
void nextNote() {
  int cnt = sizeof(NOTES) / sizeof(NOTES[0]), n;
  do { n = random(0, cnt); } while (n == noteIdx && cnt > 1);
  noteIdx = n; drawNote();
}

// ---------- COUPONS ----------
int cpn(int i) { return (cpnBits >> (2 * i)) & 3; }
void drawCoupon(int i) {
  int x = 12 + (i % 2) * 152, y = 34 + (i / 2) * 58, s = cpn(i);
  if (s == 0) {
    tft.fillRoundRect(x, y, 144, 50, 12, C_BLUSH);
    heart(tft, x + 26, y + 22, 8, C_CREAM);
    putB(tft, 2, "Surprise!", x + 94, y + 25, C_DEEP);
  } else if (s == 1) {
    tft.fillRoundRect(x, y, 144, 50, 12, C_CREAM);
    tft.drawRoundRect(x, y, 144, 50, 12, C_ROSE);
    tft.drawRoundRect(x + 1, y + 1, 142, 48, 11, C_ROSE);
    String l[2];
    tft.setFreeFont(nullptr);
    int n = wrapText(COUPONS[i], 126, 2, l, 2);
    for (int k = 0; k < n; k++) putB(tft, 2, l[k], x + 72, y + 25 + (k - (n - 1) / 2.0f) * 17, C_TEXT);
  } else {
    tft.fillRoundRect(x, y, 144, 50, 12, rgb(238, 228, 250));
    heart(tft, x + 26, y + 22, 8, C_LAV);
    putB(tft, 2, "Enjoyed", x + 94, y + 25, C_SOFT);
  }
}
void drawCoupons() {
  drawBgTFT();
  putF(tft, &FreeSerifBoldItalic12pt7b, "Love Coupons", 160, 16, C_DEEP);
  for (int i = 0; i < 6; i++) drawCoupon(i);
}
void couponTap(int x, int y) {
  for (int i = 0; i < 6; i++) {
    int cx = 12 + (i % 2) * 152, cy = 34 + (i / 2) * 58;
    if (x >= cx && x < cx + 144 && y >= cy && y < cy + 50) {
      int s = cpn(i);
      if (s < 2) {
        cpnBits &= ~(3u << (2 * i)); cpnBits |= ((uint32_t)(s + 1) << (2 * i));
        prefs.putUInt("cpn", cpnBits);
        drawCoupon(i);
      }
      return;
    }
  }
}

// ---------- GAME ----------
struct Item { float x, y, vy; uint8_t type; bool on; } items[8];
int gstate = 0, score = 0, lives = 3, spawnT = 0;
uint32_t best = 0;
float bx = 160;

void startGame() {
  score = 0; lives = 3; spawnT = 20; gstate = 1;
  for (auto &i : items) i.on = false;
}
void spawnItem() {
  for (auto &i : items) if (!i.on) {
    int r = random(100);
    i.type = r < 8 ? 1 : (r < 26 ? 2 : 0);
    i.x = random(20, 300); i.y = -10; i.on = true;
    i.vy = 2.0f + min(score * 0.05f, 2.5f) + random(0, 6) / 10.0f;
    return;
  }
}
void checkOver() {
  if (lives <= 0) {
    gstate = 2;
    if ((uint32_t)score > best) { best = score; prefs.putUInt("best", best); }
    burst(bx, 190, 16, C_ROSE, C_GOLD);
  }
}
void updateGame() {
  float target = touchDown ? constrain(tx, 34, 286) : bx;
  bx += (target - bx) * 0.35f;
  if (gstate != 1) return;
  if (--spawnT <= 0) { spawnItem(); spawnT = max(14, 40 - score / 2); }
  for (auto &it : items) {
    if (!it.on) continue;
    it.y += it.vy;
    if (it.y >= 176 && it.y <= 200 && fabsf(it.x - bx) < 38) {
      it.on = false;
      if (it.type == 0) { score++; burst(it.x, it.y, 6, C_ROSE, C_BLUSH); ledKick = 0.6f; }
      else if (it.type == 1) { score += 5; burst(it.x, it.y, 12, C_GOLD, C_CREAM); ledKick = 1; }
      else { lives--; burst(it.x, it.y, 8, C_LAV, C_SOFT); checkOver(); }
    } else if (it.y > 228) {
      it.on = false;
      if (it.type == 0) { lives--; checkOver(); }
    }
  }
}
void drawGame(int yo) {
  for (auto &it : items) {
    if (!it.on) continue;
    uint16_t c = it.type == 0 ? C_ROSE : it.type == 1 ? C_GOLD : rgb(150, 128, 176);
    heart(spr, it.x, it.y - yo, 9, c);
    if (it.type == 2) {
      uint16_t k = rgb(80, 60, 100);
      spr.drawLine(it.x - 2, it.y - 7 - yo, it.x + 2, it.y - 1 - yo, k);
      spr.drawLine(it.x + 2, it.y - 1 - yo, it.x - 2, it.y + 3 - yo, k);
      spr.drawLine(it.x - 2, it.y + 3 - yo, it.x + 1, it.y + 9 - yo, k);
    }
  }
  drawParticles(yo);
  spr.fillRoundRect(bx - 32, 186 - yo, 64, 22, 10, C_ROSE);
  spr.fillRoundRect(bx - 35, 182 - yo, 70, 8, 4, C_DEEP);
  heart(spr, bx, 196 - yo, 4, C_CREAM);
  spr.fillRoundRect(10, 8 - yo, 92, 24, 12, C_CREAM);
  spr.drawRoundRect(10, 8 - yo, 92, 24, 12, C_BLUSH);
  putB(spr, 2, "Score " + String(score), 56, 20 - yo, C_TEXT);
  for (int i = 0; i < 3; i++) heart(spr, 250 + i * 24, 18 - yo, 6, i < lives ? C_ROSE : mix(C_BLUSH, 0xFFFFu & C_LAV, 128));
  if (gstate != 1) {
    spr.fillRoundRect(48, 56 - yo, 224, 100, 18, C_CREAM);
    spr.drawRoundRect(48, 56 - yo, 224, 100, 18, C_ROSE);
    if (gstate == 0) {
      putF(spr, &FreeSerifBoldItalic12pt7b, "Catch the hearts", 160, 82 - yo, C_DEEP);
      putB(spr, 2, "Slide to move - tap to start", 160, 112 - yo, C_TEXT);
      putB(spr, 2, "Gold = +5     Dark = ouch", 160, 134 - yo, C_SOFT);
    } else {
      putF(spr, &FreeSerifBoldItalic12pt7b, "Score: " + String(score), 160, 82 - yo, C_DEEP);
      putB(spr, 2, "Best: " + String(best), 160, 110 - yo, C_TEXT);
      putB(spr, 2, "Tap to play again", 160, 134 - yo, C_SOFT);
    }
  }
}

// ---------- nav ----------
void icoMail(int cx, int cy, uint16_t c) {
  tft.drawRoundRect(cx - 10, cy - 7, 20, 14, 3, c);
  tft.drawLine(cx - 9, cy - 6, cx, cy + 1, c); tft.drawLine(cx + 9, cy - 6, cx, cy + 1, c);
}
void icoGift(int cx, int cy, uint16_t c) {
  tft.drawRect(cx - 8, cy - 2, 16, 10, c); tft.drawRect(cx - 10, cy - 6, 20, 5, c);
  tft.drawFastVLine(cx, cy - 6, 14, c);
  tft.drawCircle(cx - 3, cy - 9, 3, c); tft.drawCircle(cx + 3, cy - 9, 3, c);
}
void icoFlower(int cx, int cy, uint16_t c) {
  for (int i = 0; i < 5; i++) {
    float a = i * 1.2566f - 1.5708f;
    tft.fillCircle(cx + cosf(a) * 6, cy + sinf(a) * 6, 4, c);
  }
  tft.fillCircle(cx, cy, 3, C_GOLD);
}
void drawNav() {
  tft.fillRect(0, NAV_Y, 320, 28, C_CREAM);
  tft.drawFastHLine(0, NAV_Y, 320, C_BLUSH);
  for (int i = 0; i < 4; i++) {
    bool act = (i == (int)page);
    int cx = i * 80 + 40, cy = NAV_Y + 14;
    if (act) tft.fillRoundRect(cx - 24, NAV_Y + 3, 48, 22, 11, C_BLUSH);
    uint16_t c = act ? C_DEEP : C_SOFT;
    if (i == 0) heart(tft, cx, cy - 3, 5, c);
    else if (i == 1) icoMail(cx, cy, c);
    else if (i == 2) icoGift(cx, cy + 1, c);
    else icoFlower(cx, cy, act ? C_ROSE : C_PEACH);
  }
}

void gotoPage(Page p) {
  page = p;
  for (auto &b : brs) b.life = 0;
  if (p == P_NOTES) { if (noteIdx < 0) nextNote(); else drawNote(); }
  if (p == P_COUPON) drawCoupons();
  if (p == P_GAME) gstate = 0;
  drawNav();
  lastFrame = 0;
}

// ---------- touch ----------
void onPress(int x, int y) {
  if (y >= NAV_Y) { Page p = (Page)constrain(x / 80, 0, 3); if (p != page) gotoPage(p); return; }
  switch (page) {
    case P_HOME: if (y > 30 && y < 150) { burst(x, y, 12, C_ROSE, C_BLUSH); ledKick = 1; } break;
    case P_NOTES: nextNote(); break;
    case P_COUPON: couponTap(x, y); break;
    case P_GAME: if (gstate != 1) startGame(); break;
  }
}

// ---------- splash / time ----------
void splashBeat(float k) {
  for (int y = 40; y < 170; y++) tft.drawFastHLine(0, y, 320, gradLut[y]);
  heart(tft, 160, 88, 18 + (int)(5 * k), C_ROSE);
  putF(tft, &FreeSerifBoldItalic18pt7b, "For you", 160, 146, C_DEEP);
}

bool syncTime() {
  if (!strlen(WIFI_SSID)) return false;
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis(); int f = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) { splashBeat(fabsf(sinf(f++ * 0.2f))); delay(40); }
  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com");
    struct tm t;
    while (millis() - t0 < 20000) {
      splashBeat(fabsf(sinf(f++ * 0.2f)));
      if (getLocalTime(&t, 50) && t.tm_year + 1900 >= 2024) { ok = true; break; }
    }
  }
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  return ok;
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  ledcSetup(0, 5000, 8); ledcAttachPin(BL_PIN, 0); ledcWrite(0, 0);
  ledcSetup(1, 5000, 8); ledcAttachPin(LED_R, 1);
  ledcSetup(2, 5000, 8); ledcAttachPin(LED_G, 2);
  ledcSetup(3, 5000, 8); ledcAttachPin(LED_B, 3);
  setLedPink(0);
  pinMode(LDR_PIN, INPUT);

  prefs.begin("lovegift", false);
  best = prefs.getUInt("best", 0);
  cpnBits = prefs.getUInt("cpn", 0);

  buildGrad();
  tft.init(); tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.invertDisplay(true);     // required on this specific CYD panel
  drawBgTFT(); tft.fillRect(0, 212, 320, 28, gradLut[AREA_H - 1]);
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI); ts.setRotation(1);

  if (!spr.createSprite(320, STRIP_H)) Serial.println("Sprite alloc failed!");
  blNow = 85; setBacklight(blNow);

  for (int i = 0; i < 24; i += 2) { splashBeat(i / 24.0f); delay(30); }
  timeOk = syncTime();
  for (auto &a : amb) spawnAmb(a, true);
  for (auto &b : brs) b.life = 0;
  lightSmooth = readLightPct();
  refreshClock();
  gotoPage(P_HOME);
}

void loop() {
  uint32_t now = millis();

  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    tx = constrain(map(p.x, TS_MINX, TS_MAXX, 0, 320), 0, 319);
    ty = constrain(map(p.y, TS_MINY, TS_MAXY, 0, 240), 0, 239);
    lastTouchMs = now;
    if (!touchDown) { touchDown = true; onPress(tx, ty); }
  } else if (touchDown && now - lastTouchMs > 60) touchDown = false;

  if (now - lastFrame >= 33) {
    lastFrame = now;
    refreshClock();
    updateParticles();
    if (page == P_GAME) updateGame();

    // LED
    float base = (page == P_HOME) ? beat() : 0.18f + 0.12f * sinf(now / 700.0f);
    ledKick *= 0.9f;
    setLedPink(max(base, ledKick));

    if (page == P_HOME || page == P_GAME) {
      for (int s = 0; s < 2; s++) {
        int yo = s * STRIP_H;
        for (int y = 0; y < STRIP_H; y++) spr.drawFastHLine(0, y, 320, gradLut[yo + y]);
        if (page == P_HOME) drawHome(yo); else drawGame(yo);
        spr.pushSprite(0, yo);
      }
    }
  }

#if AUTO_DIM
  if (now - lastSlow >= 500) {
    lastSlow = now;
    lightSmooth = lightSmooth * 0.7f + readLightPct() * 0.3f;
    int target = constrain(map((int)lightSmooth, 0, 35, 25, 100), 25, 100);
    blNow += constrain(target - blNow, -4, 4);
    setBacklight(blNow);
  }
#endif
}
