//  Discovers Roku devices on the local network 
//  then presents a touchscreen remote 
//  port 8060. Must be connected to WiFi before entering.
//  Roku remote control uses Roku's official External Control Protocol (ECP):
//  https://developer.roku.com/docs/developer-program/debugging/external-control-api.md

#include "ui24.h"
#include "config24.h"
#include "touch24.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUDP.h>
#include "roku_remote24.h"

extern TFT_eSPI tft;

#define ROKU_PORT       8060
#define SSDP_ADDR       "239.255.255.250"
#define SSDP_PORT       1900
#define MAX_ROKUS       5
#define SSDP_TIMEOUT_MS 4000

struct RokuDevice {
  String ip;
  String name;
};

static RokuDevice rokus[MAX_ROKUS];
static int rokuCount = 0;

// ── HTTP POST to Roku ECP ─────────────────────────────────────
static bool rokuPost(const String& ip, const String& path) {
  WiFiClient client;
  Serial.printf("[ROKU] Connecting to %s:%d\n", ip.c_str(), ROKU_PORT);
  if (!client.connect(ip.c_str(), ROKU_PORT)) {
    Serial.println("[ROKU] connect() failed");
    return false;
  }
  String req = String("POST ") + path + " HTTP/1.0\r\nHost: " + ip + ":" + ROKU_PORT + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  Serial.printf("[ROKU] Sending: %s\n", req.c_str());
  client.print(req);
  unsigned long t = millis();
  while (!client.available() && millis() - t < 2000) delay(10);
  String resp = "";
  while (client.available()) resp += (char)client.read();
  Serial.printf("[ROKU] Response: %s\n", resp.c_str());
  client.stop();
  return true;
}

// ── Fetch friendly name from /query/device-info ───────────────
static String rokuGetName(const String& ip) {
  WiFiClient client;
  if (!client.connect(ip.c_str(), ROKU_PORT)) return ip;
  client.printf("GET /query/device-info HTTP/1.0\r\nHost: %s:%d\r\n\r\n",
                ip.c_str(), ROKU_PORT);
  unsigned long t = millis();
  String body = "";
  while (millis() - t < 3000) {
    while (client.available()) body += (char)client.read();
    if (body.length() > 2000) break;
  }
  client.stop();
  // parse <friendly-device-name>...</friendly-device-name>
  int start = body.indexOf("<friendly-device-name>");
  int end   = body.indexOf("</friendly-device-name>");
  if (start >= 0 && end > start) {
    return body.substring(start + 22, end);
  }
  return ip;
}

// ── SSDP discovery ────────────────────────────────────────────
static void rokuDiscover() {
  rokuCount = 0;

  WiFiUDP udp;
  udp.begin(SSDP_PORT);

  const char* search =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 3\r\n"
    "ST: roku:ecp\r\n"
    "\r\n";

  udp.beginPacket(SSDP_ADDR, SSDP_PORT);
  udp.write((const uint8_t*)search, strlen(search));
  udp.endPacket();

  uint32_t deadline = millis() + SSDP_TIMEOUT_MS;
  while (millis() < deadline && rokuCount < MAX_ROKUS) {
    int len = udp.parsePacket();
    if (len > 0) {
      String resp = "";
      while (udp.available()) resp += (char)udp.read();
      // parse LOCATION: http://ip:8060/
      int loc = resp.indexOf("LOCATION:");
      if (loc < 0) loc = resp.indexOf("Location:");
      if (loc >= 0) {
        int lineEnd = resp.indexOf("\r\n", loc);
        String locLine = resp.substring(loc + 9, lineEnd);
        locLine.trim();
        // extract IP between http:// and :8060
        int ipStart = locLine.indexOf("//") + 2;
        int ipEnd   = locLine.indexOf(":", ipStart);
        if (ipStart >= 2 && ipEnd > ipStart) {
          String ip = locLine.substring(ipStart, ipEnd);
          // deduplicate
          bool found = false;
          for (int i = 0; i < rokuCount; i++) {
            if (rokus[i].ip == ip) { found = true; break; }
          }
          if (!found) {
            rokus[rokuCount].ip   = ip;
            rokus[rokuCount].name = ip; // placeholder until we fetch name
            rokuCount++;
          }
        }
      }
    }
    delay(50);
  }
  udp.stop();

  // fetch friendly names
  for (int i = 0; i < rokuCount; i++) {
    rokus[i].name = rokuGetName(rokus[i].ip);
    if (rokus[i].name.length() > 20) rokus[i].name = rokus[i].name.substring(0, 20);
  }
}

// ── Remote UI ─────────────────────────────────────────────────
struct RokuBtn {
  int16_t x, y, w, h;
  const char* label;
  const char* cmd;        // ECP keypress path e.g. /keypress/Home
};

