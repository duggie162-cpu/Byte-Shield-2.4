#include "ui24.h"
#include "touch24.h"
#include "bs_ui.h"            
#include "leashlock.h"        
#include "Free_Fonts.h"
#include "Orbitron_Bold_12.h"
#include "Orbitron_Bold_18.h"
#include "logo.h"
#include "logo_bg.h"
#include <string.h>

//  Boot screen
void showBootScreen() {
  tft.fillScreen(C_BG);

  // Outer frame brackets try rounded corners to keep consisitant with the rest of the UI
  bsui::cornerBrackets(tft, 3, 3, SCREEN_W - 6, SCREEN_H - 6, C_SUBTEXT, 12);

  // Logo, centered
  int logoX = (SCREEN_W - LOGO_W) / 2;
  int logoY = 10;
  tft.pushImage(logoX, logoY, LOGO_W, LOGO_H, byteshield_logo);

  int divY = logoY + LOGO_H + 6;
  bsui::taperedLine(tft, 20, divY, SCREEN_W - 40, C_SUBTEXT, C_ACCENT, C_TEXT);

  // Title
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setFreeFont(NULL);
  tft.setTextSize(2);
  tft.drawString("BYTE_SHIELD", SCREEN_W / 2, divY + 18);

  // Subtitle
  tft.setTextSize(1);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("BS-2.4 //  DIGITAL BLUE TEAM", SCREEN_W / 2, divY + 34);

  bsui::taperedLine(tft, 20, divY + 44, SCREEN_W - 40, C_SUBTEXT, C_ACCENT, C_TEXT);

  // System status
  tft.setTextColor(C_SUBTEXT, C_BG);
  tft.drawString("[ SYS OK  //  RF OK  //  SD OK ]", SCREEN_W / 2, divY + 58);

  // Progress bar
  int barY = divY + 72, barX = 50, barW = 140, barH = 4;
  tft.drawRect(barX, barY, barW, barH, C_SUBTEXT);
  tft.fillRect(barX - 4, barY - 2, 3, barH + 4, C_ACCENT);
  tft.fillRect(barX + barW + 1, barY - 2, 3, barH + 4, C_ACCENT);
  for (int i = 0; i <= barW - 2; i += 2) {
    tft.fillRect(barX + 1, barY + 1, i, barH - 2, C_ACCENT);
    delay(8);
  }

  // Version 
  tft.setTextColor(C_SUBTEXT, C_BG);
  tft.drawString("v0.2.3 //  ESP32-CYD", SCREEN_W / 2, barY + 16);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("[ TAP IT TO CONTINUE ]", SCREEN_W / 2, barY + 30);

  // Bottom status strip
  tft.fillRect(0, SCREEN_H - 15, SCREEN_W, 15, C_PANEL);
  tft.drawFastHLine(0, SCREEN_H - 15, SCREEN_W, C_SUBTEXT);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_SUBTEXT, C_PANEL);
  tft.drawString(" MEM OK", 0, SCREEN_H - 8);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("READY ", SCREEN_W, SCREEN_H - 8);
  tft.setTextDatum(MC_DATUM);
  tft.fillCircle(SCREEN_W / 2, SCREEN_H - 8, 2, C_ACCENT);

  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
}

