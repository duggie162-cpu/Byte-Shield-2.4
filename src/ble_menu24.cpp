#include "ble_menu24.h"
#include "bs_ui.h"
#include "Orbitron_Bold_12.h"
#include "ui24.h"
#include "touch24.h"
#include "config24.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
extern TFT_eSPI tft;
#define SPEAKER_PIN 26
static void bleScanAll();
static void bleChameleonUltra();
static void bleTrackerLocator();
static void blePwnagotchi();
static void bleHuntDevice(const String& addr, const String& name);
static void hunterBeep(int rssi);


//  Device 
struct BLEDev {
  String addr;
  String name;
  int    rssi;
  String mfgData;
};

#define MAX_DEVICES 15 //turned down fine at 20 but 15 is better for screen
static BLEDev devices[MAX_DEVICES];
static int deviceCount = 0;
static int findDevice(const String& addr) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].addr == addr) return i;
  }
  return -1;
}
static void addOrUpdate(const String& addr, const String& name, int rssi, const String& mfg) {
  int idx = findDevice(addr);
  if (idx >= 0) {
    devices[idx].rssi = rssi;
    if (name.length() > 0) devices[idx].name = name;
    if (mfg.length() > 0)  devices[idx].mfgData = mfg;
    return;
  }
  if (deviceCount < MAX_DEVICES) {
    devices[deviceCount] = { addr, name, rssi, mfg };
    deviceCount++;
  }
}

// callback 
static String _huntTarget = "";
static int    _huntRSSI   = -127;
class HuntScanCallback : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    String addr = String(dev->getAddress().toString().c_str());
    if (addr == _huntTarget) {
      _huntRSSI = dev->getRSSI();
    }
    String name = dev->haveName() ? String(dev->getName().c_str()) : "";
    addOrUpdate(addr, name, dev->getRSSI(), "");
  }
};


//  BLE Scan Callback
class BLEScanCallback : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    String addr = String(dev->getAddress().toString().c_str());
    String name = dev->haveName() ? String(dev->getName().c_str()) : "";
    String mfg = "";
    if (dev->haveManufacturerData()) {
      std::string md = dev->getManufacturerData();
      char buf[6];
      for (size_t i = 0; i < md.length() && i < 16; i++) {
        snprintf(buf, sizeof(buf), "%02X ", (uint8_t)md[i]);
        mfg += buf;
      }
    }
    addOrUpdate(addr, name, dev->getRSSI(), mfg);
  }
};


//  BLE Submenu
void bleMenu() {
  esp_wifi_set_promiscuous(false);
  if (WiFi.isConnected()) {
    WiFi.disconnect(false);
    delay(500); // tried double delay to let the WiFi disconnect before starting BLE scan
  }
  delay(200);

  while (true) {
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.fillScreen(C_BG);
    drawHeader("BLE", true);
    const char* labels[4] = {
      "Scan All Devices",
      "Chameleon Ultra",
      "Tracker Locator",
      "Pwnagotchi Hunt"
    };
    int btnW = SCREEN_W - 20;
    int btnH = 45;
    int btnPad = 10;
    int startY = HEADER_H + 20;

    Button btns[4]; //if add more buttons in UI hhere
    for (int i = 0; i < 4; i++) {
      btns[i] = { 10, (int16_t)(startY + i*(btnH+btnPad)),
                  (int16_t)btnW, (int16_t)btnH, labels[i], C_BTN, C_TEXT };
      drawButton(btns[i]);
      tft.setFreeFont(&Orbitron_Bold_12);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_TEXT);
      tft.drawString(labels[i], btns[i].x + btns[i].w/2, btns[i].y + btns[i].h/2 + 3);
      tft.setTextDatum(TL_DATUM);
      tft.setFreeFont(NULL);
    }

    bool exitMenu = false, redrawMenu = false;
    while (!exitMenu && !redrawMenu) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) {
        waitTouchRelease();
        if (backButtonHit(tx, ty)) { exitMenu = true; break; }
        for (int i = 0; i < 4; i++) {
          if (tx >= btns[i].x && tx < btns[i].x + btns[i].w &&
              ty >= btns[i].y && ty < btns[i].y + btns[i].h) {
            drawButton(btns[i], true); delay(60);
            switch (i) {
              case 0: bleScanAll(); break;
              case 1: bleChameleonUltra(); break;
              case 2: bleTrackerLocator(); break;
              case 3: blePwnagotchi(); break;
            }
            tft.setTextDatum(TL_DATUM);
            tft.setFreeFont(NULL);
            tft.setTextSize(1);
            redrawMenu = true; break;
          }
        }
      }
    }
    if (exitMenu) {
      NimBLEDevice::getScan()->stop();
      NimBLEDevice::getScan()->clearResults();
      NimBLEDevice::getScan()->setAdvertisedDeviceCallbacks(nullptr);
      _huntTarget = "";
      return;
    }
  }
}


