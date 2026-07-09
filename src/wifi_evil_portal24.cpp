#include <Arduino.h>
#include <WiFi.h>
#include "ui24.h"
#include "config24.h"
#include "touch24.h"
#include <WiFiClient.h>
#include "esp_wifi.h"
#define EP_MAX_NETWORKS 20
#define EP_RESCAN_MS    15000

struct EPNetwork {
    String  ssid;
    int     rssi;
    uint8_t bssid[6];
    bool    flagged;   // evil twin suspect as fuckkkks
    bool    openStrong; // open + RSSI > -50
    bool    captivePortal; // open network that intercepts HTTP — likely evil portal
    bool    mesh;          // 3+ nodes same SSID + OUI — likely mesh network go piglet still how to deal with twins Mesh?
};

static EPNetwork epNets[EP_MAX_NETWORKS];
static int       epCount    = 0;
static int       epFlagged  = 0;
static int       epScroll   = 0;        
static bool      epScanning = false;

// layout 
#define EP_LIST_Y    (HEADER_H + 38)    
#define EP_ROW_H     28
#define EP_VISIBLE   7                  
#define EP_LIST_BOT  (EP_LIST_Y + EP_VISIBLE * EP_ROW_H)

// forward
static void epDrawHeader();
static void epDrawStatus();
static void epDrawList();
static void epDrawRow(int idx, int y);
static void epRunScan();

// header
static void epDrawHeader() {
    drawHeader("EVIL PORTAL SCAN", true);
}