//  Header
void drawHeader(const char* title, bool showBack) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, C_PANEL);

  // Left orange accent bar
  tft.fillRect(0, 0, 3, HEADER_H, C_ACCENT2);
  tft.fillRect(0, 4,              3, 2, TFT_BLACK);
  tft.fillRect(0, HEADER_H/2 - 1, 3, 2, TFT_BLACK);
  tft.fillRect(0, HEADER_H - 6,   3, 2, TFT_BLACK);

  // Bottom border
  tft.drawFastHLine(0,  HEADER_H,   SCREEN_W,    C_SUBTEXT);
  tft.drawFastHLine(3,  HEADER_H+1, SCREEN_W-3,  C_ACCENT);
  tft.drawFastHLine(20, HEADER_H+2, SCREEN_W-40, C_SUBTEXT);
  bsui::cornerBrackets(tft, 0, 0, SCREEN_W, HEADER_H, TFT_BLACK, 8);

  // Back button should i make touch area bigger?  maybe just make the whole left side a back button?
  if (showBack) {
    bsui::clippedButton(tft, 6, 5, 28, HEADER_H - 10, C_ACCENT, C_ACCENT);
    tft.setFreeFont(&Orbitron_Bold_12);
    tft.setTextColor(C_ACCENT);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("<", 20, HEADER_H/2 + 3);
  }

  // Title 
  tft.setFreeFont(&Orbitron_Bold_18);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(tft.color565(0, 80, 100));
  tft.drawString(title, SCREEN_W/2 + 1, HEADER_H/2 + 6);
  tft.drawString(title, SCREEN_W/2 - 1, HEADER_H/2 + 6);
  tft.setTextColor(C_TEXT);
  tft.drawString(title, SCREEN_W/2, HEADER_H/2 + 5);

  if (!showBack) {
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(TFT_ORANGE);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("DC-01", 6, HEADER_H - 6);
  }

  // SECURE tag — red if deauth alert active or watchdog disabled
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextDatum(TR_DATUM);
  bool secureAlert = leashlockAlertActive() || !leashlockIsEnabled();
  uint16_t secureColor = secureAlert ? C_RED : TFT_ORANGE;
  tft.setTextColor(secureColor);
  tft.drawString(secureAlert ? "NOT SECURE" : "SECURE", SCREEN_W - 6, 5);
  tft.fillCircle(SCREEN_W - 58, 9, 2, secureColor);

  // Version
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextDatum(BR_DATUM);
  tft.drawString(APP_VERSION, SCREEN_W - 4, HEADER_H - 1);

  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
}

bool backButtonHit(int16_t tx, int16_t ty) {
  return (tx >= 6 && tx <= 38 && ty >= 6 && ty <= HEADER_H - 6);
}

//  submenus
void drawButton(const Button& b, bool pressed) {
  tft.setTextDatum(TL_DATUM);
  uint16_t border = pressed ? C_TEXT : C_ACCENT;
  bsui::clippedButton(tft, b.x, b.y, b.w, b.h, border, C_ACCENT);
  tft.setFreeFont(&Orbitron_Bold_12);
  tft.setTextColor(b.textColor);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(b.label, b.x + b.w/2, b.y + b.h/2 + 3);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
}

//  Main 2x4 grid
#define STATUS_BAR_H  14
#define GRID_PAD       4

static Button _gridBtns[8];
static const char* _btnIdx[8] = {
  "[01]","[02]","[03]","[04]","[05]","[06]","[07]","[08]"
};