static void rokuRemoteUI(int deviceIdx) {
  const String& ip = rokus[deviceIdx].ip;

  // Layout constants
  const int nameBarH = 14;
  const int padX     = 8;
  const int padY     = HEADER_H + nameBarH + 8;
  const int gap      = 6;
  const int btnW     = (SCREEN_W - padX * 2 - gap * 2) / 3;  // 70px at 240wide
  const int btnH     = 44;

  const int col0 = padX;
  const int col1 = padX + btnW + gap;
  const int col2 = padX + (btnW + gap) * 2;

  const int row0 = padY;
  const int row1 = padY + (btnH + gap);
  const int row2 = padY + (btnH + gap) * 2;
  const int row3 = padY + (btnH + gap) * 3;

  RokuBtn btns[] = {
    { (int16_t)col0, (int16_t)row0, btnW, btnH, "PWR",  "/keypress/PowerOff"  },
    { (int16_t)col1, (int16_t)row0, btnW, btnH, "HOME", "/keypress/Home"      },
    { (int16_t)col2, (int16_t)row0, btnW, btnH, "BACK", "/keypress/Back"      },
    { (int16_t)col0, (int16_t)row1, btnW, btnH, "VOL+", "/keypress/VolumeUp"  },
    { (int16_t)col1, (int16_t)row1, btnW, btnH, "UP",   "/keypress/Up"        },
    { (int16_t)col2, (int16_t)row1, btnW, btnH, "MUTE", "/keypress/VolumeMute"},
    { (int16_t)col0, (int16_t)row2, btnW, btnH, "VOL-", "/keypress/VolumeDown"},
    { (int16_t)col1, (int16_t)row2, btnW, btnH, "OK",   "/keypress/Select"    },
    { (int16_t)col2, (int16_t)row2, btnW, btnH, "LEFT", "/keypress/Left"      },
    { (int16_t)col0, (int16_t)row3, btnW, btnH, "    ", ""                    },
    { (int16_t)col1, (int16_t)row3, btnW, btnH, "DOWN", "/keypress/Down"      },
    { (int16_t)col2, (int16_t)row3, btnW, btnH, "RGHT", "/keypress/Right"     },
  };
  const int btnCount = sizeof(btns) / sizeof(btns[0]);

  auto drawChrome = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("Roku Remote", true);
    // device name bar
    tft.fillRect(0, HEADER_H, SCREEN_W, nameBarH, C_PANEL);
    tft.setFreeFont(NULL); tft.setTextSize(1);
    tft.setTextColor(C_SUBTEXT);
    tft.setTextDatum(ML_DATUM);
    String label = rokus[deviceIdx].name;
    tft.drawString(label.c_str(), padX, HEADER_H + nameBarH / 2);
    tft.setTextDatum(TL_DATUM);
  };

  auto drawBtns = [&]() {
    for (int i = 0; i < btnCount; i++) {
      RokuBtn& b = btns[i];
      if (strlen(b.cmd) == 0) continue; // skip blank placeholder
      uint16_t border = C_BORDER;
      uint16_t txtCol = C_TEXT;
      if (i == 0) { border = C_RED;    txtCol = C_RED;    }
      if (i == 1) { border = C_ACCENT; txtCol = C_ACCENT; }
      if (i == 2) { border = C_ACCENT; txtCol = C_ACCENT; }
      // outer button
      tft.fillRoundRect(b.x, b.y, b.w, b.h, 4, C_BTN);
      tft.drawRoundRect(b.x, b.y, b.w, b.h, 4, border);
      // inner inset rect for depth
      tft.drawRoundRect(b.x + 3, b.y + 3, b.w - 6, b.h - 6, 3,
                        tft.color565(30, 50, 60));
      tft.setTextColor(txtCol);
      tft.setTextSize(1);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
      tft.setTextDatum(TL_DATUM);
    }
  };

  String statusMsg = "";
  auto drawStatus = [&]() {
    int statusY = SCREEN_H - 20;
    tft.fillRect(0, statusY, SCREEN_W, 20, C_BG);
    if (statusMsg.length() > 0) {
      tft.setFreeFont(NULL); tft.setTextSize(1);
      tft.setTextColor(C_ACCENT);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(statusMsg.c_str(), SCREEN_W / 2, statusY + 6);
      tft.setTextDatum(TL_DATUM);
    }
  };

  drawChrome();
  drawBtns();

  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return;

      for (int i = 0; i < btnCount; i++) {
        RokuBtn& b = btns[i];
        if (strlen(b.cmd) == 0) continue; // blank cell
        if (tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h) {
          // flash button
          tft.fillRoundRect(b.x, b.y, b.w, b.h, 4, C_ACCENT);
          tft.setTextColor(C_BG); tft.setTextSize(1);
          tft.setTextDatum(MC_DATUM);
          tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
          tft.setTextDatum(TL_DATUM);
          delay(80);

          bool ok = rokuPost(ip, String(b.cmd));
          statusMsg = ok ? String("Sent: ") + b.label : "Send failed";

          // redraw button normal
          uint16_t border = C_BORDER, txtCol = C_TEXT;
          if (i == 0) { border = C_RED;    txtCol = C_RED;    }
          if (i == 1) { border = C_ACCENT; txtCol = C_ACCENT; }
          if (i == 2) { border = C_ACCENT; txtCol = C_ACCENT; }
          tft.fillRoundRect(b.x, b.y, b.w, b.h, 4, C_BTN);
          tft.drawRoundRect(b.x, b.y, b.w, b.h, 4, border);
          tft.drawRoundRect(b.x + 3, b.y + 3, b.w - 6, b.h - 6, 3,
                            tft.color565(30, 50, 60));
          tft.setTextColor(txtCol); tft.setTextSize(1);
          tft.setTextDatum(MC_DATUM);
          tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
          tft.setTextDatum(TL_DATUM);
          drawStatus();
          break;
        }
      }
    }
    delay(20);
  }
}

