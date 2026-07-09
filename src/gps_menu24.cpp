#include "gps_menu24.h"
#include "ui24.h"
#include "config24.h"
#include "touch24.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SD.h>
#include <SPI.h>
#include "sd_utils24.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "Orbitron_Bold_12.h"

extern TFT_eSPI tft;

// add variant pins 22 21 done moved to config file 
#define GPS_BAUD        9600
static bool gpsStarted = false;
#define FLOCK_ALERT_M   250.0f
#define FLOCK_CLOSE_M   80.0f
#define MAX_FLOCK       2000
#define FLOCK_FILE      "/flock.csv"
static TinyGPSPlus    gps;
HardwareSerial        gpsSerial(1);
static float flockLat[MAX_FLOCK];
static float flockLon[MAX_FLOCK];
static int   flockCount  = 0;
static bool  flockLoaded = false;

// Forward
static void drawGpsFields(double lat, double lon, double speedKph,
                          int sats, double altM, float nearestM,
                          bool in150, bool in50);
static void gpsFlockScreen();
static void gpsAddCamera();
static void gpsUpdateDB();
static void gpsDebugScreen();

// Trig
static float haversineM(float lat1, float lon1, float lat2, float lon2) {
  const float R = 6371000.0f;
  float dLat = radians(lat2 - lat1);
  float dLon = radians(lon2 - lon1);
  float a = sinf(dLat/2)*sinf(dLat/2) +
            cosf(radians(lat1))*cosf(radians(lat2))*
            sinf(dLon/2)*sinf(dLon/2);
  return R * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

// Load flock.csv file
#define FLOCK_USER_FILE "/flock_user.csv"

static void parseFlockFile(const char* path, bool skipHeader) {
  File f = SD.open(path, FILE_READ);
  if (!f) return;
  bool firstLine = skipHeader;
  while (f.available() && flockCount < MAX_FLOCK) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (firstLine) {
      firstLine = false;
      if (line.startsWith("@") || line.startsWith("l") ||
          !isdigit(line.charAt(0))) continue;
    }
    int sep = line.indexOf('\t');
    if (sep < 0) sep = line.indexOf(',');
    if (sep < 0) continue;
    String latStr = line.substring(0, sep);
    String lonStr = line.substring(sep + 1);
    latStr.trim(); lonStr.trim();
    float lat = latStr.toFloat();
    float lon = lonStr.toFloat();
    if (lat == 0.0f && lon == 0.0f) continue;
    flockLat[flockCount] = lat;
    flockLon[flockCount] = lon;
    flockCount++;
  }
  f.close();
}
// sd files
static void loadFlockData() {
  flockCount  = 0;
  flockLoaded = false;
  parseFlockFile(FLOCK_FILE, true);       // main DB 
  parseFlockFile(FLOCK_USER_FILE, false); // user additions dont delete leave forever
  flockLoaded = (flockCount > 0);
}
// Nearest flock 
static float nearestFlockM(float lat, float lon) {
  if (flockCount == 0) return 99999.0f;
  float minD = 99999.0f;
  for (int i = 0; i < flockCount; i++) {
    float d = haversineM(lat, lon, flockLat[i], flockLon[i]);
    if (d < minD) minD = d;
  }
  return minD;
}
// Chirp Tones
static void chirp(int count, int onMs, int offMs, int freq) {
  for (int i = 0; i < count; i++) {
    tone(SPEAKER_PIN, freq, onMs);
    delay(onMs + offMs);
  }
}

// Draw satellite dets
static void drawSatBars(int sats, int x, int y) {
  for (int i = 0; i < 5; i++) {
    int bh = 4 + i * 4;
    int bw = 6;
    int bx = x + i * (bw + 2);
    int by = y + (20 - bh);
    uint16_t col = (i < (sats / 2)) ? C_ACCENT : C_PANEL;
    tft.fillRect(bx, by, bw, bh, col);
    tft.drawRect(bx, by, bw, bh, C_BORDER);
  }
  tft.setFreeFont(NULL); tft.setTextSize(1);
  tft.setTextColor(C_TEXT);
  char buf[8]; snprintf(buf, sizeof(buf), "%d", sats);
  tft.drawString(buf, x + 44, y + 12);
}