//  Scan All BLE devices
// Serial updates for low memory debugging what a pain in the ass deauth monitoring killing me
static void doScan() {
  for (int i = 0; i < deviceCount; i++) {
    devices[i].addr = "";
    devices[i].name = "";
    devices[i].mfgData = "";
  }
  deviceCount = 0;
  tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H, C_BG);
  tft.setTextColor(C_ACCENT); tft.setTextSize(1);
  tft.drawString("Scanning BLE...", 10, HEADER_H + 10);
   Serial.printf("[BLE] Heap before scan: %d\n", ESP.getFreeHeap());
  if (ESP.getFreeHeap() < 15000) {
    tft.setTextColor(C_RED);
    tft.drawString("Low memory - restart device", 10, HEADER_H + 30);
    delay(2000);
    return;
  }
   Serial.println("BLE scan starting");
   Serial.printf("[MEM] Internal heap: %d\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
   Serial.printf("[MEM] DMA heap: %d\n", heap_caps_get_free_size(MALLOC_CAP_DMA));
   Serial.printf("[MEM] 8-bit heap: %d\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
   Serial.printf("Heap=%u\n", ESP.getFreeHeap());
   Serial.printf("Connected=%d\n", WiFi.isConnected());
  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  Serial.println("got scanner");
  BLEScanCallback cb;
  pBLEScan->setAdvertisedDeviceCallbacks(&cb);
   Serial.println("callback set");
  pBLEScan->setActiveScan(true);
   Serial.println("active scan set");
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->start(6, false);
   Serial.println("scan complete");
  pBLEScan->stop();
  pBLEScan->setAdvertisedDeviceCallbacks(nullptr);
  pBLEScan->clearResults();
  NimBLEScan* s = NimBLEDevice::getScan();
   Serial.println("scan stopped and cleared");
   Serial.printf("[BLE] Heap after scan: %d\n", ESP.getFreeHeap());
   Serial.printf("[BLE] Devices found: %d\n", deviceCount); 
  s->setActiveScan(false);
  delay(100);
}

static void bleScanAll() {
  const int btnY    = HEADER_H + 4;
  const int btnH    = 24;
  const int refreshW = 80;
  const int listY   = btnY + btnH + 4;
  const int rowH    = 22;
  auto drawChrome = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("Scan All", true);
    tft.fillRoundRect(SCREEN_W - refreshW - 6, btnY, refreshW, btnH, 4, C_BTN);
    tft.drawRoundRect(SCREEN_W - refreshW - 6, btnY, refreshW, btnH, 4, C_ACCENT);
    tft.setTextColor(C_ACCENT); tft.setTextSize(1);
    tft.drawString("REFRESH", SCREEN_W - refreshW + 18, btnY + btnH/2 - 4);
    tft.setTextColor(C_SUBTEXT);
    char s[24]; snprintf(s, sizeof(s), "%d devices", deviceCount);
    tft.drawString(s, 8, btnY + btnH/2 - 4);
  };
  int bleScrollTop = 0;
  const int maxRows = (SCREEN_H - listY - 4) / rowH;
  auto drawList = [&]() {
    // Clip scroll
    int maxScroll = max(0, deviceCount - maxRows);
    if (bleScrollTop > maxScroll) bleScrollTop = maxScroll;
    tft.fillRect(0, listY, SCREEN_W, SCREEN_H - listY, C_BG);
    int count = min(deviceCount - bleScrollTop, maxRows);
    for (int i = 0; i < count; i++) {
      int devIdx = bleScrollTop + i;
      int y = listY + i * rowH;
      // Clip — never draw over header Change from over header Utter Cha0s patch
      if (y < HEADER_H) continue;
      int rssi = devices[devIdx].rssi;
      uint16_t rcolor = rssi > -60 ? C_ACCENT : rssi > -80 ? C_YELLOW : C_RED;
      String label = devices[devIdx].name.length() > 0 ? devices[devIdx].name : devices[devIdx].addr.substring(9);
      if (label.length() > 26) label = label.substring(0, 26);
      tft.setTextColor(C_TEXT); tft.setTextSize(1);
      tft.drawString(label.c_str(), 6, y + 4);
      char rstr[8]; snprintf(rstr, sizeof(rstr), "%d", rssi);
      tft.setTextColor(rcolor);
      tft.drawString(rstr, SCREEN_W - 26, y + 4);
      tft.drawFastHLine(0, y + rowH - 1, SCREEN_W, C_PANEL);
    }
    // Scroll indicator
    if (deviceCount > maxRows) {
      int trackH = SCREEN_H - listY;
      int thumbH = max(12, trackH * maxRows / deviceCount);
      int thumbY = listY + (trackH - thumbH) * bleScrollTop / max(1, deviceCount - maxRows);
      tft.fillRect(SCREEN_W - 3, listY, 3, trackH, C_PANEL);
      tft.fillRect(SCREEN_W - 3, thumbY, 3, thumbH, C_ACCENT);
    }
  };
  drawChrome(); doScan(); drawChrome(); drawList();
  int16_t bleTouchStartY = -1;
  bool bleTouching = false;
  int bleScrollAtTouch = 0;
  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      if (!bleTouching) {
        bleTouching = true;
        bleTouchStartY = ty;
        bleScrollAtTouch = bleScrollTop;
      } else {
        int drag = bleTouchStartY - ty;
        int newScroll = constrain(bleScrollAtTouch + drag / rowH,
                                  0, max(0, deviceCount - maxRows));
        if (newScroll != bleScrollTop) {
          bleScrollTop = newScroll;
          drawList();
        }
      }
    } else {
      if (bleTouching) {
        bleTouching = false;
        int drag = abs(bleTouchStartY - ty);

        if (drag < 8) {
          // Tap
          if (backButtonHit(tx, ty)) {
            NimBLEDevice::getScan()->stop();
            NimBLEDevice::getScan()->clearResults();
            return;
          }

          if (tx >= SCREEN_W - refreshW - 6 && tx < SCREEN_W - 6 &&
              ty >= btnY && ty < btnY + btnH) {
            bleScrollTop = 0;
            drawChrome(); doScan(); drawChrome(); drawList(); continue;
          }

          int row = bleScrollTop + (ty - listY) / rowH;
          if (row >= 0 && row < deviceCount && ty >= listY) {
            BLEDev& d = devices[row];
            tft.fillScreen(C_BG);
            drawHeader("Device Info", true);
            int yPos = HEADER_H + 8;
            tft.setTextSize(1);
            tft.setTextColor(C_ACCENT); tft.drawString("Name:", 8, yPos);
            tft.setTextColor(C_TEXT);
            String nm = d.name.length() > 0 ? d.name : "(unknown)";
            if (nm.length() > 22) nm = nm.substring(0, 22);
            tft.drawString(nm.c_str(), 60, yPos); yPos += 14;
            tft.setTextColor(C_ACCENT); tft.drawString("MAC:", 8, yPos);
            tft.setTextColor(C_TEXT);
            tft.drawString(d.addr.c_str(), 60, yPos); yPos += 14;
            tft.setTextColor(C_ACCENT); tft.drawString("RSSI:", 8, yPos);
            tft.setTextColor(C_TEXT);
            char rs[16]; snprintf(rs, sizeof(rs), "%d dBm", d.rssi);
            tft.drawString(rs, 60, yPos); yPos += 14;
            if (d.mfgData.length() > 0) {
              tft.setTextColor(C_ACCENT); tft.drawString("Mfg:", 8, yPos); yPos += 12;
              tft.setTextColor(C_TEXT);
              String md = d.mfgData;
              while (md.length() > 0 && yPos < SCREEN_H - 60) {
                String line = md.length() > 36 ? md.substring(0, 36) : md;
                tft.drawString(line.c_str(), 8, yPos); yPos += 12;
                md = md.length() > 36 ? md.substring(36) : "";
              }
            }

            int huntY = SCREEN_H - 70;
            tft.drawFastHLine(8, huntY - 6, SCREEN_W - 16, C_SUBTEXT);
            Button huntBtn = { 8, (int16_t)huntY, SCREEN_W - 16, 44,
                               "HUNT THIS DEVICE", C_BTN, C_ACCENT2 };
            drawButton(huntBtn);
            bool backOut = false;
            while (true) {
              int16_t tx2, ty2;
              if (tftTouched(tx2, ty2)) {
                waitTouchRelease();
                if (backButtonHit(tx2, ty2)) { backOut = true; break; }
                if (tx2 >= 8 && tx2 < SCREEN_W - 8 &&
                    ty2 >= huntY && ty2 < huntY + 44) {
                  bleHuntDevice(d.addr, d.name.length() > 0 ? d.name : d.addr.substring(9));
                  break;
                }
              }
            }

            drawChrome(); drawList();
          }
        }
      }
    }
  }
}