// ── Device picker ─────────────────────────────────────────────
static void rokuPicker() {
  tft.fillScreen(C_BG);
  drawHeader("Select Roku", true);

  const int rowH  = 44;
  const int startY = HEADER_H + 8;

  for (int i = 0; i < rokuCount; i++) {
    int y = startY + i * rowH;
    tft.fillRoundRect(8, y, SCREEN_W - 16, rowH - 4, 4, C_BTN);
    tft.drawRoundRect(8, y, SCREEN_W - 16, rowH - 4, 4, C_ACCENT);
    tft.setFreeFont(NULL); tft.setTextSize(1);
    tft.setTextColor(C_ACCENT);
    tft.drawString(rokus[i].name.c_str(), 16, y + 8);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString(rokus[i].ip.c_str(), 16, y + 22);
  }

  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      if (backButtonHit(tx, ty)) return;
      int row = (ty - startY) / rowH;
      if (row >= 0 && row < rokuCount) {
        rokuRemoteUI(row);
        // redraw picker on return
        tft.fillScreen(C_BG);
        drawHeader("Select Roku", true);
        for (int i = 0; i < rokuCount; i++) {
          int y = startY + i * rowH;
          tft.fillRoundRect(8, y, SCREEN_W - 16, rowH - 4, 4, C_BTN);
          tft.drawRoundRect(8, y, SCREEN_W - 16, rowH - 4, 4, C_ACCENT);
          tft.setFreeFont(NULL); tft.setTextSize(1);
          tft.setTextColor(C_ACCENT);
          tft.drawString(rokus[i].name.c_str(), 16, y + 8);
          tft.setTextColor(C_SUBTEXT);
          tft.drawString(rokus[i].ip.c_str(), 16, y + 22);
        }
      }
    }
    delay(20);
  }
}

// ── Public entry point ────────────────────────────────────────
void rokuRemoteMenu() {
  if (!WiFi.isConnected()) {
    tft.fillScreen(C_BG);
    drawHeader("Roku Remote", true);
    tft.setFreeFont(NULL); tft.setTextSize(1);
    tft.setTextColor(C_RED);
    tft.drawString("No WiFi connection.", 10, HEADER_H + 20);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("Connect to a network first.", 10, HEADER_H + 36);
    while (true) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) { waitTouchRelease(); if (backButtonHit(tx, ty)) return; }
      delay(20);
    }
  }

  tft.fillScreen(C_BG);
  drawHeader("Roku Remote", true);
  tft.setFreeFont(NULL); tft.setTextSize(1);
  tft.setTextColor(C_ACCENT);
  tft.drawString("Searching for Roku devices...", 10, HEADER_H + 20);
  tft.setTextColor(C_SUBTEXT);
  tft.drawString("(up to 4 seconds)", 10, HEADER_H + 36);

  rokuDiscover();

  if (rokuCount == 0) {
    tft.fillScreen(C_BG);
    drawHeader("Roku Remote", true);
    tft.setFreeFont(NULL); tft.setTextSize(1);
    tft.setTextColor(C_RED);
    tft.drawString("No Roku devices found.", 10, HEADER_H + 20);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("Check network connection.", 10, HEADER_H + 36);
    tft.drawString("Roku must be on same WiFi.", 10, HEADER_H + 52);
    while (true) {
      int16_t tx, ty;
      if (tftTouched(tx, ty)) { waitTouchRelease(); if (backButtonHit(tx, ty)) return; }
      delay(20);
    }
  }

  if (rokuCount == 1) {
    rokuRemoteUI(0);
  } else {
    rokuPicker();
  }
}