// Full screen
static void drawFullScreen(bool hasFix, double lat, double lon,
                           double speedKph, int sats, double altM,
                           float nearestM, bool in150, bool in50) {
  tft.fillScreen(C_BG);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  drawHeader("GPS / Flock Alert", true);

  tft.fillRect(0, HEADER_H, SCREEN_W, 14, C_PANEL);
  tft.setFreeFont(NULL); tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM);
  if (flockCount > 0) {
    tft.setTextColor(C_ACCENT);
    char buf[40];
    snprintf(buf, sizeof(buf), "Flock DB: %d cameras", flockCount);
    tft.drawString(buf, 6, HEADER_H + 7);
  } else {
    tft.setTextColor(C_RED);
    tft.drawString("No flock.csv on SD", 6, HEADER_H + 7);
  }
  tft.setTextDatum(TL_DATUM);

  int y = HEADER_H + 18;

  if (!hasFix) {
    tft.fillRect(0, y, SCREEN_W, 60, C_PANEL);
    tft.setTextColor(C_YELLOW); tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ACQUIRING FIX", SCREEN_W/2, y + 16);
    tft.setTextSize(1);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("Point antenna toward sky", SCREEN_W/2, y + 36);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("SATS:", 8, y + 58);
    drawSatBars(sats, 48, y + 50);
    if (flockCount > 0) {
      tft.setTextColor(C_ACCENT);
      tft.drawString("Flock DB ready - waiting for fix", 8, y + 80);
    }
    return;
  }
// use config colors stop naming them 
  tft.fillRect(0, y, SCREEN_W, 46, C_PANEL);
  tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
  tft.drawString("LAT", 8, y + 4);
  tft.drawString("LON", 8, y + 20);
  tft.setTextColor(C_TEXT); tft.setTextSize(1);
  char buf[32];
  snprintf(buf, sizeof(buf), "%.6f", lat);
  tft.drawString(buf, 38, y + 4);
  snprintf(buf, sizeof(buf), "%.6f", lon);
  tft.drawString(buf, 38, y + 20);
  tft.setTextColor(C_ACCENT); tft.setTextSize(2);
  snprintf(buf, sizeof(buf), "%.1f", speedKph * 0.621371f);
  tft.drawString(buf, 8, y + 36);
  tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
  tft.drawString("mph", 8 + (int)(strlen(buf) * 12), y + 44);
  tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
  snprintf(buf, sizeof(buf), "ALT %.0fm", altM);
  tft.drawString(buf, SCREEN_W - 70, y + 40);
  y += 56;
  tft.drawFastHLine(0, y, SCREEN_W, C_PANEL);
  y += 4;
  tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
  tft.drawString("SATS", 8, y + 6);
  drawSatBars(sats, 44, y);
  y += 28;
  tft.drawFastHLine(0, y, SCREEN_W, C_PANEL);
  y += 4;

  if (flockCount == 0) {
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("No Flock DB loaded", 8, y + 6);
    return;
  }
  if (in50) {
    tft.fillRoundRect(4, y, SCREEN_W - 8, 72, 4, C_RED);
    tft.setTextColor(C_BG); tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("!! FLOCK CAMERA !!", SCREEN_W/2, y + 14);
    tft.setTextSize(1);
    snprintf(buf, sizeof(buf), "CRITICAL: %.0f metres", nearestM);
    tft.drawString(buf, SCREEN_W/2, y + 34);
    tft.drawString("SLOW DOWN", SCREEN_W/2, y + 50);
    tft.setTextDatum(TL_DATUM);
  } else if (in150) {
    tft.fillRoundRect(4, y, SCREEN_W - 8, 72, 4, C_ACCENT2);
    tft.setTextColor(C_BG); tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("FLOCK APPROACHING", SCREEN_W/2, y + 14);
    tft.setTextSize(1);
    snprintf(buf, sizeof(buf), "WARNING: %.0f metres", nearestM);
    tft.drawString(buf, SCREEN_W/2, y + 34);
    tft.drawString("ALPR CAMERA AHEAD", SCREEN_W/2, y + 50);
    tft.setTextDatum(TL_DATUM);
  } else {
    tft.fillRoundRect(4, y, SCREEN_W - 8, 72, 4, C_PANEL);
    tft.setTextColor(C_ACCENT); tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("CLEAR", SCREEN_W/2, y + 16);
    tft.setTextColor(C_SUBTEXT);
    if (nearestM < 9999.0f) {
      snprintf(buf, sizeof(buf), "Nearest: %.0fm", nearestM);
      tft.drawString(buf, SCREEN_W/2, y + 32);
    } else {
      tft.drawString("No cameras in range", SCREEN_W/2, y + 32);
    }
    tft.setTextDatum(TL_DATUM);
  }
}