//  Hunt BLE device
static void bleHuntDevice(const String& addr, const String& name) {
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  _huntTarget = addr;
  _huntRSSI   = -127;
  bool audioOn = true; // false if want audio off by default
  int  lastRssi = -127;
  auto drawChrome = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("Hunter", true);

    // Device name / addr
    tft.setTextColor(C_ACCENT2);
    tft.setTextSize(1);
    String label = name.length() > 0 ? name : addr;
    if (label.length() > 28) label = label.substring(0, 28);
    tft.drawString(label.c_str(), 8, HEADER_H + 6);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString(addr.c_str(), 8, HEADER_H + 18);

    // Audio toggle button may need to make bigger
    int audioY = SCREEN_H - 30;
    tft.fillRoundRect(SCREEN_W - 70, audioY, 60, 22, 4, audioOn ? C_BTN_HL : C_BTN);
    tft.drawRoundRect(SCREEN_W - 70, audioY, 60, 22, 4, audioOn ? C_ACCENT : C_BORDER);
    tft.setTextColor(audioOn ? C_ACCENT : C_SUBTEXT);
    tft.drawString(audioOn ? "AUDIO ON" : "AUDIO OFF", SCREEN_W - 64, audioY + 8);
  };

  auto drawRSSI = [&](int rssi) {
    tft.fillRect(0, HEADER_H + 36, SCREEN_W, SCREEN_H - HEADER_H - 66, C_BG);
    char s[16]; snprintf(s, sizeof(s), "%d", rssi);
    uint16_t color = C_RED;
    if (rssi > -50)      color = C_ACCENT;
    else if (rssi > -65) color = C_YELLOW;
    else if (rssi > -80) color = tft.color565(255, 140, 0);

    // UI 
    tft.setTextColor(color);
    tft.setTextSize(6);
    int w = strlen(s) * 36;
    tft.drawString(s, (SCREEN_W - w) / 2, HEADER_H + 40);
    tft.setTextColor(C_SUBTEXT);
    tft.setTextSize(1);
    tft.drawString("dBm", (SCREEN_W - 18) / 2, HEADER_H + 92);
    int barY = HEADER_H + 110;
    int barH = 24;
    int barX = 14;
    int barW = SCREEN_W - 28;
    tft.fillRect(barX, barY, barW, barH, C_PANEL);
    tft.drawRect(barX, barY, barW, barH, C_BORDER);
    int pct = constrain(map(rssi, -100, -30, 0, 100), 0, 100);
    tft.fillRect(barX + 1, barY + 1, (barW-2)*pct/100, barH-2, color);
    const char* dist =
      rssi > -45 ? "GOTCHA" :
      rssi > -60 ? "VERY CLOSE" :
      rssi > -75 ? "BETTER"      :
      rssi > -85 ? "COLD"     : "FAR";
    tft.fillRect(0, barY + barH + 4, SCREEN_W, 16, C_BG);
    tft.setTextColor(color);
    tft.setTextSize(2);
    int dw = strlen(dist) * 12;
    tft.drawString(dist, (SCREEN_W - dw) / 2, barY + barH + 6);
  };

  // Start scan with hunt
  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  HuntScanCallback huntCb;
  pBLEScan->setAdvertisedDeviceCallbacks(&huntCb);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();
  pBLEScan->start(2, false);
  uint32_t scanStart = millis();
  drawChrome();
  drawRSSI(_huntRSSI);
  int audioBtnY = SCREEN_H - 30;
  while (true) {
    if (millis() - scanStart > 2000) {
      pBLEScan->clearResults();
      pBLEScan->start(2, false);
      scanStart = millis();
    }
    if (_huntRSSI != lastRssi) {
      drawRSSI(_huntRSSI);
      lastRssi = _huntRSSI;
    }
    if (audioOn && _huntRSSI > -127) hunterBeep(_huntRSSI);
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) {
        pBLEScan->stop();
        noTone(SPEAKER_PIN);
        _huntTarget = "";
        tft.setTextDatum(TL_DATUM);
        tft.setFreeFont(NULL);
        tft.setTextSize(1);
        return;
      }
      if (tx >= SCREEN_W - 70 && tx < SCREEN_W - 10 &&
          ty >= audioBtnY && ty < audioBtnY + 22) {
        audioOn = !audioOn;
        if (!audioOn) noTone(SPEAKER_PIN);
        drawChrome();
        drawRSSI(lastRssi);
      }
    }
    delay(20);
  }
}

//  Chameleon Ultra BLE Client DID not write this code it was taken from the ChameleonUltraGUI source code and modified for the 2.4" Byte Shield
// RfidResearchGroup/ChameleonUltra — https://github.com/RfidResearchGroup/ChameleonUltra (protocol/command definitions)
// GameTec-live/ChameleonUltraGUI — https://github.com/GameTec-live/ChameleonUltraGUI (GUI reference implementation)
#define CHAMELEON_SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAMELEON_RX_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAMELEON_TX_UUID        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Command codes (verified from ChameleonUltraGUI source, decimal values)
#define CHAM_CMD_GET_APP_VERSION   1000
#define CHAM_CMD_SET_ACTIVE_SLOT   1003
#define CHAM_CMD_GET_ACTIVE_SLOT   1018
#define CHAM_CMD_GET_SLOT_INFO     1019
#define CHAM_CMD_GET_ENABLED       1023
#define CHAM_CMD_MF1_GET_ANTICOLL  4018
#define CHAM_CMD_MF1_GET_EMULATOR_CONFIG 4009
#define CHAM_CMD_MF1_SET_BLOCK_ANTICOLL_MODE 4015
#define CHAM_CMD_SET_SLOT_TAG_TYPE   1004
#define CHAM_CMD_SET_SLOT_DATA_DEFAULT 1005
#define CHAM_CMD_SET_SLOT_ENABLE     1006
#define CHAM_CMD_SAVE_SLOT_DATA      1009
#define CHAM_CMD_MF1_SET_ANTICOLL  4001
#define CHAM_CMD_CHANGE_DEV_MODE   1001
#define CHAM_CMD_SCAN_14A_TAG      2000
#define CHAM_CMD_GET_DEVICE_MODE   1002

static NimBLEClient*              _chamClient    = nullptr;
static NimBLERemoteCharacteristic* _chamRX       = nullptr;
static bool                       _chamConnected = false;
static String                     _chamResponse  = "";
static bool                       _chamGotResp   = false;
static uint8_t                    _chamRawData[512] = {};
static size_t                     _chamRawLen    = 0;
static uint16_t                   _chamRawStatus = 0;

// Last-read card data (for Read/Save)
static uint8_t  _cardUid[10] = {};
static uint8_t  _cardUidLen  = 0;
static uint8_t  _cardAtqa[2] = {};
static uint8_t  _cardSak     = 0;
static bool     _cardValid   = false;

static void chamNotifyCallback(NimBLERemoteCharacteristic* pChar,
                                uint8_t* pData, size_t length, bool isNotify) {
  // STATUS is bytes 4-5 (big endian)
  if (length >= 6) {
    _chamRawStatus = ((uint16_t)pData[4] << 8) | pData[5];
  }
  // DATA field starts at byte 9 (SOF|LRC1|CMD[2]|STATUS[2]|LEN[2]|LRC2 = 9 bytes header)
  if (length > 9) {
    _chamRawLen = (length - 9) < sizeof(_chamRawData) ? (length - 9) : sizeof(_chamRawData);
    memcpy(_chamRawData, pData + 9, _chamRawLen);
  } else {
    _chamRawLen = 0;
  }

  // Build printable string for firmware version (data is ASCII)
  _chamResponse = "";
  for (size_t i = 0; i < _chamRawLen; i++) {
    if (_chamRawData[i] >= 0x20 && _chamRawData[i] < 0x7F) _chamResponse += (char)_chamRawData[i];
  }
  if (_chamResponse.length() == 0) {
    for (size_t i = 0; i < _chamRawLen; i++) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X ", _chamRawData[i]);
      _chamResponse += buf;
    }
  }
  _chamGotResp = true;
}

static bool chamConnect(const String& addr) {
  Serial.printf("[CHAM] Creating client, connecting to %s\n", addr.c_str());
  _chamClient = NimBLEDevice::createClient();
  NimBLEAddress bleAddr(addr.c_str(), BLE_ADDR_RANDOM);
  if (!_chamClient->connect(bleAddr)) {
    Serial.println("[CHAM] connect() failed");
    return false;
  }
  Serial.println("[CHAM] Connected, getting service...");

  NimBLERemoteService* svc = _chamClient->getService(CHAMELEON_SERVICE_UUID);
  if (!svc) {
    Serial.println("[CHAM] Service not found");
    _chamClient->disconnect();
    return false;
  }
  Serial.println("[CHAM] Service found, getting characteristics...");

  _chamRX = svc->getCharacteristic(CHAMELEON_RX_UUID);
  NimBLERemoteCharacteristic* tx = svc->getCharacteristic(CHAMELEON_TX_UUID);
  if (!_chamRX || !tx) {
    Serial.printf("[CHAM] Missing characteristics RX=%d TX=%d\n", _chamRX != nullptr, tx != nullptr);
    _chamClient->disconnect();
    return false;
  }
  Serial.println("[CHAM] Subscribing to TX notifications...");
  tx->subscribe(true, chamNotifyCallback);
  _chamConnected = true;
  Serial.println("[CHAM] chamConnect success");
  return true;
}

