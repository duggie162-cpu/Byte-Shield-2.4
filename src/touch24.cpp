#include "touch24.h"
#include "config24.h"
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;
static SPIClass touchSPI = SPIClass(VSPI);
static XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// Runtime calibration — seeded from config defaults, overwritten by NVS.
static int _calXMin = TOUCH_CAL_XMIN;
static int _calXMax = TOUCH_CAL_XMAX;
static int _calYMin = TOUCH_CAL_YMIN;
static int _calYMax = TOUCH_CAL_YMAX;

// Pressure floor so light grazes don't register as taps.
static const int Z_THRESH = 300;
static Preferences _prefs;


void touchInit() {
  if (g_dispVariant == DISP_VARIANT_ST) {
    touchSPI.end();
    delay(10);
  }
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(0);

  _prefs.begin("bytetouch", true);   // read-only
  bool haveCal = _prefs.getBool("done", false);
  if (haveCal) {
    _calXMin = _prefs.getInt("xmin", _calXMin);
    _calXMax = _prefs.getInt("xmax", _calXMax);
    _calYMin = _prefs.getInt("ymin", _calYMin);
    _calYMax = _prefs.getInt("ymax", _calYMax);
  }
  _prefs.end();

  Serial.printf("[TOUCH] cal %s  X[%d-%d] Y[%d-%d]\n",
                haveCal ? "loaded" : "DEFAULT (not calibrated)",
                _calXMin, _calXMax, _calYMin, _calYMax);
}


bool tftTouched(int16_t &tx, int16_t &ty) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  Serial.printf("[TOUCH] raw x=%d y=%d z=%d\n", p.x, p.y, p.z);
  if (p.z < Z_THRESH) return false;

  if (g_dispVariant == DISP_VARIANT_ST) {
    tx = map(p.y, 3899, 232, 0, SCREEN_W);
    ty = map(p.x, 3822, 219, 0, SCREEN_H);
  } else {
    tx = map(p.x, _calXMin, _calXMax, 0, SCREEN_W);
    ty = map(p.y, _calYMin, _calYMax, 0, SCREEN_H);
  }
  tx = constrain(tx, 0, SCREEN_W - 1);
  ty = constrain(ty, 0, SCREEN_H - 1);
  return true;
}


void waitTouchRelease() {
  while (ts.getPoint().z >= Z_THRESH) delay(10);
  delay(40);
}

//  Calibration
bool touchNeedsCalibration() {
  _prefs.begin("bytetouch", true);
  bool done = _prefs.getBool("done", false);
  _prefs.end();
  return !done;
}

// Average several raw
static void _capRawCorner(int &rx, int &ry) {
  // wait for a solid press
  TS_Point p;
  do { delay(15); p = ts.getPoint(); } while (p.z < Z_THRESH);
  long sx = 0, sy = 0; int n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 250) {         
    p = ts.getPoint();
    if (p.z >= Z_THRESH) { sx += p.x; sy += p.y; n++; }
    delay(10);
  }
  rx = n ? (int)(sx / n) : p.x;
  ry = n ? (int)(sy / n) : p.y;

  while (ts.getPoint().z >= Z_THRESH) delay(10);   // wait for release
  delay(150);
}

void touchCalibrate() {
  struct Corner { const char* name; int cx; int cy; };
  Corner corners[4] = {
    {"TOP LEFT",     12,            12},
    {"TOP RIGHT",    SCREEN_W - 12, 12},
    {"BOTTOM LEFT",  12,            SCREEN_H - 12},
    {"BOTTOM RIGHT", SCREEN_W - 12, SCREEN_H - 12},
  };

  int rx[4], ry[4];
  for (int i = 0; i < 4; i++) {
    tft.fillScreen(C_BG);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString("TOUCH SCREEN CALIBRATION", SCREEN_W / 2, SCREEN_H / 2 - 10);
    tft.drawString(corners[i].name, SCREEN_W / 2, SCREEN_H / 2 + 6);
    tft.drawFastHLine(corners[i].cx - 12, corners[i].cy, 24, C_ACCENT2);
    tft.drawFastVLine(corners[i].cx, corners[i].cy - 12, 24, C_ACCENT2);
    tft.drawCircle(corners[i].cx, corners[i].cy, 6, C_ACCENT2);
    _capRawCorner(rx[i], ry[i]);
    tft.fillCircle(corners[i].cx, corners[i].cy, 6, C_ACCENT);
    delay(200);
  }

  // X axis
  int xLo = rx[0], xHi = rx[0], yLo = ry[0], yHi = ry[0];
  for (int i = 1; i < 4; i++) {
    if (rx[i] < xLo) xLo = rx[i];
    if (rx[i] > xHi) xHi = rx[i];
    if (ry[i] < yLo) yLo = ry[i];
    if (ry[i] > yHi) yHi = ry[i];
  }
  _calXMin = xLo; _calXMax = xHi;
  _calYMin = yLo; _calYMax = yHi;
  _prefs.begin("bytetouch", false);  
  _prefs.putInt("xmin", _calXMin);
  _prefs.putInt("xmax", _calXMax);
  _prefs.putInt("ymin", _calYMin);
  _prefs.putInt("ymax", _calYMax);
  _prefs.putBool("done", true);
  _prefs.end();
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("CALIBRATION SAVED", SCREEN_W / 2, SCREEN_H / 2);
  Serial.printf("[TOUCH] saved  X[%d-%d] Y[%d-%d]\n",
                _calXMin, _calXMax, _calYMin, _calYMax);
  delay(1200);
}

void touchResetCalibration() {
  _prefs.begin("bytetouch", false);
  _prefs.putBool("done", false);
  _prefs.end();
}