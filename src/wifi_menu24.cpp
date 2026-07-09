#include "wifi_menu24.h"
#include "ui24.h"
#include "touch24.h"
#include "config24.h"
#include "sd_utils24.h"
#include "wifi_evil_portal24.h"
#include "roku_remote24.h"
#include "wifi_deauth_scan24.h"
#include "leashlock.h"
#include <WiFi.h>
#include "esp_wifi.h"
extern TFT_eSPI tft;
static volatile uint32_t pktCount = 0;
static void IRAM_ATTR trafficSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  pktCount++;
}
static void wifiScanAPs();
static void wifiTrafficMonitor();

void wifiMenu(int* outChannel) {
  while (true) {
    tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
    tft.fillScreen(C_BG);
    drawHeader("WiFi", true);

    char wdLabel[24];
    snprintf(wdLabel, sizeof(wdLabel), "Watchdog: %s",
             leashlockIsEnabled() ? "ON" : "OFF");
    const char* labels[6] = { "Scan APs", "Traffic Monitor", "Deauth Scanner", wdLabel, "Evil Portal", "Roku Remote" };
    const int btnW      = SCREEN_W - 20;
    const int btnH      = 45;
    const int btnPad    = 10;
    const int totalBtns = 6;
    const int visibleH  = SCREEN_H - HEADER_H - 20;
    const int strideH   = btnH + btnPad;
    const int maxScroll = max(0, totalBtns * strideH - visibleH);
    static int wifiScrollTop = 0;

    auto drawMenuBtns = [&]() {
      tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, C_BG);
      for (int i = 0; i < totalBtns; i++) {
        int y = HEADER_H + 10 + i * strideH - wifiScrollTop;
        if (y + btnH < HEADER_H + 4 || y > SCREEN_H) continue;
        if (y < HEADER_H + 4) continue;
        uint16_t txtCol = C_TEXT;
        if (i == 3) txtCol = leashlockIsEnabled() ? C_ACCENT : C_SUBTEXT;
        Button b = { 10, (int16_t)y, (int16_t)btnW, (int16_t)btnH, labels[i], C_BTN, txtCol };
        drawButton(b);
      }
      drawHeader("WiFi", true);
      if (maxScroll > 0) {
        int trackH = SCREEN_H - HEADER_H;
        int thumbH = max(16, trackH * visibleH / (totalBtns * strideH));
        int thumbY = HEADER_H + (trackH - thumbH) * wifiScrollTop / maxScroll;
        tft.fillRect(SCREEN_W - 4, HEADER_H, 4, trackH, C_PANEL);
        tft.fillRect(SCREEN_W - 4, thumbY, 4, thumbH, C_ACCENT);
      }
    };

    drawMenuBtns();
    bool exitMenu = false, redraw = false;
    int16_t wifiTouchStartY = -1;
    bool wifiTouching = false;
    int wifiScrollAtTouch = 0;
    while (!exitMenu && !redraw) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) {
        if (!wifiTouching) {
          wifiTouching = true;
          wifiTouchStartY = ty;
          wifiScrollAtTouch = wifiScrollTop;
        } else {
          int drag = wifiTouchStartY - ty;
          int newScroll = constrain(wifiScrollAtTouch + drag / strideH * strideH,
                                    0, maxScroll);
          if (newScroll != wifiScrollTop) {
            wifiScrollTop = newScroll;
            drawMenuBtns();
          }
        }
      } else {
        if (wifiTouching) {
          wifiTouching = false;
          int drag = abs(wifiTouchStartY - ty);
          if (drag < 8) {
            if (backButtonHit(tx, ty)) { exitMenu = true; break; }
            for (int i = 0; i < totalBtns; i++) {
              int y = HEADER_H + 10 + i * strideH - wifiScrollTop;
              if (tx >= 10 && tx < 10 + btnW && ty >= y && ty < y + btnH) {
                uint16_t txtCol = C_TEXT;
                if (i == 3) txtCol = leashlockIsEnabled() ? C_ACCENT : C_SUBTEXT;
                Button b = { 10, (int16_t)y, (int16_t)btnW, (int16_t)btnH, labels[i], C_BTN, txtCol };
                drawButton(b, true); delay(60);
                if      (i == 0) wifiScanAPs();
                else if (i == 1) wifiTrafficMonitor();
                else if (i == 2) wifiDeauthScanner();
                else if (i == 4) wifiEvilPortalMenu();
                else if (i == 5) rokuRemoteMenu();
                else {
                  bool nowEnabled = !leashlockIsEnabled();
                  leashlockSetEnabled(nowEnabled);
                  if (!nowEnabled) leashlockStop();
                  else if (WiFi.isConnected()) leashlockStart(WiFi.channel());
                }
                redraw = true; break;
              }
            }
          }
        }
      }
    }
    if (exitMenu) {
      if (outChannel) *outChannel = WiFi.isConnected() ? WiFi.channel() : 0;
      return;
    }
  }
}