// Partial data update (ui can tft.roundrect if it shows up)
static void drawGpsFields(double lat, double lon, double speedKph,
                          int sats, double altM, float nearestM,
                          bool in150, bool in50) {
  int y = HEADER_H + 18;
  char buf[32];

  tft.fillRect(38, y + 4, SCREEN_W - 46, 8, C_PANEL);
  tft.fillRect(38, y + 20, SCREEN_W - 46, 8, C_PANEL);
  tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(0xFFFF);
  snprintf(buf, sizeof(buf), "%.6f", lat);
  tft.drawString(buf, 38, y + 4);
  snprintf(buf, sizeof(buf), "%.6f", lon);
  tft.drawString(buf, 38, y + 20);

  tft.fillRect(8, y + 36, 100, 16, C_PANEL);
  tft.setTextColor(C_ACCENT); tft.setTextSize(2);
  snprintf(buf, sizeof(buf), "%.1f", speedKph * 0.621371f);
  tft.drawString(buf, 8, y + 36);
  tft.setTextColor(0x0228); tft.setTextSize(1);
  tft.drawString("mph", 8 + (int)(strlen(buf) * 12), y + 44);

  tft.fillRect(SCREEN_W - 70, y + 40, 66, 8, C_PANEL);
  tft.setTextColor(0x0228); tft.setTextSize(1);
  snprintf(buf, sizeof(buf), "ALT %.0fm", altM);
  tft.drawString(buf, SCREEN_W - 70, y + 40);

  y += 56 + 4;
  tft.fillRect(44, y, 60, 22, C_PANEL);
  drawSatBars(sats, 44, y);
  y += 28 + 4;

  tft.fillRoundRect(4, y, SCREEN_W - 8, 72, 4,
                    in50 ? C_RED : in150 ? C_ACCENT2 : C_PANEL);
  tft.setTextDatum(MC_DATUM);
  if (in50) {
    tft.setTextColor(0x0000); tft.setTextSize(2);
    tft.drawString("!! FLOCK CAMERA !!", SCREEN_W/2, y + 14);
    tft.setTextSize(1);
    snprintf(buf, sizeof(buf), "CRITICAL: %.0f metres", nearestM);
    tft.drawString(buf, SCREEN_W/2, y + 34);
    tft.drawString("SLOW DOWN", SCREEN_W/2, y + 50);
  } else if (in150) {
    tft.setTextColor(0x0000); tft.setTextSize(2);
    tft.drawString("FLOCK APPROACHING", SCREEN_W/2, y + 14);
    tft.setTextSize(1);
    snprintf(buf, sizeof(buf), "WARNING: %.0f metres", nearestM);
    tft.drawString(buf, SCREEN_W/2, y + 34);
    tft.drawString("ALPR CAMERA AHEAD", SCREEN_W/2, y + 50);
  } else {
    tft.setTextColor(C_ACCENT); tft.setTextSize(1);
    tft.drawString("CLEAR", SCREEN_W/2, y + 16);
    tft.setTextColor(C_SUBTEXT);
    if (nearestM < 9999.0f) {
      snprintf(buf, sizeof(buf), "Nearest: %.0fm", nearestM);
      tft.drawString(buf, SCREEN_W/2, y + 32);
    } else {
      tft.drawString("No cameras in range", SCREEN_W/2, y + 32);
    }
  }
  tft.setTextDatum(TL_DATUM);
}

// Flock screen ui to add or not to add come back
static void gpsFlockScreen() {
  loadFlockData();

  bool     lastFix  = false;
  float    nearestM = 99999.0f;
  bool     in150    = false;
  bool     in50     = false;
  bool     was150   = false;
  bool     was50    = false;
  uint32_t lastDraw = 0;
  int      dotCount = 0;

  drawFullScreen(false, 0, 0, 0, 0, 0, nearestM, false, false);
  while (true) {
    static uint32_t charsSeen = 0;
    while (gpsSerial.available()) {
      char c = gpsSerial.read();
      charsSeen++;
      gps.encode(c);
    }
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) {
        return;
      }
    }
    bool hasFix = gps.location.isValid() && gps.location.age() < 3000;
    int  sats   = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
    if (hasFix) {
      double lat = gps.location.lat();
      double lon = gps.location.lng();
      nearestM   = nearestFlockM((float)lat, (float)lon);
      in150      = (nearestM <= FLOCK_ALERT_M);
      in50       = (nearestM <= FLOCK_CLOSE_M);
      if (in50 && !was50)   chirp(5, 80,  80,  2200); // chrip pattern changes
      else if (in150 && !was150) chirp(3, 150, 150, 1600);  // chrip pattern changes
      was50  = in50;
      was150 = in150;
    } else {
      in150 = false; in50 = false;
      was150 = false; was50 = false;
    }

    if (millis() - lastDraw > 500) {
      lastDraw = millis();
      if (hasFix != lastFix) {
        drawFullScreen(hasFix,
                       gps.location.lat(), gps.location.lng(),
                       gps.speed.kmph(), sats,
                       gps.altitude.isValid() ? gps.altitude.meters() : 0.0,
                       nearestM, in150, in50);
        lastFix = hasFix;
      } else if (hasFix) {
        drawGpsFields(gps.location.lat(), gps.location.lng(),
                      gps.speed.kmph(), sats,
                      gps.altitude.isValid() ? gps.altitude.meters() : 0.0,
                      nearestM, in150, in50);
      } else {
        dotCount = (dotCount + 1) % 4;
        drawFullScreen(false, 0, 0, 0, sats, 0, nearestM, false, false);
        tft.setFreeFont(NULL); tft.setTextSize(2);
        tft.setTextColor(C_YELLOW);
        tft.setTextDatum(MC_DATUM);
        char dots[5]; memset(dots, '.', dotCount); dots[dotCount] = 0;
        tft.fillRect(0, HEADER_H + 52, SCREEN_W, 20, C_PANEL);
        tft.drawString(dots, SCREEN_W/2, HEADER_H + 60);
        tft.setTextDatum(TL_DATUM);
      }
    }
    delay(10);
  }
}