static void epDrawStatus() {
    // status bar between header and list
    tft.fillRect(0, HEADER_H, SCREEN_W, 36, C_PANEL);
    tft.drawFastHLine(0, HEADER_H + 36, SCREEN_W, C_BORDER);
    tft.setTextDatum(ML_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    Serial.printf("[MEM] Internal heap: %d\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Serial.printf("[MEM] DMA heap: %d\n", heap_caps_get_free_size(MALLOC_CAP_DMA));
    Serial.printf("[MEM] 8-bit heap: %d\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    if (epScanning) {
        tft.setTextColor(C_YELLOW, C_PANEL);
        tft.drawString("SCANNING...", 8, HEADER_H + 18);
    } else if (epFlagged > 0) {
        tft.setTextColor(C_ACCENT2, C_PANEL);
        char buf[40];
        snprintf(buf, sizeof(buf), "!! %d SUSPECT(S) FOUND — STAY ALERT", epFlagged);
        tft.drawString(buf, 8, HEADER_H + 18);
    } else if (epCount > 0) {
        tft.setTextColor(C_GREEN, C_PANEL);
        char buf[32];
        snprintf(buf, sizeof(buf), "CLEAR — %d NETWORKS SCANNED", epCount);
        tft.drawString(buf, 8, HEADER_H + 18);
    } else {
        tft.setTextColor(C_SUBTEXT, C_PANEL);
        tft.drawString("TAP SCAN TO BEGIN", 8, HEADER_H + 18);
    }

    // rescan button — right side make bigger
    tft.fillRoundRect(SCREEN_W - 66, HEADER_H + 6, 60, 24, 3, C_BTN);
    tft.drawRoundRect(SCREEN_W - 66, HEADER_H + 6, 60, 24, 3, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ACCENT, C_BTN);
    tft.drawString("RESCAN", SCREEN_W - 36, HEADER_H + 19);
}

static void epDrawRow(int idx, int y) {
    if (idx < 0 || idx >= epCount) return;
    EPNetwork& n = epNets[idx];
    uint16_t rowBg   = (idx % 2 == 0) ? C_BG : C_PANEL;
    uint16_t rowText = C_TEXT;
    uint16_t tagCol  = C_SUBTEXT;
    if (n.flagged) {
        rowBg   = 0x3000;       // dark red tint
        rowText = C_ACCENT2;
        tagCol  = C_ACCENT2;
    } else if (n.mesh) {
        rowBg   = 0x0818;       // dark cyan tint
        rowText = C_CYAN;
        tagCol  = C_CYAN;
    } else if (n.openStrong) {
        rowBg   = 0x2820;       // dark orange tint
        rowText = C_YELLOW;
        tagCol  = C_YELLOW;
    }

    tft.fillRect(0, y, SCREEN_W, EP_ROW_H, rowBg);
    tft.drawFastHLine(0, y + EP_ROW_H - 1, SCREEN_W, C_PANEL);
    tft.setTextDatum(ML_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    // SSID 
    String ssid = n.ssid.length() == 0 ? "[hidden]" : n.ssid;
    if (ssid.length() > 18) ssid = ssid.substring(0, 17) + "~";

    tft.setTextColor(rowText, rowBg);
    tft.drawString(ssid, 6, y + 10);

    // RSSI bar right side
    int bars = 0;
    if (n.rssi > -55) bars = 4;
    else if (n.rssi > -67) bars = 3;
    else if (n.rssi > -78) bars = 2;
    else bars = 1;

    int bx = SCREEN_W - 28;
    for (int b = 0; b < 4; b++) {
        int bh = 4 + b * 3;
        uint16_t bc = (b < bars) ? C_GREEN : C_PANEL;
        tft.fillRect(bx + b * 6, y + EP_ROW_H - 4 - bh, 4, bh, bc);
    }

    // tag: TWIN / OPEN or nothing
    if (n.flagged) {
        tft.setTextColor(C_ACCENT2, rowBg);
        tft.drawString("TWIN", SCREEN_W - 70, y + 10);
    } else if (n.mesh) {
        tft.setTextColor(C_CYAN, rowBg);
        tft.drawString("MESH?", SCREEN_W - 70, y + 10);
    } else if (n.captivePortal) {
        tft.setTextColor(C_YELLOW, rowBg);
        tft.drawString("!PORTAL", SCREEN_W - 70, y + 10);
    } else if (n.openStrong) {
        tft.setTextColor(C_YELLOW, rowBg);
        tft.drawString("OPEN!", SCREEN_W - 70, y + 10);
    }

    char mac[20];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X",
             n.bssid[0], n.bssid[1], n.bssid[2]);
    tft.setTextColor(C_SUBTEXT, rowBg);
    tft.drawString(mac, 6, y + 19);
}

static void epDrawList() {
    tft.fillRect(0, EP_LIST_Y, SCREEN_W, SCREEN_H - EP_LIST_Y - 34, C_BG);
    if (epCount == 0) {
        tft.setTextDatum(MC_DATUM);
        tft.setFreeFont(NULL);
        tft.setTextSize(1);
        tft.setTextColor(C_SUBTEXT, C_BG);
        tft.drawString("no networks — tap RESCAN", SCREEN_W / 2, EP_LIST_Y + 50);
        return;
    }

    for (int i = 0; i < EP_VISIBLE; i++) {
        int idx = epScroll + i;
        int y   = EP_LIST_Y + i * EP_ROW_H;
        if (idx < epCount) {
            epDrawRow(idx, y);
        } else {
            tft.fillRect(0, y, SCREEN_W, EP_ROW_H, C_BG);
        }
    }

    // scroll bar
    if (epCount > EP_VISIBLE) {
        int trackH = EP_VISIBLE * EP_ROW_H;
        int thumbH = max(12, trackH * EP_VISIBLE / epCount);
        int thumbY = EP_LIST_Y + (trackH - thumbH) * epScroll / max(1, epCount - EP_VISIBLE);
        tft.fillRect(SCREEN_W - 4, EP_LIST_Y, 4, trackH, C_PANEL);
        tft.fillRect(SCREEN_W - 4, thumbY, 4, thumbH, C_ACCENT);
    }
}

static void epRunScan() {
    epScanning = true;
    epDrawStatus();
    epCount   = 0;
    epFlagged = 0;
    epScroll  = 0;
    esp_wifi_set_promiscuous(false);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    delay(200);
    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) {
        epScanning = false;
        epDrawStatus();
        epDrawList();
        return;
    }

    // first pass — collect them all like Pokemon
    for (int i = 0; i < n && epCount < EP_MAX_NETWORKS; i++) {
        epNets[epCount].ssid      = WiFi.SSID(i);
        epNets[epCount].rssi      = WiFi.RSSI(i);
        epNets[epCount].flagged   = false;
        epNets[epCount].openStrong = false;
        epNets[epCount].captivePortal = false;
        epNets[epCount].mesh = false;
        memcpy(epNets[epCount].bssid, WiFi.BSSID(i), 6);

        // open + very strong signal
        if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN && WiFi.RSSI(i) > -50) {
            epNets[epCount].openStrong = true;
        }
        epCount++;
    }
    // 1.5 pass — HTTP probe — flag open networks that intercept requests 
    for (int i = 0; i < epCount; i++) {
        if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
            WiFi.begin(epNets[i].ssid.c_str());
            unsigned long t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 4000) delay(100);
            if (WiFi.status() == WL_CONNECTED) {
                WiFiClient client;
                if (client.connect("connectivitycheck.gstatic.com", 80)) {
                    client.print("GET /generate_204 HTTP/1.0\r\nHost: connectivitycheck.gstatic.com\r\n\r\n");
                    unsigned long rt = millis();
                    while (!client.available() && millis() - rt < 3000) delay(50);
                    String resp = client.readStringUntil('\n');
                    if (!resp.startsWith("HTTP/1") || resp.indexOf("204") == -1) {
                        epNets[i].captivePortal = true;
                        epNets[i].openStrong = false; // captive takes priority
                    }
                    client.stop();
                }
                WiFi.disconnect(true);
                delay(300);
            }
        }
    }

    // second pass — flag evil twins dup SSID, different sSID how to deal with twins tho? come back
    for (int i = 0; i < epCount; i++) {
        for (int j = i + 1; j < epCount; j++) {
            if (epNets[i].ssid == epNets[j].ssid &&
                epNets[i].ssid.length() > 0 &&
                memcmp(epNets[i].bssid, epNets[j].bssid, 6) != 0)
            {
                epNets[i].flagged = true;
                epNets[j].flagged = true;
            }
        }
    }

    // third pass — downgrade flagged twins to MESH? if 3+ nodes share same SSID + OUI 
    for (int i = 0; i < epCount; i++) {
        if (!epNets[i].flagged) continue;
        int matchCount = 0;
        for (int j = 0; j < epCount; j++) {
            if (epNets[i].ssid == epNets[j].ssid &&
                epNets[i].bssid[0] == epNets[j].bssid[0] &&
                epNets[i].bssid[1] == epNets[j].bssid[1] &&
                epNets[i].bssid[2] == epNets[j].bssid[2])
            {
                matchCount++;
            }
        }
        if (matchCount >= 3) {
            for (int j = 0; j < epCount; j++) {
                if (epNets[i].ssid == epNets[j].ssid &&
                    epNets[i].bssid[0] == epNets[j].bssid[0] &&
                    epNets[i].bssid[1] == epNets[j].bssid[1] &&
                    epNets[i].bssid[2] == epNets[j].bssid[2])
                {
                    if (epNets[j].flagged) {
                        epNets[j].flagged = false;
                        epNets[j].mesh    = true;
                    }
                }
            }
        }
    }

    // count total flagged
    for (int i = 0; i < epCount; i++) {
        if (epNets[i].flagged) epFlagged++;
    }

    // sort: flagged first, then openStrong, then by RSSI
    // simple bubble sort — list is small
    for (int i = 0; i < epCount - 1; i++) {
        for (int j = 0; j < epCount - 1 - i; j++) {
            int scoreA = (epNets[j].flagged ? 2 : 0) + (epNets[j].captivePortal ? 2 : 0) + (epNets[j].openStrong ? 1 : 0) + (epNets[j].mesh ? 1 : 0);
            int scoreB = (epNets[j+1].flagged ? 2 : 0) + (epNets[j+1].captivePortal ? 2 : 0) + (epNets[j+1].openStrong ? 1 : 0) + (epNets[j+1].mesh ? 1 : 0);
            if (scoreB > scoreA) {
                EPNetwork tmp  = epNets[j];
                epNets[j]      = epNets[j + 1];
                epNets[j + 1]  = tmp;
            }
        }
    }

    WiFi.scanDelete();
    epScanning = false;
}

