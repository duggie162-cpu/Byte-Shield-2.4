#include "about_menu24.h"
#include "ui24.h"
#include "touch24.h"
#include "config24.h"
#include "bs_ui.h"
#include "logo.h"
#include "Orbitron_Bold_12.h"
#include <string.h>

extern TFT_eSPI tft;

static const char* TICKER =
  "  BS-2.4  //  BYTE_SHIELD  //  DIGITAL BLUE TEAM DEFENSE TOOL  //  "
  "BY DUGGIE TECH // GH0ST3CH Deauth Scan concept // BKbroiler_Chameleon_Roku ideas // Wolf // Ghost // "
  "WIFI  //  BLE  //  IR  //  RFID  //  LEASHLOCK  //  ";

void aboutMenu() {
  tft.fillScreen(C_BG);
  drawHeader("About", true);

  int logoX = (SCREEN_W - LOGO_W) / 2;
  int logoY = HEADER_H + 6;
  tft.pushImage(logoX, logoY, LOGO_W, LOGO_H, byteshield_logo);
  int divY = logoY + LOGO_H + 4;
  bsui::taperedLine(tft, 20, divY, SCREEN_W - 40, C_SUBTEXT, C_ACCENT, C_TEXT);
  tft.setFreeFont(&Orbitron_Bold_12);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.drawString("BYTE_SHIELD", SCREEN_W/2, divY + 14);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(C_ACCENT);
  tft.drawString("BS-2.4  //  DIGITAL BLUE TEAM", SCREEN_W/2, divY + 28);
  tft.setTextColor(C_SUBTEXT);
  tft.drawString(APP_VERSION "  //  " APP_BOARD, SCREEN_W/2, divY + 42);
  tft.drawFastHLine(20, divY+52, SCREEN_W - 40, C_SUBTEXT);
  tft.setFreeFont(&Orbitron_Bold_12);
  tft.setTextColor(C_ACCENT2);
  tft.drawString("DUGGIE TECH", SCREEN_W/2, divY + 66);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(C_SUBTEXT);
  tft.drawString("duggietech.com", SCREEN_W/2, divY + 80);
  tft.setTextDatum(TL_DATUM);
  bsui::cornerBrackets(tft, 3, 3, SCREEN_W - 6, SCREEN_H - 6, C_ACCENT, 10);
  
  int tickerY  = SCREEN_H - 16;
  int tickerH  = 14;
  int textLen  = strlen(TICKER);
  int pixelLen = textLen * 6;
  int scrollX  = SCREEN_W;

  tft.drawFastHLine(0, tickerY - 2, SCREEN_W, C_SUBTEXT);
  tft.fillRect(0, tickerY, SCREEN_W, tickerH, C_PANEL);

  unsigned long lastScroll = millis();
  int16_t tx, ty;

  while (true) {
    if (millis() - lastScroll >= 30) {
      lastScroll = millis();
      tft.fillRect(0, tickerY, SCREEN_W, tickerH, C_PANEL);
      tft.setFreeFont(NULL);
      tft.setTextSize(1);
      tft.setTextColor(C_CYAN);
      int x = scrollX;
      tft.drawString(TICKER, x, tickerY + 3);
      tft.drawString(TICKER, x + pixelLen, tickerY + 3);
      scrollX--;
      if (scrollX <= -pixelLen) scrollX = 0;
    }
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return;
    }
  }
}