// Add Camera button basied on location need to add to main detection screen @GhostT3ch
static void gpsAddCamera() {
  tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  tft.fillScreen(0x0000);
  drawHeader("Add Camera", true);

  // Need GPS fix to work
  bool hasFix = gps.location.isValid() && gps.location.age() < 5000;
  if (!hasFix) {
    tft.setTextColor(C_RED);
    tft.drawString("No GPS fix.", 10, HEADER_H + 20);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("Run Flock Alert first to", 10, HEADER_H + 36);
    tft.drawString("acquire a satellite fix.", 10, HEADER_H + 52);
    while (true) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) { waitTouchRelease(); if (backButtonHit(tx, ty)) return; }
      delay(20);
    }
  }

  double lat = gps.location.lat();
  double lon = gps.location.lng();
  int y = HEADER_H + 12;
  tft.setTextColor(C_SUBTEXT); tft.drawString("Current position:", 8, y); y += 16;
  tft.setTextColor(C_TEXT);
  char buf[32];
  snprintf(buf, sizeof(buf), "LAT  %.6f", lat);
  tft.drawString(buf, 8, y); y += 14;
  snprintf(buf, sizeof(buf), "LON  %.6f", lon);
  tft.drawString(buf, 8, y); y += 22;
  tft.setTextColor(C_SUBTEXT);
  tft.drawString("Saves to flock_user.csv", 8, y); y += 14;
  tft.drawString("Survives DB updates.", 8, y); y += 22;

  // Confirm button add shadow button maybe?
  int btnY = y;
  tft.fillRoundRect(8, btnY, SCREEN_W - 16, 40, 4, C_BTN);
  tft.drawRoundRect(8, btnY, SCREEN_W - 16, 40, 4, C_ACCENT);
  tft.setTextColor(C_ACCENT); tft.setTextDatum(MC_DATUM);
  tft.drawString("SAVE THIS LOCATION", SCREEN_W/2, btnY + 20);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return;
      if (tx >= 8 && tx < SCREEN_W - 8 && ty >= btnY && ty < btnY + 40) {
        // Add to flock_user.csv file maybe sepperate file that survies deletion 
        File f = SD.open(FLOCK_USER_FILE, FILE_APPEND);
        bool ok = false;
        if (f) {
          char line[32];
          snprintf(line, sizeof(line), "%.6f,%.6f\n", lat, lon);
          f.print(line);
          f.close();
          ok = true;
        }
        tft.fillRect(0, btnY, SCREEN_W, 60, C_BG);
        if (ok) {
          tft.setTextColor(C_ACCENT); tft.setTextDatum(MC_DATUM);
          tft.drawString("Saved!", SCREEN_W/2, btnY + 10);
          tft.setTextColor(C_SUBTEXT);
          tft.drawString("Camera added to your DB.", SCREEN_W/2, btnY + 26);
        } else {
          tft.setTextColor(C_RED); tft.setTextDatum(MC_DATUM);
          tft.drawString("Save failed.", SCREEN_W/2, btnY + 10);
          tft.drawString("Check SD card.", SCREEN_W/2, btnY + 26);
        }
        tft.setTextDatum(TL_DATUM);
        delay(2000);
        return;
      }
    }
    delay(20);
  }
}

