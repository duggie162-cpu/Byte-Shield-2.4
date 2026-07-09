#include "tools_menu24.h"
#include "ui24.h"
#include "touch24.h"
#include "config24.h"
#include <string.h>

extern TFT_eSPI tft;
static bool     _inverted     = false;
static uint8_t  _brightness   = 200;
static uint8_t  _timeoutIdx   = 3;
static uint32_t _lastActivity = 30000;
static const uint32_t _timeouts[]      = { 30000, 60000, 300000, 0 };
static const char*    _timeoutLabels[] = { "30 SEC", "1 MIN", "5 MIN", "OFF" };

#define BTN_X      8
#define BTN_W      (SCREEN_W - 16)
#define BTN_H      40
#define BTN_PAD    5
#define BTN_STRIDE (BTN_H + BTN_PAD)
#define BTN_COUNT  7
#define LIST_Y     (HEADER_H + 6)
#define TOTAL_H    (BTN_COUNT * BTN_STRIDE)
#define VISIBLE_H  (SCREEN_H - LIST_Y)

static int _scrollY = 0;
static inline int _btnY(int i) {
  return LIST_Y + i * BTN_STRIDE - _scrollY;
}

// Button index 
//  0 INVERT  1 BRIGHTNESS  2 SPEAKER TEST  3 TIMEOUT
//  4 RECALIBRATE  5 SECURITY GUIDE  6 TESTING GUIDE

static void _drawBrightnessBar() {
  int barY = _btnY(1) + BTN_H + 1;
  if (barY < LIST_Y || barY > SCREEN_H) return;
  int barX = BTN_X + 4;
  int barW = BTN_W - 8;
  int barH = 4;
  int fill = map(_brightness, 0, 255, 0, barW);
  tft.fillRect(barX, barY, barW, barH, C_PANEL);
  tft.drawRect(barX, barY, barW, barH, C_SUBTEXT);
  tft.fillRect(barX, barY, fill, barH, C_ACCENT);
  for (int i = 1; i < 4; i++) {
    int txk = barX + (barW * i / 4);
    tft.drawFastVLine(txk, barY, barH, C_SUBTEXT);
  }
}

static void _drawAll() {
  tft.fillScreen(C_BG);
  drawHeader("Tools", true);
  char briLabel[24];
  snprintf(briLabel, sizeof(briLabel), "BRIGHTNESS: %d%%  ",
           map(_brightness, 0, 255, 0, 100));
  char toLabel[28];
  snprintf(toLabel, sizeof(toLabel), "TIMEOUT: %s     ", _timeoutLabels[_timeoutIdx]);
  struct { const char* label; uint16_t color; } items[BTN_COUNT] = {
    { _inverted ? "INVERT: ON      " : "INVERT: OFF     ", (uint16_t)(_inverted ? C_ACCENT : C_TEXT) },
    { briLabel,            C_TEXT   },
    { "SPEAKER TEST    ",  C_ACCENT2 },
    { toLabel,             C_TEXT   },
    { "RECALIBRATE TOUCH", C_ACCENT2 },
    { "SECURITY GUIDE  ",  C_ACCENT  },
    { "TESTING GUIDE   ",  C_ACCENT  },
  };
  for (int i = 0; i < BTN_COUNT; i++) {
    int y = _btnY(i);
    if (y + BTN_H < LIST_Y || y > SCREEN_H) continue;
    Button b = { BTN_X, (int16_t)y, BTN_W, BTN_H, items[i].label, C_BTN, items[i].color };
    drawButton(b);
  }

  _drawBrightnessBar();

  if (TOTAL_H > VISIBLE_H) {
    int trackH = VISIBLE_H;
    int thumbH = max(16, trackH * VISIBLE_H / TOTAL_H);
    int maxScr = TOTAL_H - VISIBLE_H;
    int thumbY = LIST_Y + (trackH - thumbH) * _scrollY / maxScr;
    tft.fillRect(SCREEN_W - 4, LIST_Y, 4, trackH, C_PANEL);
    tft.fillRect(SCREEN_W - 4, thumbY, 4, thumbH, C_ACCENT);
  }
}

static void _speakerTest() {
  int freqs[] = { 800, 1200, 1800 };
  for (int f = 0; f < 3; f++) {
    int halfPeriod = 500000 / freqs[f];
    for (int i = 0; i < 100; i++) {
      digitalWrite(SPEAKER_PIN, HIGH);
      delayMicroseconds(halfPeriod);
      digitalWrite(SPEAKER_PIN, LOW);
      delayMicroseconds(halfPeriod);
    }
    delay(80);
  }
}