// public entry point 

void wifiEvilPortalMenu() {
    epCount   = 0;
    epFlagged = 0;
    epScroll  = 0;

    tft.fillScreen(C_BG);
    epDrawHeader();
    epDrawStatus();
    epDrawList();

    // auto-run first scan
    epRunScan();
    epDrawStatus();
    epDrawList();

    int16_t tx, ty;
    int16_t lastTy = -1;

    while (true) {
        if (tftTouched(tx, ty)) {

            // back button
            if (ty < HEADER_H) {
                waitTouchRelease();
                for (int i = 0; i < epCount; i++) {
                    epNets[i].ssid = "";
                }
                epCount = 0;
                WiFi.scanDelete();
                return;
            }

            // rescan button
            if (ty >= HEADER_H + 6 && ty <= HEADER_H + 30 &&
                tx >= SCREEN_W - 66)
            {
                waitTouchRelease();
                epRunScan();
                epDrawStatus();
                epDrawList();
                continue;
            }

            // scroll: track drag start
            lastTy = ty;

        } else if (lastTy >= 0) {
            // touch released — check if it was a drag
            lastTy = -1;
        }

        // drag scroll while held
        if (lastTy >= 0 && tftTouched(tx, ty)) {
            if (ty >= EP_LIST_Y && ty < EP_LIST_BOT) {
                int delta = (lastTy - ty) / EP_ROW_H;
                if (delta != 0) {
                    epScroll = constrain(epScroll + delta,
                                        0, max(0, epCount - EP_VISIBLE));
                    epDrawList();
                    lastTy = ty;
                }
            }
        }

        delay(20);
    }
}