// GPS Debug Screen Imported from bench tester
static void gpsDebugScreen() {
  tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  tft.fillScreen(C_BG);
  drawHeader("GPS Debug", true);

  // STAGE 1: Quick serial / NMEA check need to add big delay users go stright to this before it has time to get going 
  delay(302);
  int y = HEADER_H + 8;
  tft.setTextColor(C_SUBTEXT);
  tft.drawString("Stage 1: NMEA sniff (6s)...", 8, y); y += 14;
  tft.setTextColor(C_YELLOW);
  tft.drawString("Listening...", 8, y);
  gpsSerial.flush();
  delay(51);
  while (gpsSerial.available()) gpsSerial.read();
  int  dollarCount = 0;
  bool nmeaValid   = false;
  unsigned long sniffStart = millis();
  while (millis() - sniffStart < 6000) {
    while (gpsSerial.available()) {
      char c = gpsSerial.read();
      if (c == '$') dollarCount++;
      if (gps.encode(c)) nmeaValid = true;
    }
  }

  bool serialAlive = (dollarCount > 2);
  tft.fillRect(8, y, SCREEN_W - 16, 10, C_BG);
  tft.setTextColor(C_SUBTEXT); tft.drawString("Serial: ", 8, y);
  tft.setTextColor(serialAlive ? C_ACCENT : C_RED);
  tft.drawString(serialAlive ? "PASS" : "FAIL", 60, y); y += 13;
  char buf[48];
  snprintf(buf, sizeof(buf), "  %d sentences in 6s", dollarCount);
  tft.setTextColor(C_SUBTEXT); tft.drawString(buf, 8, y); y += 13;
  tft.setTextColor(C_SUBTEXT); tft.drawString("NMEA:   ", 8, y);
  if (nmeaValid) {
    tft.setTextColor(C_ACCENT);  tft.drawString("PASS", 60, y);
  } else if (serialAlive) {
    tft.setTextColor(C_YELLOW);  tft.drawString("PARTIAL", 60, y);
  } else {
    tft.setTextColor(C_RED);     tft.drawString("FAIL", 60, y);
  }
  y += 13;

  snprintf(buf, sizeof(buf), "  RX pin: GPIO%d  Baud: %d", GPS_RX_PIN, GPS_BAUD);
  tft.setTextColor(C_SUBTEXT); tft.drawString(buf, 8, y); y += 13;
  if (!serialAlive) {
    tft.setTextColor(C_RED);
    tft.drawString("  No data - check wiring/baud", 8, y); y += 13;
  }
  tft.drawFastHLine(0, y + 2, SCREEN_W, C_BORDER); y += 10;
  tft.setTextColor(C_SUBTEXT);
  tft.drawString("Stage 2: Live tracker...", 8, y); y += 12;
  tft.drawString("Tap back to exit", 8, y); y += 14;
  delay(2000);

  //STAGE 2: Live satellite tracker 
  unsigned long liveStart = millis();
  uint32_t lastDraw = 0;

  const int liveY = HEADER_H + 110;
  while (true) {
    while (gpsSerial.available()) gps.encode(gpsSerial.read());
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return;
    }
    if (millis() - lastDraw > 1000) {
      lastDraw = millis();
      unsigned long elapsed = (millis() - liveStart) / 1000;
      int  sats   = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
      bool hasFix = gps.location.isValid() && gps.location.age() < 3000;
      int py = liveY;

      // Title bar
      tft.fillRect(0, py, SCREEN_W, 12, hasFix ? C_ACCENT : C_YELLOW);
      tft.setTextColor(C_BG); tft.setTextDatum(ML_DATUM);
      const char* spin = "|/-\\";
      if (hasFix) {
        snprintf(buf, sizeof(buf), "GPS LOCKED  %lus", elapsed);
      } else {
        snprintf(buf, sizeof(buf), "SEARCHING %c  %lus", spin[elapsed % 4], elapsed);
      }
      tft.drawString(buf, 4, py + 6);
      tft.setTextDatum(TL_DATUM);
      py += 16;

      // Sats
      tft.fillRect(0, py, SCREEN_W, 12, C_BG);
      tft.setTextColor(C_SUBTEXT); tft.drawString("Sats:", 8, py);
      snprintf(buf, sizeof(buf), "%d", sats);
      tft.setTextColor(C_TEXT); tft.drawString(buf, 46, py);
      int barMax = SCREEN_W - 90;
      int barW   = sats > 0 ? constrain(sats * (barMax / 12), 2, barMax) : 0;
      tft.fillRect(66, py + 2, barMax, 8, C_PANEL);
      if (barW > 0) tft.fillRect(66, py + 2, barW, 8, hasFix ? C_ACCENT : C_YELLOW);
      py += 14;

      // Fix
      tft.fillRect(0, py, SCREEN_W, 10, C_BG);
      tft.setTextColor(C_SUBTEXT); tft.drawString("Fix: ", 8, py);
      tft.setTextColor(hasFix ? C_ACCENT : C_RED);
      tft.drawString(hasFix ? "YES" : "NO", 46, py); py += 13;

      // Lat
      tft.fillRect(0, py, SCREEN_W, 10, C_BG);
      tft.setTextColor(C_SUBTEXT); tft.drawString("Lat: ", 8, py);
      tft.setTextColor(C_TEXT);
      if (hasFix) snprintf(buf, sizeof(buf), "%.6f", gps.location.lat());
      else        snprintf(buf, sizeof(buf), "waiting...");
      tft.drawString(buf, 46, py); py += 13;

      // Lon
      tft.fillRect(0, py, SCREEN_W, 10, C_BG);
      tft.setTextColor(C_SUBTEXT); tft.drawString("Lon: ", 8, py);
      tft.setTextColor(C_TEXT);
      if (hasFix) snprintf(buf, sizeof(buf), "%.6f", gps.location.lng());
      else        snprintf(buf, sizeof(buf), "waiting...");
      tft.drawString(buf, 46, py); py += 13;

      // Alt
      tft.fillRect(0, py, SCREEN_W, 10, C_BG);
      tft.setTextColor(C_SUBTEXT); tft.drawString("Alt: ", 8, py);
      tft.setTextColor(C_TEXT);
      if (gps.altitude.isValid()) snprintf(buf, sizeof(buf), "%.1fm", gps.altitude.meters());
      else                        snprintf(buf, sizeof(buf), "--");
      tft.drawString(buf, 46, py); py += 13;

      // Speed might have bug shows some movment when still but not sure if its gps or code issue
      tft.fillRect(0, py, SCREEN_W, 10, C_BG);
      tft.setTextColor(C_SUBTEXT); tft.drawString("Spd: ", 8, py);
      tft.setTextColor(C_TEXT);
      if (gps.speed.isValid()) snprintf(buf, sizeof(buf), "%.1f mph", gps.speed.mph());
      else                     snprintf(buf, sizeof(buf), "--");
      tft.drawString(buf, 46, py); py += 13;

      // Chars processed
      tft.fillRect(0, py, SCREEN_W, 10, C_BG);
      tft.setTextColor(C_SUBTEXT); tft.drawString("Parsed:", 8, py);
      snprintf(buf, sizeof(buf), "%lu chars", gps.charsProcessed());
      tft.setTextColor(C_TEXT); tft.drawString(buf, 54, py); py += 13;

      // Bad CRC 
      if (py + 10 < SCREEN_H) {
        tft.fillRect(0, py, SCREEN_W, 10, C_BG);
        tft.setTextColor(C_SUBTEXT); tft.drawString("Bad CRC:", 8, py);
        snprintf(buf, sizeof(buf), "%lu", gps.failedChecksum());
        tft.setTextColor(gps.failedChecksum() > 0 ? C_YELLOW : C_TEXT);
        tft.drawString(buf, 54, py);
      }
    }

    delay(10);
  }
}