// scrolling text guide
static void _scrollGuide(const char* title, const char* const* lines, int totalLines) {
  const int rowH        = 14;
  const int listY       = HEADER_H + 4;
  const int visibleRows = (SCREEN_H - listY) / rowH;
  int scrollOffset      = 0;
  int maxScroll         = max(0, totalLines - visibleRows);
  int16_t touchStartY   = -1, lastTouchY = -1;
  bool touching         = false;

  auto drawPage = [&]() {
    tft.fillScreen(C_BG);
    drawHeader(title, true);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    for (int i = 0; i < visibleRows && (scrollOffset + i) < totalLines; i++) {
      const char* line = lines[scrollOffset + i];
      uint16_t color = C_TEXT;
      if      (line[0] == '=')                    color = C_ACCENT;
      else if (line[0] == '-')                    color = C_SUBTEXT;
      else if (strncmp(line, "WEP", 3) == 0 ||
               strncmp(line, "WPA", 3) == 0 ||
               strncmp(line, "OPEN", 4) == 0)     color = C_YELLOW;
      else if (strncmp(line, "  192", 5) == 0)    color = C_CYAN;
      else if (strncmp(line, "LEGAL", 5) == 0 ||
               strncmp(line, "Stay", 4) == 0)     color = C_RED;
      tft.setTextColor(color);
      tft.drawString(line, 6, listY + i * rowH);
    }
    if (maxScroll > 0) {
      int barH   = SCREEN_H - listY;
      int thumbH = max(12, barH * visibleRows / totalLines);
      int thumbY = listY + (barH - thumbH) * scrollOffset / maxScroll;
      tft.fillRect(SCREEN_W - 4, listY, 4, barH, C_PANEL);
      tft.fillRect(SCREEN_W - 4, thumbY, 4, thumbH, C_ACCENT);
    }
  };

  drawPage();
  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      if (!touching) { touchStartY = ty; lastTouchY = ty; touching = true; }
      else {
        int dy = lastTouchY - ty;
        if (abs(dy) >= 8) {
          scrollOffset = constrain(scrollOffset + dy / 8, 0, maxScroll);
          lastTouchY = ty;
          drawPage();
        }
      }
    } else {
      if (touching) {
        if (abs(touchStartY - lastTouchY) < 10 && backButtonHit(tx, ty)) return;
        touching = false;
      }
    }
    delay(16);
  }
}

static void _securityGuide() {
  static const char* lines[] = {
    "== WIFI SECURITY GUIDE ==", "",
    "WHAT IS A DEAUTH ATTACK?", "------------------------",
    "A deauth attack sends fake", "disconnect packets to kick",
    "devices off your network.", "When your device reconnects",
    "it performs a WPA handshake", "-- and the attacker grabs it.", "",
    "With that handshake they can", "brute-force your WiFi password",
    "offline from their car.", "",
    "The Traffic Monitor in this", "app can help detect unusual",
    "packet spikes on your channel.", "",
    "== HOW TO PROTECT YOURSELF ==", "",
    "STEP 1: LOG INTO YOUR ROUTER", "----------------------------",
    "Open a browser and go to:", "", "  192.168.1.1", "  or 192.168.0.1", "",
    "Login with admin credentials.", "Change defaults if you haven't.", "",
    "STEP 2: DISABLE WPS", "-------------------",
    "WPS PIN can be brute-forced.", "Turn it OFF. Use your password.", "",
    "STEP 3: UPGRADE TO WPA3", "-----------------------",
    "WEP  -- broken, never use", "WPA  -- broken, never use",
    "WPA2 -- handshake capturable", "WPA3 -- best, use this", "",
    "STEP 4: OTHER QUICK WINS", "------------------------",
    "WIFI PASSWORD -- 16+ chars", "ADMIN PASSWORD -- change it",
    "REMOTE MGMT -- turn OFF", "FIRMWARE -- keep updated",
    "GUEST NET -- isolate IoT", "",
    "Stay paranoid. Stay patched.", "=========================="
  };
  _scrollGuide("Security Guide", lines, sizeof(lines)/sizeof(lines[0]));
}

