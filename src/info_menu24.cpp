#include "info_menu24.h"
#include "ui24.h"
#include "touch24.h"
#include "config24.h"
#include "leashlock.h"
#include "sd_utils24.h"
#include "esp_system.h"
#include <SD.h>
#include <WiFi.h>
extern TFT_eSPI tft;
#define ROW_X      10
#define VAL_X      130
#define ROW_START  (HEADER_H + 10)
#define ROW_H      18

static void _drawRow(int row, const char* label, const char* value, uint16_t valColor) {
  int y = ROW_START + row * ROW_H;
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_SUBTEXT);
  tft.drawString(label, ROW_X, y);
  tft.setTextColor(valColor);
  tft.drawString(value, VAL_X, y);
}

static void _drawDivider(int row) {
  int y = ROW_START + row * ROW_H - 4;
  tft.drawFastHLine(ROW_X, y, SCREEN_W - ROW_X * 2, C_SUBTEXT);
}

void infoMenu() {
  char buf[32];

  while (true) {
    tft.fillScreen(C_BG);
    drawHeader("Info", true);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_ACCENT);
    tft.drawString("// SYSTEM", ROW_X, ROW_START);

    uint32_t heap = esp_get_free_heap_size();
    snprintf(buf, sizeof(buf), "%u KB", heap / 1024);
    _drawRow(1, "FREE RAM :", buf, heap > 50000 ? C_GREEN : C_ACCENT2);
    snprintf(buf, sizeof(buf), "%u MHz", ESP.getCpuFreqMHz());
    _drawRow(2, "CPU FREQ :", buf, C_TEXT);
    snprintf(buf, sizeof(buf), "%u MB", ESP.getFlashChipSize() / (1024 * 1024));
    _drawRow(3, "FLASH    :", buf, C_TEXT);

    unsigned long s = millis() / 1000;
    unsigned long m = s / 60; s %= 60;
    unsigned long h = m / 60; m %= 60;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
    _drawRow(4, "UPTIME   :", buf, C_CYAN);

    // LeashLock
    _drawDivider(6);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(C_ACCENT);
    tft.drawString("// LEASHLOCK", ROW_X, ROW_START + 5 * ROW_H);

    snprintf(buf, sizeof(buf), "%s", leashlockIsRunning() ? "ACTIVE" : "STOPPED");
    _drawRow(6, "STATUS   :", buf, leashlockIsRunning() ? C_GREEN : C_RED);
    snprintf(buf, sizeof(buf), "CH %d", leashlockChannel());
    _drawRow(7, "CHANNEL  :", buf, C_CYAN);
    snprintf(buf, sizeof(buf), "%d pkts", leashlockTotalPackets());
    _drawRow(8, "PKT COUNT:", buf, leashlockAlertActive() ? C_RED : C_TEXT);
    snprintf(buf, sizeof(buf), leashlockAlertActive() ? "!! DETECTED !!" : "CLEAN");
    _drawRow(9, "THREAT   :", buf, leashlockAlertActive() ? C_RED : C_GREEN);

    // Storage
    _drawDivider(11);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(C_ACCENT);
    tft.drawString("// STORAGE", ROW_X, ROW_START + 10 * ROW_H);

    if (sdReady()) {
      uint64_t total = SD.totalBytes() / (1024 * 1024);
      uint64_t used  = SD.usedBytes()  / (1024 * 1024);
      if (total > 0) {
        snprintf(buf, sizeof(buf), "%lluMB / %lluMB", used, total);
        _drawRow(11, "SD CARD  :", buf, C_TEXT);
      } else {
        _drawRow(11, "SD CARD  :", "NO CARD", C_ACCENT2);
      }
    } else {
      _drawRow(11, "SD CARD  :", "NO CARD", C_ACCENT2);
    }
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("[ AUTO-REFRESH  //  BACK TO EXIT ]", ROW_X, SCREEN_H - 22);
    unsigned long drawTime = millis();
    while (millis() - drawTime < 2000) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) {
        waitTouchRelease();
        if (backButtonHit(tx, ty)) return;
      }
      delay(20);
    }
  }
}