static void _drawGridBtn(int i, bool pressed) {
  tft.setTextDatum(TL_DATUM);
  Button& b = _gridBtns[i];
  int clip = 5;

  if (pressed) {
    bsui::clippedButton(tft, b.x, b.y, b.w, b.h, C_TEXT, C_ACCENT);
  } else {
    int x = b.x, y = b.y, w = b.w, h = b.h;
    tft.drawFastHLine(x+clip,    y,      w-clip*2, C_BG);
    tft.drawFastHLine(x+clip,    y+h-1,  w-clip*2, C_BG);
    tft.drawFastVLine(x,         y+clip, h-clip*2, C_BG);
    tft.drawFastVLine(x+w-1,     y+clip, h-clip*2, C_BG);
    tft.drawFastHLine(x+clip,    y,      w-clip*2, C_ACCENT);
    tft.drawFastHLine(x+clip,    y+h-1,  w-clip*2, C_ACCENT);
    tft.drawFastVLine(x,         y+clip, h-clip*2, C_ACCENT);
    tft.drawFastVLine(x+w-1,     y+clip, h-clip*2, C_ACCENT);
    tft.drawLine(x,          y+clip,     x+clip,   y,          C_ACCENT);
    tft.drawLine(x+w-clip-1, y,          x+w-1,    y+clip,     C_ACCENT);
    tft.drawLine(x,          y+h-clip-1, x+clip,   y+h-1,      C_ACCENT);
    tft.drawLine(x+w-clip-1, y+h-1,      x+w-1,    y+h-clip-1, C_ACCENT);
    tft.drawFastHLine(x+clip+2, y+2,   w-clip*2-4, tft.color565(0,40,50));
    tft.drawFastHLine(x+clip+2, y+h-3, w-clip*2-4, tft.color565(0,40,50));
  }

  // Number badge
  int badgeX = b.x + 4, badgeY = b.y + 3;
  tft.fillRect(badgeX, badgeY, 20, 10, TFT_BLACK);
  tft.drawFastVLine(badgeX, badgeY, 10, C_ACCENT);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(C_ACCENT);
  tft.setTextDatum(ML_DATUM);
  char numOnly[4] = { _btnIdx[i][1], _btnIdx[i][2], 0 };
  tft.drawString(numOnly, badgeX + 3, badgeY + 5);

  // Dark square behind label anyhting other looks really bad 
  tft.setFreeFont(&Orbitron_Bold_18);
  tft.setTextDatum(MC_DATUM);
  int labelW = tft.textWidth(b.label) + 10;
  int labelH = 18;
  int labelX = b.x + b.w/2 - labelW/2;
  int labelY = b.y + b.h/2 - 6;
  tft.fillRoundRect(labelX, labelY, labelW, labelH, 3, C_BG);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(b.label, b.x + b.w/2 + 1, b.y + b.h/2 + 6);
  tft.drawString(b.label, b.x + b.w/2 - 1, b.y + b.h/2 + 6);
  tft.setTextColor(pressed ? C_ACCENT : C_TEXT);
  tft.drawString(b.label, b.x + b.w/2, b.y + b.h/2 + 5);
}