static void wifiScanAPs() {
  tft.fillScreen(C_BG);
  drawHeader("Scan APs", true);
  tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_ACCENT);
  tft.drawString("Scanning...", 10, HEADER_H + 10);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  delay(200);
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    WiFi.mode(WIFI_OFF); delay(150);
    WiFi.mode(WIFI_STA); delay(100);
    n = WiFi.scanNetworks();
  }
  tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H, C_BG);
  tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  if (n <= 0) {
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("No networks found", 10, HEADER_H + 20);
    while (true) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) { waitTouchRelease();
        if (backButtonHit(tx, ty)) { WiFi.scanDelete(); return; } }
    }
  }

  tft.setTextColor(C_ACCENT);
  char hdr[32]; snprintf(hdr, sizeof(hdr), "%d networks found", n);
  tft.drawString(hdr, 10, HEADER_H + 5);
  const int maxRows = 10, rowH = 24, startY = HEADER_H + 22;
  int count = (n > maxRows) ? maxRows : n;
  String ssids[maxRows]; bool secured[maxRows];
  for (int i = 0; i < count; i++) {
    int y = startY + i * rowH;
    int rssi = WiFi.RSSI(i);
    bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    ssids[i] = WiFi.SSID(i); secured[i] = secure;
    tft.drawFastHLine(4, y + rowH - 2, SCREEN_W - 8, C_PANEL);
    uint16_t color = C_RED;
    if      (rssi > -60) color = C_ACCENT;
    else if (rssi > -75) color = C_YELLOW;
    tft.setTextColor(secure ? C_YELLOW : C_SUBTEXT);
    tft.drawString(secure ? "*" : " ", 6, y + 4);
    tft.setTextColor(C_TEXT);
    String ssid = ssids[i];
    if (ssid.length() > 22) ssid = ssid.substring(0, 22);
    tft.drawString(ssid.c_str(), 16, y + 4);
    char rssiStr[8]; snprintf(rssiStr, sizeof(rssiStr), "%d", rssi);
    tft.setTextColor(color);
    tft.drawString(rssiStr, SCREEN_W - 28, y + 4);
  }

  String savedSSID, savedPass;
  bool hasSaved = loadWifiConfig(savedSSID, savedPass);
  if (hasSaved) {
    tft.setTextColor(C_SUBTEXT);
    String hint = "saved: " + savedSSID;
    if (hint.length() > 30) hint = hint.substring(0, 29) + "~";
    tft.drawString(hint.c_str(), 6, SCREEN_H - 12);
  }

  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) { WiFi.scanDelete(); return; }
      if (ty >= startY && ty < startY + count * rowH) {
        int row = (ty - startY) / rowH;
        if (row >= 0 && row < count) {
          char password[64]; password[0] = 0; bool gotPw = false;
          if (hasSaved && ssids[row] == savedSSID) {
            strncpy(password, savedPass.c_str(), sizeof(password) - 1);
            password[sizeof(password) - 1] = 0; gotPw = true;
          } else if (secured[row]) {
            char prompt[40]; String s = ssids[row];
            if (s.length() > 16) s = s.substring(0, 16);
            snprintf(prompt, sizeof(prompt), "PW: %s", s.c_str());
            gotPw = onScreenKeyboard(prompt, password, sizeof(password));
            if (!gotPw) { WiFi.scanDelete(); return; }
          } else gotPw = true;
          tft.fillScreen(C_BG);
          drawHeader("Connecting", true);
          tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
          tft.setTextColor(C_ACCENT); tft.drawString("SSID:", 10, HEADER_H + 10);
          tft.setTextColor(C_TEXT);
          String s = ssids[row]; if (s.length() > 30) s = s.substring(0, 30);
          tft.drawString(s.c_str(), 50, HEADER_H + 10);
          tft.setTextColor(C_SUBTEXT); tft.drawString("Connecting...", 10, HEADER_H + 30);
          WiFi.scanDelete();
          WiFi.begin(ssids[row].c_str(), password);
          unsigned long start = millis(); bool connected = false;
          while (millis() - start < 15000) {
            if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
            delay(200);
          }

          tft.fillRect(0, HEADER_H + 50, SCREEN_W, 120, C_BG);
          if (connected) {
            tft.setTextColor(C_ACCENT); tft.setTextSize(2);
            tft.drawString("Connected!", 10, HEADER_H + 50);
            tft.setTextColor(C_TEXT); tft.setTextSize(1);
            tft.drawString("IP:", 10, HEADER_H + 85);
            tft.drawString(WiFi.localIP().toString().c_str(), 30, HEADER_H + 85);
            char chStr[24];
            snprintf(chStr, sizeof(chStr), "CH:%d  RSSI:%d", WiFi.channel(), WiFi.RSSI());
            tft.drawString(chStr, 10, HEADER_H + 100);
            saveWifiConfig(ssids[row], String(password));
            tft.setTextColor(C_SUBTEXT);
            tft.drawString("Credentials saved to SD.", 10, HEADER_H + 118);
          } else {
            tft.setTextColor(C_RED); tft.setTextSize(2);
            tft.drawString("Failed", 10, HEADER_H + 50);
            tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
            tft.drawString("Check password and signal.", 10, HEADER_H + 85);
            WiFi.disconnect();
          }
          while (true) {
            int16_t tx2, ty2;
            if (tftTouched(tx2, ty2)) { waitTouchRelease();
              if (backButtonHit(tx2, ty2)) return; }
          }
        }
      }
    }
  }
}

