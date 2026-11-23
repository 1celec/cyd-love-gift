/* CYD Love Gift — a pastel, buffered interface for ESP32-2432S028R. */
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"

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

constexpr int W = 320, H = 240, NAV_Y = 203;
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}
const uint16_t BG = rgb(255, 238, 244), PAPER = rgb(255, 252, 250);
const uint16_t INK = rgb(120, 54, 82), MUTED = rgb(183, 108, 137);
const uint16_t ROSE = rgb(238, 99, 147), PALE = rgb(255, 215, 228);
const uint16_t LINE = rgb(247, 193, 211), GOLD = rgb(246, 190, 111);
const uint16_t LAV = rgb(187, 155, 209), LILAC = rgb(239, 226, 250);
const uint16_t PEACH = rgb(255, 224, 210), CHEEK = rgb(255, 167, 193);
TFT_eSPI tft;
TFT_eSprite canvas(&tft);
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;
enum Page { HOME, NOTES_PAGE, COUPONS_PAGE, GAME };
Page page = HOME;
bool dirty = true, touchDown = false, timeOk = false, hasDays = false;
int tx = 160, ty = 100, noteIdx = -1, blNow = 85;
uint32_t lastTouch = 0, lastFrame = 0, lastSlow = 0, cpnBits = 0, best = 0;
float lightSmooth = 50, ledKick = 0;
long daysTogether = 0;
String greeting = "Hello";

