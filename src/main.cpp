//  ByteShield DC-01  —  2.4
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "config24.h"
#include "touch24.h"
#include "ui24.h"
#include "bs_hal.h"
#include "leashlock.h"
#include "sd_utils24.h"
#include "wifi_menu24.h"
#include "tools_menu24.h"
#include "info_menu24.h"
#include "about_menu24.h"
#include "gps_menu24.h"
#include "sd_menu24.h"
#include <NimBLEDevice.h>
#include "ble_menu24.h"
#include "logo_bg.h"

TFT_eSPI tft = TFT_eSPI();
uint8_t g_dispVariant = DISP_VARIANT_ILI;
uint16_t C_PANEL = 0x18C3;
uint16_t C_BTN   = 0x18C3;
uint8_t  GPS_RX_PIN = 35;
uint8_t  GPS_TX_PIN = 21;

static uint8_t detectAndSaveDisplayVariant() {
  Preferences prefs;
  prefs.begin(NVS_DISPLAY_NS, false);
  if (prefs.isKey(NVS_DISPLAY_KEY)) {
    uint8_t saved = prefs.getUChar(NVS_DISPLAY_KEY, DISP_VARIANT_ILI);
    prefs.end();
    Serial.printf("[DISP] Variant loaded from NVS: %d\n", saved);
    return saved;
  }
  prefs.end();

  // First boot — ask the user once, save forever
  tft.setRotation(0);
  tft.fillScreen(0x0000);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(0xFFFF);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SELECT YOUR BOARD TYPE", 120, 60);
  tft.setTextSize(1);
  tft.setTextColor(0x07FF);
  tft.drawString("Tap the button that matches", 120, 80);
  tft.drawString("your hardware. Saved forever.", 120, 92);

  // Button A — Standard
  tft.fillRoundRect(20, 120, 200, 50, 6, 0x18C3);
  tft.drawRoundRect(20, 120, 200, 50, 6, 0x073F);
  tft.setTextColor(0xFFFF);
  tft.drawString("STANDARD", 120, 138);
  tft.setTextSize(1);
  tft.setTextColor(0x0228);
  tft.drawString("Original CYD batch", 120, 152);

  // Button B — IPS
  tft.fillRoundRect(20, 190, 200, 50, 6, 0x18C3);
  tft.drawRoundRect(20, 190, 200, 50, 6, 0xFD40);
  tft.setTextColor(0xFFFF);
  tft.setTextSize(1);
  tft.drawString("IPS DISPLAY", 120, 208);
  tft.setTextColor(0x0228);
  tft.drawString("AITRIP / bright IPS screen", 120, 222);

  tft.setTextDatum(TL_DATUM);

  // Use BOOT button — no touch needed, works on both board types
  // Tap once = Standard ILI9341
  // Hold 2 seconds = IPS ST7789
  pinMode(0, INPUT_PULLUP);
  tft.fillScreen(0x0000);
  tft.setFreeFont(NULL); tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xFFFF);
  tft.drawString("PRESS BOOT BUTTON TO SELECT", 120, 100);
  tft.setTextColor(0x073F);
  tft.drawString("SHORT PRESS  =  Standard CYD", 120, 140);
  tft.setTextColor(0xFD40);
  tft.drawString("HOLD 2 SEC   =  IPS Display", 120, 160);
  tft.setTextColor(0x0228);
  tft.drawString("(BOOT button on edge of board)", 120, 190);
  tft.setTextDatum(TL_DATUM);

  // Wait for BOOT button press
  while (digitalRead(0) == HIGH) delay(20);  // wait for press
  uint32_t pressStart = millis();
  while (digitalRead(0) == LOW) delay(20);   // wait for release
  uint32_t holdMs = millis() - pressStart;

  uint8_t chosen = (holdMs >= 2000) ? DISP_VARIANT_ST : DISP_VARIANT_ILI;

  Preferences wprefs;
  wprefs.begin(NVS_DISPLAY_NS, false);
  wprefs.putUChar(NVS_DISPLAY_KEY, chosen);
  wprefs.end();

  // If IPS variant selected, wipe touch calibration so the cal wizard runs fresh with correct mapping
  if (chosen == DISP_VARIANT_ST) {
    Preferences calprefs;
    calprefs.begin("bytetouch", false);
    calprefs.putBool("done", false);
    calprefs.end();
    Serial.println("[DISP] IPS selected — touch cal wiped, will recalibrate");
  }
  Serial.printf("[DISP] User selected variant: %d\n", chosen);
  return chosen;
}

enum AppState { STATE_BOOT, STATE_MAIN };
static AppState appState = STATE_BOOT;

// Watchdog on/off 
static const char* MAIN_LABELS[8] = {
  "WiFi", "BLE", "GPS", "RFID",
  "Tools", "SD", "Info", "About"
};