// Update DB issues with dial out in canada need mirror site 
static void gpsUpdateDB() {
  tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  tft.fillScreen(C_BG);
  drawHeader("Update DB", true);

  // Check WiFi
  if (!WiFi.isConnected()) {
    tft.setTextColor(C_RED);
    tft.drawString("No WiFi connection.", 10, HEADER_H + 20);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("Connect via WiFi menu first.", 10, HEADER_H + 36);
    while (true) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) { waitTouchRelease(); if (backButtonHit(tx, ty)) return; }
      delay(20);
    }
  }

  // Check GPS fix
  bool hasFix = gps.location.isValid() && gps.location.age() < 10000;
  if (!hasFix) {
    tft.setTextColor(C_RED);
    tft.drawString("No GPS fix.", 10, HEADER_H + 20);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("Run Flock Alert first to", 10, HEADER_H + 36);
    tft.drawString("acquire a satellite fix.", 10, HEADER_H + 52);
    while (true) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) { waitTouchRelease(); if (backButtonHit(tx, ty)) return; }
      delay(20);
    }
  }

  double lat = gps.location.lat();
  double lon = gps.location.lng();

  // Zone picker
  int y = HEADER_H + 10;
  tft.setTextColor(C_SUBTEXT);
  tft.drawString("Select your ZONE:", 8, y); y += 16;
  char posStr[48];
  snprintf(posStr, sizeof(posStr), "%.4f, %.4f", lat, lon);
  tft.setTextColor(C_ACCENT);
  tft.drawString(posStr, 8, y); y += 18;

  const char* zoneLabels[3] = { "City - 30 km ", "Urban - 50 km ", "Rural - 65 km " };
  const float zoneKm[3]     = { 30.0f, 50.0f, 65.0f };
  const int   btnH = 38, btnPad = 8;

  Button btns[3];
  for (int i = 0; i < 3; i++) {
    btns[i] = { 8, (int16_t)(y + i * (btnH + btnPad)),
                (int16_t)(SCREEN_W - 16), (int16_t)btnH,
                zoneLabels[i], C_BTN, C_TEXT };
    tft.fillRoundRect(btns[i].x, btns[i].y, btns[i].w, btns[i].h, 4, C_BTN);
    tft.drawRoundRect(btns[i].x, btns[i].y, btns[i].w, btns[i].h, 4, C_ACCENT);
    tft.setFreeFont(&Orbitron_Bold_12);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT);
    tft.drawString(zoneLabels[i], btns[i].x + btns[i].w/2, btns[i].y + btns[i].h/2 + 3);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
  }

  int chosen = -1;
  while (chosen < 0) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return;
      for (int i = 0; i < 3; i++) {
        if (tx >= btns[i].x && tx < btns[i].x + btns[i].w &&
            ty >= btns[i].y && ty < btns[i].y + btns[i].h) {
          drawButton(btns[i], true); delay(60);
          chosen = i;
          break;
        }
      }
    }
    delay(20);
  }

  float km = zoneKm[chosen];

  // Build box
  float dLat = km / 111.0f;
  float dLon = km / (111.0f * cosf(radians((float)lat)));
  float minLat = (float)lat - dLat;
  float maxLat = (float)lat + dLat;
  float minLon = (float)lon - dLon;
  float maxLon = (float)lon + dLon;

  // Show downloading screen
  tft.fillScreen(C_BG);
  drawHeader("Update DB", true);
  tft.setTextColor(C_ACCENT);
  tft.drawString("Downloading camera data...", 8, HEADER_H + 16);
  tft.setTextColor(C_SUBTEXT);
  tft.drawString("This may take 10-30 seconds.", 8, HEADER_H + 32);
  char bbox[64];
  snprintf(bbox, sizeof(bbox), "%.3f,%.3f,%.3f,%.3f", minLat, minLon, maxLat, maxLon);
  tft.drawString(bbox, 8, HEADER_H + 48);

  // Build 
  String query = "[out:csv(::lat,::lon;false)][timeout:60];node[\"man_made\"=\"surveillance\"][\"surveillance:type\"=\"ALPR\"](";
  query += bbox;
  query += ");out;";

  // HTTP request — HTTPS with fallback mirror for cananda UK works fine add mirros for other places if needed
  const char* hosts[] = { "overpass-api.de",     "overpass.kumi.systems" };
  const char* paths[] = { "/api/interpreter",     "/api/interpreter"      };
  bool connected = false;
  WiFiClientSecure client;
  client.setInsecure();

  for (int attempt = 0; attempt < 2 && !connected; attempt++) {
    if (attempt == 1) {
      tft.fillRect(0, HEADER_H + 64, SCREEN_W, 16, C_BG);
      tft.setTextColor(C_YELLOW);
      tft.drawString("Trying mirror server...", 8, HEADER_H + 64);
    }
    if (client.connect(hosts[attempt], 443)) {
      String body    = "data=" + query;
      String httpReq = String("POST ") + paths[attempt] + " HTTP/1.0\r\n"
                     + "Host: " + hosts[attempt] + "\r\n"
                     + "User-Agent: ByteShield/1.0\r\n"
                     + "Content-Type: application/x-www-form-urlencoded\r\n"
                     + "Content-Length: " + String(body.length()) + "\r\n"
                     + "Connection: close\r\n\r\n"
                     + body;
      client.print(httpReq);
      connected = true;
    }
  }

  if (!connected) {
    tft.fillRect(0, HEADER_H + 64, SCREEN_W, 40, C_BG);
    tft.setTextColor(C_RED);
    tft.drawString("Connection failed.", 8, HEADER_H + 64);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("Check WiFi signal.", 8, HEADER_H + 80);
    while (true) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) { waitTouchRelease(); if (backButtonHit(tx, ty)) return; }
      delay(20);
    }
  }

  // Wait for response
  uint32_t t = millis();
  while (!client.available() && millis() - t < 15000) delay(50);
  if (!client.available()) {
    tft.fillRect(0, HEADER_H + 64, SCREEN_W, 40, C_BG);
    tft.setTextColor(C_RED);
    tft.drawString("No response from server.", 8, HEADER_H + 64);
    client.stop();
    Serial.println("[UPDATE] Done.");
    while (true) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) { waitTouchRelease(); if (backButtonHit(tx, ty)) return; }
      delay(20);
    }
  }

  // Skip HTTP headers
  bool inHeaders = true;
  String headerLine = "";
  while (client.available() && inHeaders) {
    char c = client.read();
    if (c == '\n') {
      if (headerLine.length() <= 1) inHeaders = false;
      headerLine = "";
    } else if (c != '\r') {
      headerLine += c;
    }
  }

  // Stream body to SD
  File f = SD.open(FLOCK_FILE, FILE_WRITE);
  int saved = 0;
  if (f) {
    String lineBuf = "";
    uint32_t lastProgress = millis();
    uint32_t lastData = millis();
    while (millis() - lastData < 10000) {
      while (client.available()) {
        char c = client.read();
        lastData = millis();
        if (c == '\n') {
          lineBuf.trim();
          if (lineBuf.length() > 0) {
            if (!lineBuf.startsWith("@") && isdigit(lineBuf.charAt(0))) {
              f.println(lineBuf);
              saved++;
            }
          }
          lineBuf = "";
        } else {
          lineBuf += c;
        }
      }
      if (!client.connected() && !client.available()) break;
      if (millis() - lastProgress > 1000) {
        lastProgress = millis();
        tft.fillRect(0, HEADER_H + 64, SCREEN_W, 16, C_BG);
        tft.setTextColor(C_YELLOW); tft.setTextSize(1);
        char prog[32]; snprintf(prog, sizeof(prog), "Saved: %d cameras...", saved);
        tft.drawString(prog, 8, HEADER_H + 64);
      }
      delay(10);
    }
    Serial.println("[UPDATE] Done.");
    if (lineBuf.length() > 0) {
      lineBuf.trim();
      if (!lineBuf.startsWith("@") && isdigit(lineBuf.charAt(0))) {
        f.println(lineBuf);
        saved++;
      }
    }
    f.close();
  }
  client.stop();

  // Result screen
  tft.fillScreen(C_BG);
  drawHeader("Update DB", true);
  if (saved > 0) {
    tft.setTextColor(C_ACCENT); tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    char result[32]; snprintf(result, sizeof(result), "%d cameras", saved);
    tft.drawString(result, SCREEN_W/2, HEADER_H + 40);
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT);
    tft.drawString("saved to flock.csv", SCREEN_W/2, HEADER_H + 62);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("User cameras preserved.", SCREEN_W/2, HEADER_H + 80);
    tft.setTextDatum(TL_DATUM);
  } else {
    tft.setTextColor(C_RED);
    tft.drawString("No cameras saved.", 8, HEADER_H + 20);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("Server may be busy.", 8, HEADER_H + 36);
    tft.drawString("Try again later.", 8, HEADER_H + 52);
  }
  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) { waitTouchRelease(); if (backButtonHit(tx, ty)) return; }
    delay(20);
  }
}