void text(const String &s, int x, int y, uint16_t color, int font = 2, uint8_t datum = MC_DATUM) {
  canvas.setFreeFont(nullptr); canvas.setTextDatum(datum); canvas.setTextColor(color);
  canvas.drawString(s, x, y, font);
}
void serif(const String &s, int x, int y, uint16_t color, const GFXfont *font = &FreeSerifBoldItalic12pt7b) {
  canvas.setFreeFont(font); canvas.setTextDatum(MC_DATUM); canvas.setTextColor(color);
  canvas.drawString(s, x, y, 1);
}
void heart(TFT_eSPI &d, int x, int y, int r, uint16_t color) {
  r = max(2, r);
  d.fillCircle(x - r, y, r, color); d.fillCircle(x + r, y, r, color);
  d.fillTriangle(x - 2*r, y + r/3, x + 2*r, y + r/3, x, y + 3*r, color);
}
void sparkle(int x, int y, int r, uint16_t c) {
  canvas.drawFastVLine(x, y-r, r*2+1, c); canvas.drawFastHLine(x-r, y, r*2+1, c);
}
void card(int x, int y, int w, int h, int radius = 14) {
  canvas.fillRoundRect(x, y+2, w, h, radius, LINE);
  canvas.fillRoundRect(x, y, w, h, radius, PAPER);
  canvas.drawRoundRect(x, y, w, h, radius, LINE);
}
// Small vector stickers keep the artwork crisp without storing bitmap assets.
void bow(int x, int y, uint16_t color) {
  canvas.fillTriangle(x, y, x-10, y-6, x-10, y+6, color);
  canvas.fillTriangle(x, y, x+10, y-6, x+10, y+6, color);
  canvas.fillCircle(x, y, 3, PAPER);
}
void cloud(int x, int y, uint16_t color) {
  canvas.fillCircle(x-10, y, 7, color);
  canvas.fillCircle(x, y-4, 10, color);
  canvas.fillCircle(x+11, y, 7, color);
  canvas.fillRoundRect(x-16, y, 33, 7, 3, color);
}
void bunny(int x, int y, uint16_t accent) {
  // Ears and face are deliberately simple enough for the small CYD screen.
  canvas.fillRoundRect(x-12, y-22, 9, 21, 4, PAPER);
  canvas.fillRoundRect(x+3, y-22, 9, 21, 4, PAPER);
  canvas.fillRoundRect(x-9, y-19, 3, 13, 1, accent);
  canvas.fillRoundRect(x+6, y-19, 3, 13, 1, accent);
  canvas.fillCircle(x, y+3, 16, PAPER);
  canvas.fillCircle(x-5, y+2, 1, INK); canvas.fillCircle(x+5, y+2, 1, INK);
  canvas.fillCircle(x-9, y+7, 2, CHEEK); canvas.fillCircle(x+9, y+7, 2, CHEEK);
  canvas.fillCircle(x, y+6, 1, ROSE);
  canvas.drawLine(x-2, y+9, x, y+10, INK);
  canvas.drawLine(x, y+10, x+2, y+9, INK);
}
int wrap(const char *s, int width, String *lines, int maxLines) {
  canvas.setFreeFont(&FreeSerifItalic12pt7b);
  String line, word, input = String(s) + " "; int n = 0;
  for (unsigned i = 0; i < input.length(); ++i) {
    if (input[i] != ' ') { word += input[i]; continue; }
    if (!word.length()) continue;
    String next = line.length() ? line + " " + word : word;
    if (canvas.textWidth(next, 1) <= width) line = next;
    else { if (line.length() && n < maxLines) lines[n++] = line; line = word; }
    word = "";
  }
  if (line.length() && n < maxLines) lines[n++] = line;
  return n;
}
void setLed(float value) {
#if LED_GLOW
  value = constrain(value, 0.0f, 1.0f);
  ledcWrite(1, 255 - int(value * 150)); ledcWrite(2, 255 - int(value * 18)); ledcWrite(3, 255 - int(value * 65));
#else
  ledcWrite(1, 255); ledcWrite(2, 255); ledcWrite(3, 255);
#endif
}
void setBacklight(int pct) { ledcWrite(0, map(constrain(pct, 8, 100), 0, 100, 0, 255)); }
int readLight() {
  uint32_t sum = 0; for (int i = 0; i < 8; i++) sum += analogRead(LDR_PIN);
  int p = map(sum / 8, 0, 4095, 0, 100);
#if LDR_INVERT
  p = 100 - p;
#endif
  return constrain(p, 0, 100);
}
void refreshClock() {
  static uint32_t last = 0;
  if (last && millis() - last < 1000) return;
  last = millis(); struct tm t;
  if (!timeOk || !getLocalTime(&t, 0)) return;
  struct tm a = {}; a.tm_year = ANNIV_Y - 1900; a.tm_mon = ANNIV_M - 1; a.tm_mday = ANNIV_D; a.tm_hour = 12; a.tm_isdst = -1;
  struct tm n = t; n.tm_hour = 12; n.tm_min = n.tm_sec = 0; n.tm_isdst = -1;
  long days = long((mktime(&n) - mktime(&a)) / 86400);
  String g = t.tm_hour < 5 ? "Sweet dreams" : t.tm_hour < 12 ? "Good morning" : t.tm_hour < 18 ? "Hello" : t.tm_hour < 22 ? "Good evening" : "Sweet dreams";
  if (days != daysTogether || g != greeting || !hasDays) dirty = true;
  daysTogether = days; hasDays = days >= 0; greeting = g;
}