// Attempt to connect to the saved AP
static int connectSavedAP() {
  String ssid, pass;
  if (!loadWifiConfig(ssid, pass)) return 0;
  if (ssid.length() == 0) return 0;
  Serial.printf("[WIFI] auto-connecting to saved AP: %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (millis() - start < 12000) {
    if (WiFi.status() == WL_CONNECTED) {
      int ch = WiFi.channel();
      Serial.printf("[WIFI] connected, channel %d\n", ch);
      return ch;
    }
    delay(200);
  }
  Serial.println("[WIFI] auto-connect failed");
  WiFi.disconnect();
  return 0;
}

// Arm the watchdog on the currently connected AP's
static void armWatchdog() {
  if (!leashlockIsEnabled()) { Serial.println("[LEASHLOCK] disabled by user"); return; }
  if (!WiFi.isConnected()) { Serial.println("[LEASHLOCK] not armed — no AP connection"); return; }
  int ch = WiFi.channel();
  leashlockStart(ch);
  Serial.printf("[LEASHLOCK] armed on channel %d\n", ch);
}
static void showMainMenu() {
  tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  tft.fillScreen(C_BG);
  tft.pushImage((SCREEN_W - BG_LOGO_W) / 2, 50, BG_LOGO_W, BG_LOGO_H, byteshield_bg);
  drawHeader(APP_NAME, false);
  drawMainGrid(MAIN_LABELS);
}
static void showPlaceholder(const char* title) {
  tft.fillScreen(C_BG);
  drawHeader(title, true);
  tft.setFreeFont(NULL); tft.setTextSize(2);
  tft.setTextColor(C_SUBTEXT); tft.setTextDatum(MC_DATUM);
  tft.drawString("Coming soon", SCREEN_W / 2, SCREEN_H / 2);
  tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return;
    }
    leashlockTick();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);
  delay(300);
  Serial.println(F("\n\n### ByteShield 2.4 — WiFi + LeashLock ###"));
  // Load variant from NVS first without touching SPI
  {
    Preferences p;
    p.begin(NVS_DISPLAY_NS, true);
    if (p.isKey(NVS_DISPLAY_KEY)) {
      g_dispVariant = p.getUChar(NVS_DISPLAY_KEY, DISP_VARIANT_ILI);
    }
    p.end();
  }
  touchInit();   // touch SPI before tft.init() — same as original
  tft.init();
  g_dispVariant = detectAndSaveDisplayVariant();  // full detect (uses NVS cache on subsequent boots)
  if (g_dispVariant == DISP_VARIANT_ST) {
    tft.invertDisplay(true);
    C_PANEL = 0x0000;
    C_BTN   = 0x0000;
    GPS_RX_PIN = 22;
    GPS_TX_PIN = 21;
    Serial.println("[DISP] ILI9341 IPS inversion applied");
  }
  tft.fillScreen(C_BG);
  if (touchNeedsCalibration()) {
    Serial.println(F("[BOOT] first boot — running calibration"));
    touchCalibrate();
  }

  sdInit();   // mount SD try it here come back if sd fails 
  NimBLEDevice::init("");
  // Auto-connect saved AP, then arm the watchdog on its channel
  int ch = connectSavedAP();
  if (ch > 0) armWatchdog();
  showBootScreen();
  appState = STATE_BOOT;
  Serial.println(F("[BOOT] ready — tap to continue"));
}

// loop
void loop() {
  leashlockTick();
  if (appState == STATE_MAIN) leashlockDrawIndicator();
  int16_t tx, ty;
  switch (appState) {

    case STATE_BOOT:
      if (tftTouched(tx, ty)) {
        waitTouchRelease();
        appState = STATE_MAIN;
        showMainMenu();
      }
      break;

    case STATE_MAIN:
      if (tftTouched(tx, ty)) {
        int8_t hit = mainGridHit(tx, ty);
        waitTouchRelease();
        if (hit < 0) break;
        switch (hit) {
          case 0: {  // WiFi — stop watchdog run menu, re-arm
            leashlockStop();
            int newCh = 0;
            wifiMenu(&newCh);
            // reconnect saved AP so the watchdog has a channel again
            connectSavedAP();
            armWatchdog();
            showMainMenu();
            break;
          }
          case 1:  // BLE — would also stop watchdog
            leashlockStop();
            bleMenu();
            connectSavedAP();
            armWatchdog();
            showMainMenu();
            break;
          case 2:
            leashlockStop();
            gpsMenu();
            connectSavedAP();
            armWatchdog();
            showMainMenu();
            break;
          case 3: showPlaceholder("RFID");  showMainMenu(); break;
          case 4: toolsMenu(); showMainMenu(); break;
          case 5: sdMenu();    showMainMenu(); break;
          case 6: infoMenu();  showMainMenu(); break;
          case 7: aboutMenu(); showMainMenu(); break;
        }
      }
      break;
  }
}