// GPS submenu
void gpsMenu() {
  if (!gpsStarted) {
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    delay(500);
    gpsStarted = true;
  }
  while (true) {
    tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
    tft.fillScreen(C_BG);
    drawHeader("GPS", true);
    const char* labels[4] = { "Flock Alert", "Add Camera", "Update DB", "GPS Debug" };
    int btnW = SCREEN_W - 20, btnH = 38, btnPad = 8;
    int startY = HEADER_H + 16;

    Button btns[4];
    for (int i = 0; i < 4; i++) {
      btns[i] = { 10, (int16_t)(startY + i * (btnH + btnPad)),
                  (int16_t)btnW, (int16_t)btnH, labels[i], C_BTN, C_TEXT };
      drawButton(btns[i]);
    }
    bool exitMenu = false, redraw = false;
    while (!exitMenu && !redraw) {
      while (gpsSerial.available()) gps.encode(gpsSerial.read());
      int16_t tx, ty;
      if (tftTouched(tx, ty)) {
        waitTouchRelease();
        if (backButtonHit(tx, ty)) { exitMenu = true; break; }
        for (int i = 0; i < 4; i++) {
          if (tx >= btns[i].x && tx < btns[i].x + btns[i].w &&
              ty >= btns[i].y && ty < btns[i].y + btns[i].h) {
            drawButton(btns[i], true); delay(60);
            if (i == 0)      gpsFlockScreen();
            else if (i == 1) gpsAddCamera();
            else if (i == 2) gpsUpdateDB();
            else             gpsDebugScreen();
            tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
            redraw = true; break;
          }
        }
      }
    }
    if (exitMenu) return;
  }
}