struct Particle { float x, y, vx, vy, life; } particles[18];
void burst(int x, int y) {
  for (auto &p : particles) {
    p.x = x; p.y = y; p.vx = random(-45, 46) / 10.0f; p.vy = random(-55, -10) / 10.0f; p.life = random(24, 42);
  }
  ledKick = 1;
}
void updateParticles(float step) {
  for (auto &p : particles) if (p.life > 0) {
    p.x += p.vx * step; p.y += p.vy * step; p.vy += 0.14f * step; p.life -= step;
  }
}
void drawParticles() {
  for (auto &p : particles) if (p.life > 0 && p.y < NAV_Y - 5) heart(canvas, p.x, p.y, p.life < 8 ? 2 : 3, p.life < 12 ? PALE : ROSE);
}
// A little smiling heart is drawn entirely into the framebuffer.
void cuteHeart(int x, int y, int r) {
  heart(canvas, x, y + 3, r, LINE); // soft offset shadow
  heart(canvas, x, y, r, ROSE);
  canvas.fillCircle(x-r+2, y-r/2, max(2, r/4), CHEEK);
  canvas.fillCircle(x-r/2, y+2, 2, INK);
  canvas.fillCircle(x+r/2, y+2, 2, INK);
  canvas.fillCircle(x-r, y+8, 3, CHEEK);
  canvas.fillCircle(x+r, y+8, 3, CHEEK);
  canvas.drawLine(x-3, y+10, x, y+12, INK);
  canvas.drawLine(x, y+12, x+3, y+10, INK);
}
void drawHome(uint32_t now) {
  // A tiny pastel world around the heart; the artwork stays inside the content area.
  canvas.fillCircle(18, 61, 21, PEACH);
  canvas.fillCircle(304, 69, 26, LILAC);
  cloud(47, 63, PAPER); cloud(274, 56, PAPER);
  heart(canvas, 27, 43, 3, ROSE); heart(canvas, 292, 92, 4, LAV);
  sparkle(65, 81, 4, GOLD); sparkle(254, 85, 4, ROSE);
  sparkle(89, 140, 3, LAV); sparkle(231, 142, 3, GOLD);
  canvas.fillCircle(80, 59, 2, CHEEK); canvas.fillCircle(239, 68, 2, LAV);

  text(greeting + ", " + HER_NAME + "!", 160, 17, INK, 2);
  text("MADE WITH LOVE, JUST FOR YOU", 160, 33, MUTED, 1);
  float breathe = (sinf(now / 950.0f) + 1.0f) * 0.5f;
  int bob = int(sinf(now / 1100.0f) * 2.0f);
  canvas.fillCircle(160, 89, 43, PALE);
  canvas.drawCircle(160, 89, 47, PAPER);
  for (int i = 0; i < 12; i++) {
    float angle = i * 6.283185f / 12;
    canvas.fillCircle(160 + int(cosf(angle)*47), 89 + int(sinf(angle)*47), 1, ROSE);
  }
  cuteHeart(160, 76+bob, 15 + int(breathe * 2));
  bow(182, 65+bob, LAV);
  bunny(43, 125, PALE);
  bunny(277, 125, LILAC);
  heart(canvas, 65, 127, 3, ROSE); heart(canvas, 255, 128, 3, ROSE);
  canvas.fillRoundRect(132, 128, 56, 14, 7, PAPER);
  text("tap me!", 160, 135, ROSE, 1);
  serif(TITLE_TEXT, 160, 155, INK);
  canvas.fillRoundRect(48, 172, 224, 23, 11, LINE);
  canvas.fillRoundRect(48, 170, 224, 23, 11, PAPER);
  heart(canvas, 63, 177, 3, ROSE); heart(canvas, 257, 177, 3, ROSE);
  text(hasDays ? String(daysTogether) + " days of loving you" : "Forever & always", 160, 181, INK, 2);
  drawParticles();
}
void nextNote() {
  int count = sizeof(NOTES) / sizeof(NOTES[0]);
  if (count == 0) return;
  int n; do { n = random(count); } while (n == noteIdx && count > 1);
  noteIdx = n; dirty = true;
}
void drawNote() {
  heart(canvas, 31, 16, 4, ROSE);
  text("a little love note", 160, 15, INK, 2);
  sparkle(286, 16, 4, LAV);
  card(15, 30, 290, 128, 16);
  // Washi-tape corners and a heart seal make the note feel handwritten.
  canvas.fillRoundRect(21, 33, 30, 8, 3, LILAC);
  canvas.fillRoundRect(269, 148, 30, 8, 3, PEACH);
  canvas.fillRoundRect(113, 25, 94, 19, 9, PALE);
  text("FOR YOU", 160, 34, ROSE, 1);
  heart(canvas, 34, 52, 3, CHEEK);
  heart(canvas, 286, 137, 3, CHEEK);
  sparkle(38, 138, 3, LAV); sparkle(280, 53, 3, GOLD);
  String lines[4]; int n = wrap(NOTES[noteIdx], 250, lines, 4);
  for (int i = 0; i < n; i++) serif(lines[i], 160, 101 + (i - (n - 1) / 2.0f) * 25, INK, &FreeSerifItalic12pt7b);
  canvas.fillRoundRect(90, 169, 140, 27, 13, LINE);
  canvas.fillRoundRect(90, 167, 140, 27, 13, ROSE);
  text("another note  >", 160, 180, PAPER, 2);
}
int couponState(int i) { return (cpnBits >> (2*i)) & 3; }
void drawCoupons() {
  heart(canvas, 38, 10, 3, ROSE);
  text("sweet little surprises", 160, 12, INK, 2);
  sparkle(282, 12, 3, LAV);
  for (int i = 0; i < 6; i++) {
    int x = 12 + i%2*152, y = 26 + i/2*57, state = couponState(i);
    card(x, y, 144, 50, 10);
    if (state == 0) {
      canvas.fillRoundRect(x+3, y+3, 138, 44, 8, i % 2 ? LILAC : PEACH);
      canvas.fillCircle(x+24, y+25, 15, PAPER);
      heart(canvas, x+24, y+20, 5, i % 2 ? LAV : ROSE);
      bow(x+137, y+7, i % 2 ? ROSE : LAV);
      text("open me!", x+92, y+17, INK, 2); text("A TINY GIFT", x+92, y+34, MUTED, 1);
    } else if (state == 1) {
      // Built-in font fits more text in the compact ticket.
      canvas.setFreeFont(nullptr); String line, word, input = String(COUPONS[i]) + " "; String lines[2]; int n = 0;
      for (unsigned j = 0; j < input.length(); j++) {
        if (input[j] != ' ') { word += input[j]; continue; }
        if (!word.length()) continue;
        String next = line.length() ? line + " " + word : word;
        if (canvas.textWidth(next, 2) <= 128) line = next;
        else { if (n < 2 && line.length()) lines[n++] = line; line = word; }
        word = "";
      }
      if (line.length() && n < 2) lines[n++] = line;
      for (int j = 0; j < n; j++) text(lines[j], x+72, y+19+(j-(n-1)/2.0f)*15, INK, 2);
      text("TAP WHEN ENJOYED", x+72, y+42, MUTED, 1);
    } else {
      heart(canvas, x+22, y+20, 5, PALE);
      text("Loved!", x+85, y+25, MUTED, 2);
    }
  }
}
void couponTap(int x, int y) {
  for (int i = 0; i < 6; i++) {
    int cx = 12 + i%2*152, cy = 26 + i/2*57;
    if (x >= cx && x < cx+144 && y >= cy && y < cy+50) {
      int state = couponState(i);
      if (state < 2) { cpnBits = (cpnBits & ~(3u << (2*i))) | (uint32_t(state+1) << (2*i)); prefs.putUInt("cpn", cpnBits); dirty = true; }
      return;
    }
  }
}
struct Item { float x, y, speed; uint8_t type; bool active; } items[8];
int gameState = 0, score = 0, lives = 3;
float basket = 160, spawnIn = 0;
void startGame() { score = 0; lives = 3; spawnIn = 0.6f; basket = 160; gameState = 1; for (auto &i : items) i.active = false; }
void updateGame(float dt) {
  if (gameState != 1) return;
  if (touchDown && ty < NAV_Y) basket += (constrain(tx, 32, 288) - basket) * min(1.0f, dt * 12.0f);
  spawnIn -= dt;
  if (spawnIn <= 0) {
    for (auto &i : items) if (!i.active) {
      int r = random(100); i.type = r < 8 ? 1 : r < 26 ? 2 : 0;
      i.x = random(20, 300); i.y = -12; i.speed = 60 + min(score * 1.5f, 75.0f) + random(18); i.active = true; break;
    }
    spawnIn = max(0.48f, 1.3f - score * 0.015f);
  }
  for (auto &i : items) if (i.active) {
    float previousY = i.y; i.y += i.speed * dt;
    if (previousY <= 176 && i.y >= 176 && fabsf(i.x-basket) < 35) {
      i.active = false;
      if (i.type == 2) lives--; else { score += i.type == 1 ? 5 : 1; ledKick = 0.7f; }
    } else if (i.y > 198) { i.active = false; if (i.type == 0) lives--; }
    if (lives <= 0) { gameState = 2; dirty = true; if ((uint32_t)score > best) { best = score; prefs.putUInt("best", best); } break; }
  }
}
void drawGame() {
  cloud(35, 56, PAPER); cloud(284, 88, PAPER);
  sparkle(76, 103, 3, LAV); sparkle(244, 54, 3, GOLD);
  canvas.fillRoundRect(9, 5, 96, 24, 12, PAPER);
  text("score  " + String(score), 20, 17, INK, 2, ML_DATUM);
  for (int i = 0; i < 3; i++) heart(canvas, 260+i*22, 12, 5, i < lives ? ROSE : LINE);
  for (auto &i : items) if (i.active) {
    heart(canvas, i.x, i.y, 7, i.type == 0 ? ROSE : i.type == 1 ? GOLD : LAV);
    if (i.type == 2) { canvas.drawLine(i.x, i.y-5, i.x-3, i.y+3, PAPER); canvas.drawLine(i.x-3, i.y+3, i.x+2, i.y+8, PAPER); }
  }
  canvas.fillRoundRect(basket-31, 176, 62, 17, 8, ROSE);
  canvas.fillRoundRect(basket-35, 172, 70, 7, 3, CHEEK);
  heart(canvas, basket, 180, 3, PAPER);
  if (gameState != 1) {
    card(40, 48, 240, 105);
    heart(canvas, 63, 61, 4, ROSE); heart(canvas, 257, 61, 4, LAV);
    serif(gameState == 0 ? "Catch the hearts" : "Lovely play!", 160, 72, INK);
    text(gameState == 0 ? "Slide to move your basket" : "Score: " + String(score) + "   Best: " + String(best), 160, 101, MUTED, 2);
    text(gameState == 0 ? "Gold +5  /  avoid dark hearts" : "Tap to play again", 160, 126, ROSE, 2);
  }
}
void drawNav() {
  canvas.fillRect(0, NAV_Y, W, H-NAV_Y, PAPER);
  canvas.drawFastHLine(0, NAV_Y, W, LINE);
  const char *labels[] = {"home", "notes", "gifts", "play"};
  for (int i = 0; i < 4; i++) {
    int x = i*80+40; bool selected = int(page) == i;
    if (selected) canvas.fillRoundRect(x-30, NAV_Y+3, 60, 31, 14, PALE);
    uint16_t c = selected ? ROSE : MUTED;
    if (i == 0) heart(canvas, x, NAV_Y+10, 3, c);
    else if (i == 1) {
      canvas.drawRoundRect(x-6, NAV_Y+7, 12, 9, 2, c);
      canvas.drawLine(x-5, NAV_Y+8, x, NAV_Y+12, c);
      canvas.drawLine(x+5, NAV_Y+8, x, NAV_Y+12, c);
    } else if (i == 2) {
      canvas.drawRect(x-5, NAV_Y+9, 10, 7, c);
      canvas.drawFastHLine(x-7, NAV_Y+8, 14, c);
      canvas.drawFastVLine(x, NAV_Y+6, 10, c);
      canvas.drawCircle(x-2, NAV_Y+6, 2, c);
      canvas.drawCircle(x+2, NAV_Y+6, 2, c);
    } else {
      canvas.drawCircle(x, NAV_Y+11, 6, c);
      canvas.fillCircle(x-2, NAV_Y+10, 1, c); canvas.fillCircle(x+2, NAV_Y+10, 1, c);
      canvas.drawFastHLine(x-2, NAV_Y+14, 5, c);
    }
    text(labels[i], x, NAV_Y+26, c, 1);
  }
}
void render(uint32_t now) {
  canvas.fillSprite(BG);
  switch (page) { case HOME: drawHome(now); break; case NOTES_PAGE: drawNote(); break; case COUPONS_PAGE: drawCoupons(); break; case GAME: drawGame(); break; }
  drawNav();
  canvas.pushSprite(0, 0);
  dirty = false;
}
void gotoPage(Page p) {
  page = p;
  for (auto &particle : particles) particle.life = 0;
  if (p == NOTES_PAGE && noteIdx < 0) nextNote();
  if (p == GAME) { gameState = 0; for (auto &i : items) i.active = false; }
  dirty = true;
}
void onPress(int x, int y) {
  if (y >= NAV_Y) { Page p = (Page)constrain(x/80, 0, 3); if (p != page) gotoPage(p); return; }
  switch (page) {
    case HOME: if (y > 40 && y < 145) { burst(x, y); dirty = true; } break;
    case NOTES_PAGE: nextNote(); break;
    case COUPONS_PAGE: couponTap(x, y); break;
    case GAME: if (gameState != 1) startGame(); dirty = true; break;
  }
}
bool syncTime() {
  if (!strlen(WIFI_SSID)) return false;
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-start < 12000) delay(50);
  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com"); struct tm t;
    while (millis()-start < 20000) { if (getLocalTime(&t, 50) && t.tm_year+1900 >= 2024) { ok = true; break; } delay(10); }
  }
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF); return ok;
}
void setup() {
  Serial.begin(115200); randomSeed(esp_random());
  ledcSetup(0, 5000, 8); ledcAttachPin(BL_PIN, 0); ledcWrite(0, 0);
  ledcSetup(1, 5000, 8); ledcAttachPin(LED_R, 1);
  ledcSetup(2, 5000, 8); ledcAttachPin(LED_G, 2);
  ledcSetup(3, 5000, 8); ledcAttachPin(LED_B, 3); setLed(0);
  pinMode(LDR_PIN, INPUT); prefs.begin("lovegift", false);
  best = prefs.getUInt("best", 0); cpnBits = prefs.getUInt("cpn", 0);
  tft.init(); tft.setRotation(1); tft.setSwapBytes(true); tft.invertDisplay(true);
  tft.fillScreen(BG); tft.setTextColor(INK, BG); tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSerifBoldItalic18pt7b); tft.drawString("For you", 160, 114, 1);
  setBacklight(blNow);
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS); ts.begin(touchSPI); ts.setRotation(1);
  timeOk = syncTime();
  // Allocate after Wi-Fi has been shut down. Prefer full color; fall back to RGB332
  // on boards with less free heap, never draw through a failed allocation.
  canvas.setColorDepth(16);
  if (!canvas.createSprite(W, H)) {
    Serial.println("Full-color framebuffer unavailable; trying 8-bit.");
    canvas.setColorDepth(8);
    if (!canvas.createSprite(W, H)) {
      Serial.println("Framebuffer allocation failed.");
      tft.setFreeFont(nullptr); tft.drawString("Not enough memory", 160, 155, 2);
      while (true) delay(1000);
    }
  }
  lightSmooth = readLight(); refreshClock(); gotoPage(HOME);
}
void loop() {
  uint32_t now = millis();
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    tx = constrain(map(p.x, TS_MINX, TS_MAXX, 0, W), 0, W-1);
    ty = constrain(map(p.y, TS_MINY, TS_MAXY, 0, H), 0, H-1);
    lastTouch = now;
    if (!touchDown) { touchDown = true; onPress(tx, ty); }
  } else if (touchDown && now-lastTouch > 60) touchDown = false;
  if (now-lastFrame >= 33) {
    float dt = min((now-lastFrame) / 1000.0f, 0.1f); lastFrame = now;
    refreshClock(); updateParticles(dt * 30.0f);
    if (page == GAME) updateGame(dt);
    ledKick *= powf(0.9f, dt * 30.0f);
    setLed(max(0.12f + 0.10f * (sinf(now/950.0f)+1.0f), ledKick));
    // Static pages are only pushed when their content changes.
    if (dirty || page == HOME || (page == GAME && gameState == 1)) render(now);
  }
#if AUTO_DIM
  if (now-lastSlow >= 500) {
    lastSlow = now; lightSmooth = lightSmooth * 0.7f + readLight() * 0.3f;
    int target = constrain(map((int)lightSmooth, 0, 35, 25, 100), 25, 100);
    blNow += constrain(target-blNow, -4, 4); setBacklight(blNow);
  }
#endif
  delay(1);
}