static bool chamSendCmd(uint16_t cmd, const uint8_t* data, uint8_t dataLen) {
  if (!_chamConnected || !_chamRX) return false;
  uint8_t pkt[64];
  pkt[0] = 0x11; // SOF
  pkt[1] = (uint8_t)((0x100 - pkt[0]) & 0xFF); // LRC1 over SOF
  pkt[2] = (cmd >> 8) & 0xFF;
  pkt[3] = cmd & 0xFF;
  pkt[4] = 0x00; // status hi
  pkt[5] = 0x00; // status lo
  pkt[6] = (dataLen >> 8) & 0xFF; // len hi
  pkt[7] = dataLen & 0xFF;        // len lo
  // LRC2 = additive LRC over bytes [2..7]
  uint16_t sum2 = 0;
  for (int i = 2; i < 8; i++) sum2 += pkt[i];
  pkt[8] = (uint8_t)((0x100 - (sum2 & 0xFF)) & 0xFF);
  for (int i = 0; i < dataLen; i++) pkt[9 + i] = data[i];
  // LRC3 = additive LRC over entire frame so far (bytes 0..8+dataLen-1)
  uint16_t sum3 = 0;
  for (int i = 0; i < 9 + dataLen; i++) sum3 += pkt[i];
  pkt[9 + dataLen] = (uint8_t)((0x100 - (sum3 & 0xFF)) & 0xFF);
  _chamGotResp = false;
  return _chamRX->writeValue(pkt, 10 + dataLen, false);
}