static void wifiTrafficMonitor() {
  const int chBarY = HEADER_H + 6, chBarH = 32;
  const int bigNumY = chBarY + chBarH + 6, bigNumH = 28;
  const int graphY = bigNumY + bigNumH + 6, graphX = 24;
  const int graphW = SCREEN_W - graphX - 4, graphH = SCREEN_H - graphY - 8;
  const int minusX = 6, ctrlW = 36;
  const int plusX = SCREEN_W - 6 - ctrlW;
  const int chLabelX = ctrlW + 14, chLabelW = SCREEN_W - 2 * (ctrlW + 14);
  int channel = WiFi.isConnected() ? WiFi.channel() : 1;
  uint32_t lastSweep = 0, lastPkt = 0;
  bool needFullRedraw = true;
  static uint32_t samples[320];
  int samplesLen = graphW;
  for (int i = 0; i < samplesLen; i++) samples[i] = 0;
  int sampleHead = 0; uint32_t yMax = 50;

  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_filter(NULL);
  esp_wifi_set_promiscuous_rx_cb(&trafficSniffer);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  pktCount = 0;
  auto ppsColor = [](uint32_t pps) -> uint16_t {
    if (pps < 5)   return C_SUBTEXT;
    if (pps < 20)  return C_ACCENT;
    if (pps < 60)  return C_YELLOW;
    if (pps < 200) return tft.color565(255, 140, 0);
    return C_RED;
  };

  auto drawChrome = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("Traffic Monitor", true);
    tft.fillRoundRect(minusX, chBarY, ctrlW, chBarH, 4, C_BTN);
    tft.drawRoundRect(minusX, chBarY, ctrlW, chBarH, 4, C_SUBTEXT);
    tft.setTextColor(C_ACCENT); tft.setTextSize(2);
    tft.drawString("-", minusX + ctrlW/2 - 5, chBarY + chBarH/2 - 7);
    tft.fillRoundRect(plusX, chBarY, ctrlW, chBarH, 4, C_BTN);
    tft.drawRoundRect(plusX, chBarY, ctrlW, chBarH, 4, C_SUBTEXT);
    tft.setTextColor(C_ACCENT);
    tft.drawString("+", plusX + ctrlW/2 - 5, chBarY + chBarH/2 - 7);
    tft.fillRect(chLabelX, chBarY, chLabelW, chBarH, C_PANEL);
    tft.drawRect(chLabelX, chBarY, chLabelW, chBarH, C_SUBTEXT);
    char ch[12]; snprintf(ch, sizeof(ch), "CH %d", channel);
    tft.setTextColor(C_TEXT); tft.setTextSize(2);
    int tw = strlen(ch) * 12;
    tft.drawString(ch, chLabelX + (chLabelW - tw) / 2, chBarY + chBarH/2 - 7);
    tft.drawRect(graphX - 1, graphY - 1, graphW + 2, graphH + 2, C_SUBTEXT);
  };

  auto drawBigNumber = [&](uint32_t pps, uint16_t color) {
    tft.fillRect(0, bigNumY, SCREEN_W, bigNumH, C_BG);
    char s[16]; snprintf(s, sizeof(s), "%lu", (unsigned long)pps);
    tft.setTextColor(color); tft.setTextSize(3);
    int w = strlen(s) * 18;
    tft.drawString(s, 8, bigNumY);
    tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
    tft.drawString("pkts/sec", 8 + w + 8, bigNumY + 14);
  };

  auto drawYAxis = [&]() {
    tft.fillRect(0, graphY, graphX - 1, graphH, C_BG);
    tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)yMax);
    tft.drawString(buf, 2, graphY);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(yMax / 2));
    tft.drawString(buf, 2, graphY + graphH/2 - 4);
    tft.drawString("0", 2, graphY + graphH - 8);
  };

  auto redrawGraph = [&]() {
    tft.fillRect(graphX, graphY, graphW, graphH, C_BG);
    tft.drawFastHLine(graphX, graphY + graphH/2, graphW, C_PANEL);
    tft.drawFastHLine(graphX, graphY + graphH/4, graphW, C_PANEL);
    tft.drawFastHLine(graphX, graphY + (graphH*3)/4, graphW, C_PANEL);
    int prevY = -1;
    for (int i = 0; i < samplesLen; i++) {
      int idx = (sampleHead + i) % samplesLen;
      uint32_t v = samples[idx];
      int yPx = graphY + graphH - 1 - (int)((uint64_t)v * (graphH - 1) / (yMax ? yMax : 1));
      if (yPx < graphY) yPx = graphY;
      if (yPx > graphY + graphH - 1) yPx = graphY + graphH - 1;
      int xPx = graphX + i;
      uint16_t color = ppsColor(v);
      if (prevY >= 0) tft.drawLine(xPx - 1, prevY, xPx, yPx, color);
      else            tft.drawPixel(xPx, yPx, color);
      prevY = yPx;
    }
  };

  drawChrome(); drawBigNumber(0, C_SUBTEXT); drawYAxis(); redrawGraph();
  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) {
        esp_wifi_set_promiscuous(false);
        WiFi.disconnect();
        return;
      }
      if (tx >= minusX && tx < minusX + ctrlW && ty >= chBarY && ty < chBarY + chBarH) {
        if (channel > 1) channel--;
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        pktCount = 0;
        for (int i = 0; i < samplesLen; i++) samples[i] = 0;
        yMax = 50; needFullRedraw = true;
      }
      if (tx >= plusX && tx < plusX + ctrlW && ty >= chBarY && ty < chBarY + chBarH) {
        if (channel < 13) channel++;
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        pktCount = 0;
        for (int i = 0; i < samplesLen; i++) samples[i] = 0;
        yMax = 50; needFullRedraw = true;
      }
    }

    if (needFullRedraw) {
      drawChrome(); drawBigNumber(0, C_SUBTEXT); drawYAxis(); redrawGraph();
      needFullRedraw = false; lastSweep = millis(); lastPkt = 0;
      continue;
    }

    if (millis() - lastSweep >= 250) {
      uint32_t now = pktCount;
      uint32_t delta = now - lastPkt;
      lastPkt = now; lastSweep = millis();
      uint32_t pps = delta * 4;
      samples[sampleHead] = pps;
      sampleHead = (sampleHead + 1) % samplesLen;
      uint32_t peak = 0;
      for (int i = 0; i < samplesLen; i++) if (samples[i] > peak) peak = samples[i];
      uint32_t newMax = peak < 10 ? 10 : peak < 50 ? 50 : peak < 100 ? 100 :
                        peak < 250 ? 250 : peak < 500 ? 500 : peak < 1000 ? 1000 :
                        peak < 2500 ? 2500 : peak < 5000 ? 5000 : 10000;
      bool axisChanged = (newMax != yMax);
      yMax = newMax;
      drawBigNumber(pps, ppsColor(pps));
      if (axisChanged) drawYAxis();
      redrawGraph();
    }
    delay(10);
  }
}