void drawMainGrid(const char* labels[8]) {
  int gridTop = HEADER_H + GRID_PAD;
  int gridBot = SCREEN_H - STATUS_BAR_H - GRID_PAD;
  int availH  = gridBot - gridTop;
  int availW  = SCREEN_W - GRID_PAD * (GRID_COLS + 1);
  int bw      = availW / GRID_COLS;
  int bh      = (availH - GRID_PAD * (GRID_ROWS - 1)) / GRID_ROWS;

  for (int i = 0; i < 8; i++) {
    int col = i % GRID_COLS;
    int row = i / GRID_COLS;
    _gridBtns[i] = {
      (int16_t)(GRID_PAD + col * (bw + GRID_PAD)),
      (int16_t)(gridTop  + row * (bh + GRID_PAD)),
      (int16_t)bw, (int16_t)bh,
      labels[i], C_BTN, C_TEXT
    };
    _drawGridBtn(i, false);
  }

  // Status bar
  tft.fillRect(0, SCREEN_H - STATUS_BAR_H, SCREEN_W, STATUS_BAR_H, C_PANEL);
  tft.drawFastHLine(0, SCREEN_H - STATUS_BAR_H, SCREEN_W, C_SUBTEXT);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(C_SUBTEXT);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(" DC-01", 0, SCREEN_H - STATUS_BAR_H/2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("ARMED ", SCREEN_W, SCREEN_H - STATUS_BAR_H/2);
  tft.setTextDatum(MC_DATUM);
  tft.fillCircle(SCREEN_W/2, SCREEN_H - STATUS_BAR_H/2, 2, C_ACCENT);
  tft.setTextDatum(TL_DATUM);
}

int8_t mainGridHit(int16_t tx, int16_t ty) {
  for (int i = 0; i < 8; i++) {
    if (tx >= _gridBtns[i].x && tx < _gridBtns[i].x + _gridBtns[i].w &&
        ty >= _gridBtns[i].y && ty < _gridBtns[i].y + _gridBtns[i].h) {
      _drawGridBtn(i, true);
      delay(80);
      _drawGridBtn(i, false);
      return i;
    }
  }
  return -1;
}


//  LeashLock indicator whatchdog 
void leashlockDrawIndicator() {
  static bool _lastAlert = false;
  static unsigned long _lastBlink = 0;
  static bool _blinkState = false;

  bool alert = leashlockAlertActive();

  if (alert && millis() - _lastBlink > 500) {
    _blinkState = !_blinkState;
    _lastBlink = millis();
  }
  if (alert == _lastAlert && !alert) return;
  _lastAlert = alert;

  int barY = SCREEN_H - STATUS_BAR_H;
  tft.fillRect(80, barY + 1, SCREEN_W - 81, STATUS_BAR_H - 2, C_PANEL);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);

  if (alert) {
    uint16_t col = _blinkState ? C_ACCENT2 : C_RED;
    tft.setTextColor(col);
    tft.drawString("** DEAUTH ATTACK **", SCREEN_W/2 + 20, barY + STATUS_BAR_H/2);
    tft.fillCircle(SCREEN_W/2, barY + STATUS_BAR_H/2, 2, col);
  } else {
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("ARMED ", SCREEN_W - 10, barY + STATUS_BAR_H/2);
    tft.fillCircle(SCREEN_W/2, barY + STATUS_BAR_H/2, 2, C_ACCENT);
  }

  // Refresh SECURE tag in header to match current alert state
  tft.setFreeFont(NULL); tft.setTextSize(1);
  bool secureAlert = alert || !leashlockIsEnabled();
  uint16_t secureColor = secureAlert ? C_RED : TFT_ORANGE;
  // Erase old tag 
  tft.fillRect(SCREEN_W - 68, 2, 66, 14, C_PANEL);
  tft.setTextColor(secureColor);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(secureAlert ? "NOT SECURE" : "SECURE", SCREEN_W - 6, 5);
  tft.fillCircle(SCREEN_W - 62, 9, 2, secureColor);
  tft.setTextDatum(TL_DATUM);
}

void showToast(const char* msg, uint16_t color) {
  tft.fillRect(0, SCREEN_H - 20, SCREEN_W, 20, C_PANEL);
  tft.drawFastHLine(0, SCREEN_H - 20, SCREEN_W, color);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.drawString(msg, 6, SCREEN_H - 13);
}

//  Text viewer
void textViewer(const char* title, const char* body) {
  tft.fillScreen(C_BG);
  drawHeader(title, true);
  bsui::taperedLine(tft, 20, HEADER_H + 2, SCREEN_W - 40, C_SUBTEXT, C_ACCENT, C_TEXT);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(C_TEXT);
  tft.setTextWrap(true);
  tft.setCursor(6, HEADER_H + 10);
  tft.print(body);
  tft.setTextWrap(false);

  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return;
    }
  }
}