static void bleChameleonUltra() {
  tft.fillScreen(C_BG);
  drawHeader("Chameleon Ultra", true);

  enum ChamState { CHAM_SCAN, CHAM_CONNECTING, CHAM_CONNECTED, CHAM_ERROR };
  ChamState state = CHAM_SCAN;
  String foundAddr = "";
  String foundName = "";
  String statusLine = "Scanning...";
  String fwVersion = "";
  uint32_t stateStart = millis();
  bool redraw = true;

  bool emulateMode = false; // tracks current device mode (false=emulator/0, true=reader/1)

  // Slot state
  uint8_t  activeSlot = 0;
  bool     slotEnabled[8] = {};
  String   slotType[8];
  bool     slotsLoaded = false;

  struct ChamScanCb : public NimBLEAdvertisedDeviceCallbacks {
    String* addr; String* name;
    void onResult(NimBLEAdvertisedDevice* dev) override {
      if (!dev->haveName()) return;
      String n = String(dev->getName().c_str());
      Serial.printf("[CHAM] Saw device: %s\n", n.c_str());
      if (n.indexOf("Chameleon") >= 0 || n.indexOf("CU-") >= 0) {
        *addr = String(dev->getAddress().toString().c_str());
        *name = n;
        Serial.printf("[CHAM] Found target: %s @ %s\n", n.c_str(), addr->c_str());
        NimBLEDevice::getScan()->stop();
      }
    }
  } scanCb;
  scanCb.addr = &foundAddr;
  scanCb.name = &foundName;

  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(&scanCb);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->start(8, false);
  Serial.println("[CHAM] Scan started");

  // Helper: send cmd and wait for response
  auto sendAndWait = [](uint16_t cmd, const uint8_t* data, uint8_t len) -> bool {
    _chamGotResp = false;
    chamSendCmd(cmd, data, len);
    uint32_t t = millis();
    while (!_chamGotResp && millis() - t < 2000) delay(30);
    return _chamGotResp;
  };

  // Load all slot info from device
  auto loadSlots = [&]() {
    // Get active slot
    if (sendAndWait(CHAM_CMD_GET_ACTIVE_SLOT, nullptr, 0)) {
      // Response data byte 7 is slot index 0-7
      // _chamResponse is hex string, parse raw bytes via callback
      // We'll store raw bytes in a secondary buffer
    }
    slotsLoaded = true;
  };

  // Override notify callback to also store raw bytes
  // We need to re-subscribe with a lambda — NimBLE supports this via a wrapper
  // Instead, use a flag and parse _chamResponse hex string
  auto parseHexResponse = [](uint8_t* out, size_t maxLen) -> size_t {
    size_t count = 0;
    String r = _chamResponse;
    // If it was converted to printable text, can't parse as hex
    // Check if it looks like hex (contains spaces and hex chars only)
    bool isHex = true;
    for (int i = 0; i < (int)r.length(); i++) {
      char c = r.charAt(i);
      if (!isxdigit(c) && c != ' ') { isHex = false; break; }
    }
    if (!isHex) return 0;
    int pos = 0;
    while (pos < (int)r.length() && count < maxLen) {
      while (pos < (int)r.length() && r.charAt(pos) == ' ') pos++;
      if (pos + 1 >= (int)r.length()) break;
      char hi = r.charAt(pos++);
      char lo = r.charAt(pos++);
      char buf[3] = {hi, lo, 0};
      out[count++] = (uint8_t)strtoul(buf, nullptr, 16);
    }
    return count;
  };

  auto drawConnected = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("Chameleon Ultra", true);

    // Status bar
    tft.fillRect(0, HEADER_H, SCREEN_W, 18, C_PANEL);
    tft.setTextColor(C_ACCENT); tft.setTextSize(1);
    tft.drawString("CONNECTED", 8, HEADER_H + 5);
    tft.setTextColor(C_SUBTEXT);
    String fw = fwVersion.length() > 0 ? "FW: " + fwVersion : "FW: unknown";
    tft.drawString(fw.c_str(), SCREEN_W - fw.length() * 6 - 6, HEADER_H + 5);

    // 8 slot grid — 2 rows of 4
    const int gridY = HEADER_H + 24;
    const int cellW = (SCREEN_W - 16) / 4;
    const int cellH = 38;
    const int gap   = 4;

    for (int i = 0; i < 8; i++) {
      int col = i % 4;
      int row = i / 4;
      int x = 8 + col * (cellW + gap);
      int y = gridY + row * (cellH + gap);

      bool isActive  = (i == activeSlot);
      bool isEnabled = slotEnabled[i];

      uint16_t bgColor    = isActive  ? C_ACCENT  : isEnabled ? C_BTN : C_PANEL;
      uint16_t borderColor = isActive ? C_ACCENT  : isEnabled ? C_BORDER : C_PANEL;
      uint16_t textColor  = isActive  ? C_BG      : isEnabled ? C_TEXT : C_SUBTEXT;

      tft.fillRoundRect(x, y, cellW, cellH, 4, bgColor);
      tft.drawRoundRect(x, y, cellW, cellH, 4, borderColor);

      tft.setTextColor(textColor); tft.setTextSize(1);
      char slotLabel[4];
      snprintf(slotLabel, sizeof(slotLabel), "S%d", i + 1);
      tft.drawString(slotLabel, x + cellW/2 - 6, y + 6);

      tft.setTextColor(isActive ? C_BG : C_SUBTEXT);
      String typeLbl = slotType[i].length() > 0 ? slotType[i] : (isEnabled ? "---" : "off");
      if (typeLbl.length() > 5) typeLbl = typeLbl.substring(0, 5);
      tft.drawString(typeLbl.c_str(), x + 2, y + 22);
    }

    // row 1: Refresh / Disconnect
    int btnY = gridY + 2 * (cellH + gap) + 8;
    int halfW = (SCREEN_W - 24) / 2;

    tft.fillRoundRect(8,            btnY, halfW, 28, 4, C_BTN);
    tft.drawRoundRect(8,            btnY, halfW, 28, 4, C_ACCENT);
    tft.setTextColor(C_ACCENT); tft.setTextSize(1);
    tft.drawString("REFRESH", 8 + halfW/2 - 18, btnY + 9);

    tft.fillRoundRect(16 + halfW,   btnY, halfW, 28, 4, C_RED);
    tft.drawRoundRect(16 + halfW,   btnY, halfW, 28, 4, C_RED);
    tft.setTextColor(C_BG);
    tft.drawString("DISCONNECT", 16 + halfW + halfW/2 - 28, btnY + 9);

    // row 2: Read / Save
    int btnY2 = btnY + 28 + 6;

    tft.fillRoundRect(8,            btnY2, halfW, 28, 4, C_BTN);
    tft.drawRoundRect(8,            btnY2, halfW, 28, 4, C_YELLOW);
    tft.setTextColor(C_YELLOW); tft.setTextSize(1);
    tft.drawString("READ CARD", 8 + halfW/2 - 26, btnY2 + 9);

    tft.fillRoundRect(16 + halfW,   btnY2, halfW, 28, 4, _cardValid ? C_BTN : C_PANEL);
    tft.drawRoundRect(16 + halfW,   btnY2, halfW, 28, 4, _cardValid ? C_ACCENT : C_BORDER);
    tft.setTextColor(_cardValid ? C_ACCENT : C_SUBTEXT);
    tft.drawString("SAVE TO SLOT", 16 + halfW + halfW/2 - 32, btnY2 + 9);

    // Card info line
    int infoY = btnY2 + 28 + 6;
    tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
    if (_cardValid) {
      char info[48];
      String uidStr = "";
      for (int i = 0; i < _cardUidLen; i++) {
        char b[4]; snprintf(b, sizeof(b), "%02X ", _cardUid[i]);
        uidStr += b;
      }
      snprintf(info, sizeof(info), "UID: %s SAK:%02X", uidStr.c_str(), _cardSak);
      tft.drawString(info, 8, infoY);
    } else {
      tft.drawString("No card read yet", 8, infoY);
    }

    // Emulate toggle button - centered, near bottom
    int emuW = 100, emuH = 26;
    int emuX = (SCREEN_W - emuW) / 2;
    int emuY = SCREEN_H - emuH - 4;
    uint16_t emuColor = emulateMode ? C_PANEL : C_ACCENT;
    tft.fillRoundRect(emuX, emuY, emuW, emuH, 4, emulateMode ? C_BTN : C_ACCENT);
    tft.drawRoundRect(emuX, emuY, emuW, emuH, 4, emuColor);
    tft.setTextColor(emulateMode ? C_ACCENT : C_BG); tft.setTextSize(1);
    tft.drawString(emulateMode ? "EMULATE: ON" : "EMULATE: OFF", emuX + 8, emuY + 9);
  };

  auto refreshSlots = [&]() {
    // Get active slot 
    if (sendAndWait(CHAM_CMD_GET_ACTIVE_SLOT, nullptr, 0)) {
      Serial.printf("[CHAM] GET_ACTIVE_SLOT raw len=%d\n", _chamRawLen);
      if (_chamRawLen >= 1) {
        activeSlot = _chamRawData[0];
        Serial.printf("[CHAM] Active slot: %d\n", activeSlot);
      }
    }
    // Get enabled slots 
    if (sendAndWait(CHAM_CMD_GET_ENABLED, nullptr, 0)) {
      Serial.printf("[CHAM] GET_ENABLED raw len=%d\n", _chamRawLen);
      if (_chamRawLen >= 16) {
        for (int i = 0; i < 8; i++) {
          slotEnabled[i] = (_chamRawData[i*2] != 0 || _chamRawData[i*2 + 1] != 0);
          Serial.printf("[CHAM] Slot %d enabled: %d\n", i, slotEnabled[i]);
        }
      }
    }

    // Get slot tag types 
    if (sendAndWait(CHAM_CMD_GET_SLOT_INFO, nullptr, 0)) {
      Serial.printf("[CHAM] GET_SLOT_INFO raw len=%d\n", _chamRawLen);
      if (_chamRawLen >= 32) {
        for (int i = 0; i < 8; i++) {
          uint16_t hfType = ((uint16_t)_chamRawData[i*4]     << 8) | _chamRawData[i*4 + 1];
          uint16_t lfType = ((uint16_t)_chamRawData[i*4 + 2] << 8) | _chamRawData[i*4 + 3];
          Serial.printf("[CHAM] Slot %d hf=%d lf=%d\n", i, hfType, lfType);
          if (hfType >= 1000 && hfType <= 1003) {
            switch (hfType) {
              case 1000: slotType[i] = "M-MIN"; break;
              case 1001: slotType[i] = "M-1K";  break;
              case 1002: slotType[i] = "M-2K";  break;
              case 1003: slotType[i] = "M-4K";  break;
            }
          } else if (hfType >= 1100 && hfType <= 1108) {
            slotType[i] = "NTAG";
          } else if (lfType >= 100 && lfType <= 104) {
            slotType[i] = "EM410x";
          } else if (lfType == 200 || lfType == 201) {
            slotType[i] = "HID";
          } else if (hfType == 0 && lfType == 0) {
            slotType[i] = "";
          } else {
            slotType[i] = "?";
          }
        }
      }
    }
    slotsLoaded = true;
  };

  auto drawScreen = [&]() {
    if (state == CHAM_CONNECTED) {
      drawConnected();
    } else {
      tft.fillScreen(C_BG);
      drawHeader("Chameleon Ultra", true);
      int y = HEADER_H + 14;
      tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
      tft.drawString("Status:", 10, y);
      tft.setTextColor(state == CHAM_ERROR ? C_RED : C_YELLOW);
      tft.drawString(statusLine.c_str(), 10, y + 14);
      if (foundAddr.length() > 0) {
        tft.setTextColor(C_SUBTEXT); y += 36;
        tft.drawString("Device:", 10, y);
        tft.setTextColor(C_TEXT);
        tft.drawString(foundName.length() > 0 ? foundName.c_str() : foundAddr.c_str(), 10, y + 14);
      }
      // Buttons
      int btnY = SCREEN_H - 50;
      tft.fillRoundRect(10, btnY, SCREEN_W - 20, 36, 5, C_BTN);
      if (state == CHAM_SCAN && foundAddr.length() > 0) {
        tft.drawRoundRect(10, btnY, SCREEN_W - 20, 36, 5, C_ACCENT);
        tft.setTextColor(C_ACCENT); tft.setTextSize(1);
        tft.drawString("CONNECT", SCREEN_W/2 - 22, btnY + 12);
      } else if (state == CHAM_SCAN) {
        tft.drawRoundRect(10, btnY, SCREEN_W - 20, 36, 5, C_ACCENT);
        tft.setTextColor(C_ACCENT); tft.setTextSize(1);
        tft.drawString("SCAN AGAIN", SCREEN_W/2 - 28, btnY + 12);
      } else if (state == CHAM_ERROR) {
        tft.drawRoundRect(10, btnY, SCREEN_W - 20, 36, 5, C_YELLOW);
        tft.setTextColor(C_YELLOW); tft.setTextSize(1);
        tft.drawString("RETRY", SCREEN_W/2 - 14, btnY + 12);
      }
    }
  };

  while (true) {
    if (redraw) { drawScreen(); redraw = false; }

    if (state == CHAM_SCAN && foundAddr.length() > 0) {
      if (statusLine != "Found: " + foundName) {
        statusLine = "Found: " + foundName;
        redraw = true;
      }
    }

    if (state == CHAM_CONNECTING) {
      statusLine = "Connecting...";
      drawScreen();
      bool ok = chamConnect(foundAddr);
      if (ok) {
        state = CHAM_CONNECTED;
        statusLine = "Connected!";
        if (sendAndWait(CHAM_CMD_GET_APP_VERSION, nullptr, 0))
          fwVersion = _chamResponse;
        refreshSlots();
      } else {
        state = CHAM_ERROR;
        statusLine = "Connect failed";
        if (_chamClient) { NimBLEDevice::deleteClient(_chamClient); _chamClient = nullptr; }
        _chamConnected = false;
      }
      redraw = true;
    }

    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();

      if (backButtonHit(tx, ty)) {
        pScan->stop();
        if (_chamClient && _chamConnected) _chamClient->disconnect();
        if (_chamClient) { NimBLEDevice::deleteClient(_chamClient); _chamClient = nullptr; }
        _chamConnected = false;
        pScan->setAdvertisedDeviceCallbacks(nullptr);
        return;
      }

      if (state == CHAM_CONNECTED) {
        const int gridY  = HEADER_H + 24;
        const int cellW  = (SCREEN_W - 16) / 4;
        const int cellH  = 38;
        const int gap    = 4;
        const int btnY   = gridY + 2 * (cellH + gap) + 8;
        const int halfW  = (SCREEN_W - 24) / 2;

        // Tap a slot cell to activate it
        if (ty >= gridY && ty < gridY + 2 * (cellH + gap)) {
          int col = (tx - 8) / (cellW + gap);
          int row = (ty - gridY) / (cellH + gap);
          if (col >= 0 && col < 4 && row >= 0 && row < 2) {
            int slot = row * 4 + col;
            uint8_t slotByte = (uint8_t)slot;
            chamSendCmd(CHAM_CMD_SET_ACTIVE_SLOT, &slotByte, 1);
            delay(100);
            activeSlot = slot;
            redraw = true;          
          }
        }

        const int btnY2 = btnY + 28 + 6;

        // Refresh button
        if (tx >= 8 && tx < 8 + halfW && ty >= btnY && ty < btnY + 28) {
          refreshSlots();
          redraw = true;
        }

        // Read Card button
        if (tx >= 8 && tx < 8 + halfW && ty >= btnY2 && ty < btnY2 + 28) {
          uint8_t readerMode = 1;
          sendAndWait(CHAM_CMD_CHANGE_DEV_MODE, &readerMode, 1);
          delay(500);
          if (sendAndWait(CHAM_CMD_GET_DEVICE_MODE, nullptr, 0)) {
            Serial.printf("[CHAM] GET_MODE status=0x%04X len=%d val=%d\n",
                           _chamRawStatus, _chamRawLen, _chamRawLen >= 1 ? _chamRawData[0] : -1);
          }
          delay(800); // give HF field time to fully activate after mode switch
          if (sendAndWait(CHAM_CMD_SCAN_14A_TAG, nullptr, 0)) {
            Serial.printf("[CHAM] SCAN14A status=0x%04X raw len=%d\n", _chamRawStatus, _chamRawLen);
            if (_chamRawLen >= 1) {
              _cardUidLen = _chamRawData[0];
              if (_cardUidLen > 0 && _cardUidLen <= 10 && _chamRawLen >= (size_t)(1 + _cardUidLen + 3)) {
                memcpy(_cardUid, &_chamRawData[1], _cardUidLen);
                _cardAtqa[0] = _chamRawData[1 + _cardUidLen];
                _cardAtqa[1] = _chamRawData[1 + _cardUidLen + 1];
                _cardSak     = _chamRawData[1 + _cardUidLen + 2];
                _cardValid   = true;
                Serial.println("[CHAM] Card read OK");
              } else {
                _cardValid = false;
                Serial.println("[CHAM] No card / empty UID");
              }
            }
          }
          uint8_t emuMode = 0;
          sendAndWait(CHAM_CMD_CHANGE_DEV_MODE, &emuMode, 1);
          delay(200);
          redraw = true;
        }

        // Save to Slot button — Mifare Classic 1K, writes to active slot
        if (_cardValid && tx >= 16 + halfW && tx < 16 + halfW + halfW && ty >= btnY2 && ty < btnY2 + 28) {
          waitTouchRelease();

          // 0x03E9 = 1001 = Mifare Classic 1K
          const uint8_t TYPE_HI = 0x03;
          const uint8_t TYPE_LO = 0xE9;
          uint8_t slot = activeSlot;
          uint8_t buf[3];

          // 1) emulator mode
          uint8_t emuMode2 = 0;
          sendAndWait(CHAM_CMD_CHANGE_DEV_MODE, &emuMode2, 1);
          delay(200);

          // 2) enable this slot 
          buf[0] = slot; buf[1] = 2; buf[2] = 1;
          sendAndWait(CHAM_CMD_SET_SLOT_ENABLE, buf, 3);
          delay(100);

          // 3) make it the active slot
          sendAndWait(CHAM_CMD_SET_ACTIVE_SLOT, &slot, 1);
          delay(100);

          // 4) set tag type
          buf[0] = slot; buf[1] = TYPE_HI; buf[2] = TYPE_LO;
          sendAndWait(CHAM_CMD_SET_SLOT_TAG_TYPE, buf, 3);
          delay(100);

          // 5) lay down default data for that type
          sendAndWait(CHAM_CMD_SET_SLOT_DATA_DEFAULT, buf, 3);
          delay(100);

          // 6) set anti-collision
          uint8_t payload[16];
          int p = 0;
          payload[p++] = _cardUidLen;
          memcpy(&payload[p], _cardUid, _cardUidLen); p += _cardUidLen;
          payload[p++] = _cardAtqa[0];
          payload[p++] = _cardAtqa[1];
          payload[p++] = _cardSak;
          payload[p++] = 0; // ATS length = 0
          if (sendAndWait(CHAM_CMD_MF1_SET_ANTICOLL, payload, p)) {
            Serial.printf("[CHAM] Anti-coll set, status=0x%04X\n", _chamRawStatus);
          }
          delay(200);

          // 7) persist slot to flash
          sendAndWait(CHAM_CMD_SAVE_SLOT_DATA, nullptr, 0);
          delay(300);

          // verify: read back the slot's tag type
          if (sendAndWait(CHAM_CMD_GET_SLOT_INFO, nullptr, 0) && _chamRawLen >= 32) {
            uint16_t hf = ((uint16_t)_chamRawData[slot*4] << 8) | _chamRawData[slot*4 + 1];
            Serial.printf("[CHAM] VERIFY slot %d hf type now = %d (want 1001)\n", slot, hf);
          }

          activeSlot = slot;
          slotEnabled[slot] = true;
          slotType[slot] = "M-1K";
          redraw = true;
        }

        // toggle button
        {
          int emuW = 100, emuH = 26;
          int emuX = (SCREEN_W - emuW) / 2;
          int emuY = SCREEN_H - emuH - 4;
          if (tx >= emuX && tx < emuX + emuW && ty >= emuY && ty < emuY + emuH) {
            emulateMode = !emulateMode;
            uint8_t mode = emulateMode ? 1 : 0; // 1=reader, 0=emulator
            bool ok = sendAndWait(CHAM_CMD_CHANGE_DEV_MODE, &mode, 1);
            Serial.printf("[CHAM] EMULATE TOGGLE -> mode=%d ok=%d status=0x%04X\n", mode, ok, _chamRawStatus);
            delay(200);
            redraw = true;
          }
        }

        // Disconnect button
        if (tx >= 16 + halfW && tx < 16 + halfW + halfW && ty >= btnY && ty < btnY + 28) {
          if (_chamClient) { _chamClient->disconnect(); NimBLEDevice::deleteClient(_chamClient); _chamClient = nullptr; }
          _chamConnected = false;
          foundAddr = ""; foundName = ""; fwVersion = "";
          for (int i = 0; i < 8; i++) { slotEnabled[i] = false; slotType[i] = ""; }
          slotsLoaded = false;
          _cardValid = false;
          state = CHAM_SCAN;
          statusLine = "Scanning...";
          stateStart = millis();
          pScan->clearResults();
          pScan->start(8, false);
          redraw = true;
        }

      } else {
        // Pre-connect button 
        int btnY = SCREEN_H - 50;
        if (ty >= btnY && ty < btnY + 36) {
          if (state == CHAM_SCAN && foundAddr.length() == 0) {
            foundAddr = ""; foundName = "";
            stateStart = millis();
            statusLine = "Scanning...";
            pScan->clearResults();
            pScan->start(8, false);
            redraw = true;
          } else if (state == CHAM_SCAN && foundAddr.length() > 0) {
            pScan->stop();
            state = CHAM_CONNECTING;
          } else if (state == CHAM_ERROR) {
            state = CHAM_SCAN;
            foundAddr = ""; foundName = "";
            statusLine = "Scanning...";
            stateStart = millis();
            pScan->clearResults();
            pScan->start(8, false);
            redraw = true;
          }
        }
      }
    }
    delay(20);
  }
}