static void _testingGuide() {
  static const char* lines[] = {
    "===================================",
    "BYTESHIELD DC-01 // TRAINING GUIDE",
    "Setting Up a Safe Test Network",
    "===================================",
    "WHY YOU NEED A TEST NETWORK", "----------------------------",
    "Practice on a network you own,", "never on someone else's.", " ",
    "OPTION 1 - DEDICATED TEST ROUTER", "---------------------------------",
    "A cheap second router, its own", "SSID, on a different channel.", " ",
    "OPTION 2 - GUEST NETWORK", "-------------------------",
    "Most routers have this built in.", "Takes about 5 minutes.", " ",
    "Log into your router admin panel:", "  192.168.0.1", "  192.168.1.1", " ",
    "CONFIGURING YOUR TEST NETWORK", "------------------------------",
    "Name it obviously:", "  DC-01-TARGET", "  BYTESHIELD-TEST", " ",
    "Test against each security level:", " ",
    "OPEN - no password", "WEP  - ancient, cracks fast",
    "WPA  - older, dictionary-weak", "WPA2 - current standard",
    "WPA3 - newest, hardest", " ",
    "WHAT TO PRACTICE", "-----------------",
    "LEASH LOCK DETECTION", "Send deauth frames at your test",
    "net and watch the indicator trip.", " ",
    "TRAFFIC MONITOR", "Watch the channel activity graph",
    "and learn normal patterns.", " ",
    "LEGAL REMINDER", "---------------",
    "Only test networks you own or", "have written permission for.",
    "Deauth/scan on others is illegal.", " ",
    "Stay legal. Stay curious.",
    "===================================",
    "DC-01 // DUGGIE TECH", "duggietech.com",
    "==================================="
  };
  _scrollGuide("Testing Guide", lines, sizeof(lines)/sizeof(lines[0]));
}

void toolsMenu() {
  _scrollY = 0;
  _lastActivity = millis();
  _drawAll();

  int16_t touchStartY = -1, lastTouchY = -1;
  bool    touching = false;
  int     touchStartScroll = 0;

  while (true) {
    if (_timeouts[_timeoutIdx] > 0 &&
        millis() - _lastActivity > _timeouts[_timeoutIdx]) {
      analogWrite(TFT_BL_PIN, 0);
      int16_t wx, wy;
      while (!tftTouched(wx, wy)) delay(50);
      waitTouchRelease();
      analogWrite(TFT_BL_PIN, _brightness);
      _lastActivity = millis();
      continue;
    }

    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      _lastActivity = millis();
      if (!touching) {
        touchStartY = ty; lastTouchY = ty;
        touchStartScroll = _scrollY; touching = true;
      } else {
        int dy = touchStartY - ty;
        int maxScroll = max(0, TOTAL_H - VISIBLE_H);
        int newScroll = constrain(touchStartScroll + dy, 0, maxScroll);
        if (newScroll != _scrollY) { _scrollY = newScroll; _drawAll(); }
        lastTouchY = ty;
      }
    } else {
      if (touching) {
        touching = false;
        int dragDist = abs(touchStartY - lastTouchY);
        if (dragDist < 8) {
          if (backButtonHit(tx, ty)) return;
          for (int i = 0; i < BTN_COUNT; i++) {
            int y = _btnY(i);
            if (tx >= BTN_X && tx < BTN_X + BTN_W && ty >= y && ty < y + BTN_H) {
              switch (i) {
                case 0:  // Invert
                  _inverted = !_inverted;
                  tft.invertDisplay(_inverted);
                  break;
                case 1:  // Brightness — left half down, right half up
                  if (tx < SCREEN_W / 2)
                    _brightness = (uint8_t)max(0,   (int)_brightness - 25);
                  else
                    _brightness = (uint8_t)min(255, (int)_brightness + 25);
                  analogWrite(TFT_BL_PIN, _brightness);
                  break;
                case 2: {  // Speaker test
                  Button spkActive = { BTN_X, (int16_t)_btnY(2), BTN_W, BTN_H,
                                       "  >> BEEPING <<  ", C_BTN, C_ACCENT2 };
                  drawButton(spkActive);
                  _speakerTest();
                  break;
                }
                case 3:  // Timeout
                  _timeoutIdx = (_timeoutIdx + 1) % 4;
                  break;
                case 4: {  // Recalibrate touc wipe cal reboot
                  tft.fillScreen(C_BG);
                  drawHeader("Recalibrate", true);
                  tft.setFreeFont(NULL); tft.setTextSize(1);
                  tft.setTextColor(C_ACCENT); tft.setTextDatum(MC_DATUM);
                  tft.drawString("Clearing calibration...", SCREEN_W/2, SCREEN_H/2 - 10);
                  tft.drawString("Rebooting to recalibrate.", SCREEN_W/2, SCREEN_H/2 + 8);
                  tft.setTextDatum(TL_DATUM);
                  touchResetCalibration();
                  delay(1200);
                  ESP.restart();
                  break;
                }
                case 5:  _securityGuide(); break;
                case 6:  _testingGuide();  break;
              }
              _drawAll();
              break;
            }
          }
        }
      }
    }
    delay(16);
  }
}