bool onScreenKeyboard(const char* prompt, char* buf, uint8_t bufLen) {
  const char* rows[5] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
    ",./?!@#$%&*"
  };
  const int keyW = 22, keyH = 26, padX = 2, kbY = HEADER_H + 55;
  tft.fillScreen(C_BG);
  drawHeader(prompt, true);

  // Input box
  tft.fillRect(4, HEADER_H + 4, SCREEN_W - 8, 22, C_PANEL);
  tft.drawRect(4, HEADER_H + 4, SCREEN_W - 8, 22, C_ACCENT);

  memset(buf, 0, bufLen);
  int len = 0;
  bool shifted = false;
  auto drawInput = [&]() {
    tft.fillRect(6, HEADER_H + 6, SCREEN_W - 12, 18, C_PANEL);
    tft.setFreeFont(NULL); tft.setTextSize(1);
    tft.setTextColor(C_TEXT);
    tft.drawString(buf, 8, HEADER_H + 8);
    int cx = 8 + len * 6;
    tft.drawFastVLine(cx, HEADER_H + 7, 14, C_ACCENT);
  };

  auto drawKeys = [&]() {
    tft.fillRect(0, kbY, SCREEN_W, SCREEN_H - kbY, C_BG);
    for (int r = 0; r < 5; r++) {
      const char* row = rows[r];
      int nk = strlen(row);
      int rowW = nk * (keyW + padX);
      int startX = (SCREEN_W - rowW) / 2;
      for (int k = 0; k < nk; k++) {
        int x = startX + k * (keyW + padX);
        int y = kbY + r * (keyH + 2);
        tft.fillRect(x, y, keyW, keyH, C_BTN);
        tft.drawRect(x, y, keyW, keyH, C_SUBTEXT);
        char label[2] = { (r < 4 && shifted) ? (char)toupper(row[k]) : row[k], 0 };
        tft.setTextColor(C_TEXT); tft.setTextSize(1);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(label, x + keyW / 2, y + keyH / 2);
        tft.setTextDatum(TL_DATUM);
      }
    }
    // Special keys row Irish Ghost 
    int specY = kbY + 5 * (keyH + 2);
    // SHFT x:2 w:36
    tft.fillRect(2, specY, 36, keyH, shifted ? C_ACCENT : C_BTN);
    tft.drawRect(2, specY, 36, keyH, shifted ? C_ACCENT : C_SUBTEXT);
    tft.setTextColor(shifted ? C_BG : C_TEXT); tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SHF", 20, specY + keyH / 2);
    // SPACE x:42 w:80
    tft.fillRect(42, specY, 80, keyH, C_BTN);
    tft.drawRect(42, specY, 80, keyH, C_SUBTEXT);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("SPACE", 82, specY + keyH / 2);
    // DEL x:126 w:38
    tft.fillRect(126, specY, 38, keyH, C_BTN);
    tft.drawRect(126, specY, 38, keyH, C_RED);
    tft.setTextColor(C_RED);
    tft.drawString("DEL", 145, specY + keyH / 2);
    // OK x:168 w:70
    tft.fillRect(168, specY, 70, keyH, C_BTN_HL);
    tft.drawRect(168, specY, 70, keyH, C_ACCENT);
    tft.setTextColor(C_ACCENT);
    tft.drawString("OK", 203, specY + keyH / 2);
    tft.setTextDatum(TL_DATUM);
  };

  drawInput();
  drawKeys();

  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return false;

      int specY = kbY + 5 * (keyH + 2);

      // Special keys row
      if (ty >= specY && ty < specY + keyH) {
        if (tx >= 2 && tx < 38) {
          shifted = !shifted; drawKeys();
        } else if (tx >= 42 && tx < 122) {
          if (len < bufLen - 1) { buf[len++] = ' '; buf[len] = 0; drawInput(); }
        } else if (tx >= 126 && tx < 164) {
          if (len > 0) { buf[--len] = 0; drawInput(); }
        } else if (tx >= 168 && tx < 238) {
          return true;
        }
        continue;
      }

      // Character rows 0-4
      for (int r = 0; r < 5; r++) {
        const char* row = rows[r];
        int nk = strlen(row);
        int rowW = nk * (keyW + padX);
        int startX = (SCREEN_W - rowW) / 2;
        int y = kbY + r * (keyH + 2);
        if (ty >= y && ty < y + keyH) {
          int k = (tx - startX) / (keyW + padX);
          if (k >= 0 && k < nk) {
            char c = (r < 4 && shifted) ? (char)toupper(row[k]) : row[k];
            if (len < bufLen - 1) {
              buf[len++] = c;
              buf[len] = 0;
              drawInput();
              if (shifted) { shifted = false; drawKeys(); }
            }
          }
          break;
        }
      }
    }
  }
}