//  Tracker Locator needs some work
enum TrackerType { TT_UNKNOWN=0, TT_AIRTAG, TT_GOOGLE_FMDN, TT_SAMSUNG_SMARTTAG, TT_TILE };
struct Tracker { String addr; TrackerType type; int rssi; uint32_t lastSeen; };
#define MAX_TRACKERS 10
static Tracker trackers[MAX_TRACKERS];
static int trackerCount = 0;
static const char* trackerTypeName(TrackerType t) {
  switch(t) {
    case TT_AIRTAG: return "AirTag/FindMy";
    case TT_GOOGLE_FMDN: return "Google FMDN";
    case TT_SAMSUNG_SMARTTAG: return "Samsung Tag";
    case TT_TILE: return "Tile";
    default: return "Unknown";
  }
}
static uint16_t trackerTypeColor(TrackerType t) {
  switch(t) {
    case TT_AIRTAG: return C_CYAN;
    case TT_GOOGLE_FMDN: return C_ACCENT;
    case TT_SAMSUNG_SMARTTAG: return C_YELLOW;
    case TT_TILE: return C_ACCENT2;
    default: return C_SUBTEXT;
  }
}
static int findTracker(const String& addr) {
  for (int i=0; i<trackerCount; i++) if (trackers[i].addr==addr) return i;
  return -1;
}
static void addOrUpdateTracker(const String& addr, TrackerType type, int rssi) {
  int idx = findTracker(addr);
  if (idx >= 0) { trackers[idx].rssi=rssi; trackers[idx].lastSeen=millis(); return; }
  if (trackerCount < MAX_TRACKERS) trackers[trackerCount++] = {addr, type, rssi, millis()};
}
static TrackerType classifyDevice(NimBLEAdvertisedDevice& dev) {
  if (dev.haveManufacturerData()) {
    std::string md = dev.getManufacturerData();
    if (md.length() >= 4) {
      uint8_t b0=(uint8_t)md[0], b1=(uint8_t)md[1], b2=(uint8_t)md[2];
      if (b0==0x4C && b1==0x00 && (b2==0x12||b2==0x07)) return TT_AIRTAG;
      if (b0==0x75 && b1==0x00) return TT_SAMSUNG_SMARTTAG;
    }
  }
  if (dev.haveServiceUUID()) {
    for (int i=0; i<dev.getServiceUUIDCount(); i++) {
      String uuid = String(dev.getServiceUUID(i).toString().c_str()); uuid.toLowerCase();
      if (uuid.indexOf("feaa")>=0||uuid.indexOf("fef3")>=0||uuid.indexOf("fe2c")>=0) return TT_GOOGLE_FMDN;
      if (uuid.indexOf("feed")>=0||uuid.indexOf("feec")>=0||uuid.indexOf("fd43")>=0) return TT_TILE;
    }
  }
  return TT_UNKNOWN;
}
class TrackerScanCallback : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    TrackerType tt = classifyDevice(*dev);
    if (tt == TT_UNKNOWN) return;
    addOrUpdateTracker(String(dev->getAddress().toString().c_str()), tt, dev->getRSSI());
  }
};
static uint32_t lastBeep = 0;
static void hunterBeep(int rssi) {
  int interval = rssi > -40 ? 80 : rssi > -55 ? 180 : rssi > -70 ? 400 : rssi > -85 ? 800 : 1500;
  if (millis() - lastBeep > (uint32_t)interval) { tone(SPEAKER_PIN, 1800, 30); lastBeep = millis(); }
}
static void hunterView(int trackerIdx) {
  tft.setTextDatum(TL_DATUM); tft.setFreeFont(NULL); tft.setTextSize(1);
  bool audioOn = true; int lastRssi = -127;
  auto drawChrome = [&]() {
    tft.fillScreen(C_BG); drawHeader("Hunter", true);
    Tracker& t = trackers[trackerIdx];
    tft.setTextColor(trackerTypeColor(t.type)); tft.setTextSize(1);
    tft.drawString(trackerTypeName(t.type), 8, HEADER_H+6);
    tft.setTextColor(C_SUBTEXT); tft.drawString(t.addr.c_str(), 8, HEADER_H+18);
    int audioY = SCREEN_H-30;
    tft.fillRoundRect(SCREEN_W-70, audioY, 60, 22, 4, audioOn?C_BTN_HL:C_BTN);
    tft.drawRoundRect(SCREEN_W-70, audioY, 60, 22, 4, audioOn?C_ACCENT:C_BORDER);
    tft.setTextColor(audioOn?C_ACCENT:C_SUBTEXT);
    tft.drawString(audioOn?"AUDIO ON":"AUDIO OFF", SCREEN_W-64, audioY+8);
  };
  auto drawRSSI = [&](int rssi) {
    tft.fillRect(0, HEADER_H+36, SCREEN_W, 80, C_BG);
    char s[16]; snprintf(s, sizeof(s), "%d", rssi);
    uint16_t color = rssi>-50?C_ACCENT:rssi>-65?C_YELLOW:rssi>-80?tft.color565(255,140,0):C_RED;
    tft.setTextColor(color); tft.setTextSize(6);
    tft.drawString(s, (SCREEN_W-strlen(s)*36)/2, HEADER_H+40);
    tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
    tft.drawString("dBm", (SCREEN_W-18)/2, HEADER_H+92);
    int barY=HEADER_H+130, barH=28, barX=14, barW=SCREEN_W-28;
    tft.fillRect(barX,barY,barW,barH,C_PANEL); tft.drawRect(barX,barY,barW,barH,C_BORDER);
    int pct=constrain(map(rssi,-100,-30,0,100),0,100);
    tft.fillRect(barX+1,barY+1,(barW-2)*pct/100,barH-2,color);
    const char* dist = rssi>-45?"RIGHT HERE":rssi>-60?"VERY CLOSE":rssi>-75?"CLOSE":rssi>-85?"NEARBY":"FAR";
    tft.fillRect(0,barY+barH+4,SCREEN_W,14,C_BG);
    tft.setTextColor(color); tft.setTextSize(2);
    tft.drawString(dist,(SCREEN_W-strlen(dist)*12)/2,barY+barH+6);
  };

  String targetAddr = trackers[trackerIdx].addr;
  drawChrome(); drawRSSI(trackers[trackerIdx].rssi);
  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  pBLEScan->clearResults(); pBLEScan->start(2, false);
  uint32_t scanStart = millis();
  int audioBtnY = SCREEN_H-30;
  while (true) {
    if (millis()-scanStart > 2000) { pBLEScan->clearResults(); pBLEScan->start(2,false); scanStart=millis(); }
    int idx = findTracker(targetAddr);
    if (idx>=0) {
      int rssi = trackers[idx].rssi;
      if (rssi!=lastRssi) { drawRSSI(rssi); lastRssi=rssi; }
      if (audioOn) hunterBeep(rssi);
    }
    int16_t tx,ty;
    if (tftTouched(tx,ty)) {
      waitTouchRelease();
      if (backButtonHit(tx,ty)) { pBLEScan->stop(); noTone(SPEAKER_PIN); tft.setTextDatum(TL_DATUM); tft.setFreeFont(NULL); tft.setTextSize(1); return; }
      if (tx>=SCREEN_W-70&&tx<SCREEN_W-10&&ty>=audioBtnY&&ty<audioBtnY+22) {
        audioOn=!audioOn; if(!audioOn) noTone(SPEAKER_PIN); drawChrome(); drawRSSI(lastRssi);
      }
    }
    delay(20);
  }
}


//  Pwnagotchi Hunter
struct PwnHit {
  String   addr;
  String   name;
  int      rssi;
  uint32_t lastSeen;
};

#define MAX_PWN_HITS 2
static PwnHit pwnHits[MAX_PWN_HITS];
static int    pwnCount = 0;

static void addOrUpdatePwn(const String& addr, const String& name, int rssi) {
  for (int i = 0; i < pwnCount; i++) {
    if (pwnHits[i].addr == addr) {
      pwnHits[i].rssi     = rssi;
      pwnHits[i].lastSeen = millis();
      if (name.length() > 0) pwnHits[i].name = name;
      return;
    }
  }
  if (pwnCount < MAX_PWN_HITS)
    pwnHits[pwnCount++] = { addr, name, rssi, millis() };
}

class PwnScanCallback : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    String name = dev->haveName() ? String(dev->getName().c_str()) : "";
    String nameLower = name;
    nameLower.toLowerCase();

    if (nameLower.indexOf("pwnagotchi") < 0 &&
        nameLower.indexOf("pwn") < 0) return;

    addOrUpdatePwn(
      String(dev->getAddress().toString().c_str()),
      name,
      dev->getRSSI()
    );
  }
};

static void blePwnagotchi() {
  for (int i = 0; i < pwnCount; i++) {
    pwnHits[i].addr = "";
    pwnHits[i].name = "";
  }
  pwnCount = 0;
  const int rowH  = 36;
  const int listY = HEADER_H + 4;

  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  PwnScanCallback pwnCb;
  pBLEScan->setAdvertisedDeviceCallbacks(&pwnCb);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  auto drawChrome = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("Pwnagotchi Hunt", true);
  };

  auto drawList = [&]() {
    tft.fillRect(0, listY, SCREEN_W, SCREEN_H - listY, C_BG);
    if (pwnCount == 0) {
      tft.setFreeFont(NULL); tft.setTextSize(1);
      tft.setTextColor(C_SUBTEXT);
      tft.drawString("Scanning Pwnagotchi", 8, listY + 10);
      tft.drawString("Device active?", 8, listY + 26);
      tft.drawString("and BLE?", 8, listY + 42);
      tft.setTextColor(C_ACCENT);
      tft.drawString("Range 20 feet", 8, listY + 62);
      return;
    }
    int maxRows = (SCREEN_H - listY - 4) / rowH;
    int count   = min(pwnCount, maxRows);
    for (int i = 0; i < count; i++) {
      int y = listY + i * rowH;
      PwnHit& h = pwnHits[i]; // needs 2
      tft.fillRect(0, y, SCREEN_W, rowH - 1, C_PANEL);
      tft.drawFastHLine(0, y + rowH - 1, SCREEN_W, C_BORDER);

      // Icon
      tft.fillRect(4, y + 6, 18, 22, 0xFD40);
      tft.setFreeFont(NULL); tft.setTextSize(1);
      tft.setTextColor(0x0000);
      tft.drawString("PW", 6, y + 13);

      // Name
      tft.setTextColor(0xFD40);
      String label = h.name.length() > 0 ? h.name : "pwnagotchi";
      if (label.length() > 18) label = label.substring(0, 17) + "~";
      tft.drawString(label.c_str(), 28, y + 6);

      // Addr
      tft.setTextColor(C_SUBTEXT);
      tft.drawString(h.addr.substring(9).c_str(), 28, y + 19);

      // Signal
      uint16_t rcolor = h.rssi > -60 ? 0xFD40 : h.rssi > -80 ? 0xFFE0 : 0xF800;
      char rs[8]; snprintf(rs, sizeof(rs), "%d", h.rssi);
      tft.setTextColor(rcolor); tft.setTextSize(2);
      tft.drawString(rs, SCREEN_W - strlen(rs) * 12 - 8, y + 10);
    }
  };

  drawChrome();
  drawList();
  pBLEScan->stop();
  delay(50);
  pBLEScan->clearResults();
  pBLEScan->start(3, false);
  uint32_t scanStart = millis();
  uint32_t lastDraw  = millis();
  int      lastCount = pwnCount;

  while (true) {
    if (millis() - scanStart > 3000) {
      pBLEScan->clearResults();
      pBLEScan->start(3, false);
      scanStart = millis();
    }
    if (pwnCount != lastCount || millis() - lastDraw > 500) {
      drawList();
      lastDraw  = millis();
      lastCount = pwnCount;
    }
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) {
        pBLEScan->stop();
        pBLEScan->clearResults();
        pBLEScan->setAdvertisedDeviceCallbacks(nullptr);
        return;
      }
      // Tap a hit to hunt it by Signal str
      int row = (ty - listY) / rowH;
      if (row >= 0 && row < pwnCount && ty >= listY) {
        pBLEScan->stop();
        pBLEScan->setAdvertisedDeviceCallbacks(nullptr);
        bleHuntDevice(pwnHits[row].addr, pwnHits[row].name);
        drawChrome();
        drawList();
        pBLEScan->start(3, false);
        scanStart = millis();
      }
    }
    delay(20);
  }
}

static void bleTrackerLocator() {
  for (int i = 0; i < trackerCount; i++) {
    trackers[i].addr = "";
  }
  trackerCount = 0;
  const int rowH=28, listY=HEADER_H+4;
  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  TrackerScanCallback cb;
  pBLEScan->setAdvertisedDeviceCallbacks(&cb);
  pBLEScan->setActiveScan(true); pBLEScan->setInterval(100); pBLEScan->setWindow(99);
  auto drawChrome = [&]() { tft.fillScreen(C_BG); drawHeader("Trackers", true); };
  auto drawList = [&]() {
    tft.fillRect(0,listY,SCREEN_W,SCREEN_H-listY,C_BG);
    if (trackerCount==0) {
      tft.setTextColor(C_SUBTEXT); tft.setTextSize(1);
      tft.drawString("Scanning for trackers...",10,listY+10);
      tft.drawString("AirTag, Google FMDN,",10,listY+30);
      tft.drawString("Samsung Tag, Tile",10,listY+44);
      return;
    }
    int count=min(trackerCount,(SCREEN_H-listY-4)/rowH);
    for (int i=0; i<count; i++) {
      int y=listY+i*rowH; Tracker& t=trackers[i];
      tft.setTextColor(trackerTypeColor(t.type)); tft.setTextSize(1);
      tft.drawString(trackerTypeName(t.type),6,y+2);
      tft.setTextColor(C_TEXT); tft.drawString(t.addr.substring(9).c_str(),6,y+14);
      uint16_t rcolor=t.rssi>-60?C_ACCENT:t.rssi>-80?C_YELLOW:C_RED;
      char rs[8]; snprintf(rs,sizeof(rs),"%d",t.rssi);
      tft.setTextColor(rcolor); tft.setTextSize(2);
      tft.drawString(rs,SCREEN_W-strlen(rs)*12-8,y+6);
      tft.drawFastHLine(0,y+rowH-1,SCREEN_W,C_PANEL);
    }
  };

  drawChrome(); drawList();
  pBLEScan->start(3,false);
  uint32_t scanStart=millis(), lastListDraw=millis();
  int lastCount=trackerCount;
  while (true) {
    if (millis()-scanStart>3000) { pBLEScan->clearResults(); pBLEScan->start(3,false); scanStart=millis(); }
    if (trackerCount!=lastCount||millis()-lastListDraw>500) { drawList(); lastListDraw=millis(); lastCount=trackerCount; }
    int16_t tx,ty;
    if (tftTouched(tx,ty)) {
      waitTouchRelease();
      if (backButtonHit(tx,ty)) { pBLEScan->stop(); pBLEScan->clearResults(); return; }
      int row=(ty-listY)/rowH;
      if (row>=0&&row<trackerCount&&ty>=listY) {
        pBLEScan->stop(); hunterView(row);
        drawChrome(); drawList(); pBLEScan->start(3,false); scanStart=millis();
      }
    }
    delay(20);
  }
}