#include "wifi_deauth_scan24.h"
#include "ui24.h"
#include "touch24.h"
#include "config24.h"
#include <WiFi.h>
#include "esp_wifi.h"
extern TFT_eSPI tft;

//Frame 
struct ieee80211_hdr {
  uint16_t frame_ctrl;
  uint16_t duration;
  uint8_t  addr1[6];   // destination
  uint8_t  addr2[6];   // source
  uint8_t  addr3[6];   // SSID
  uint16_t seq_ctrl;
};

//captured events
#define DEAUTH_MAX_ENTRIES 32
struct DeauthEntry {
  uint8_t  src[6];
  uint8_t  dst[6];
  uint8_t  channel;
  uint8_t  type;        // 0=deauth  1=disassoc
  uint8_t  reason;
  uint32_t lastSeen;    
  uint16_t count;
};

static DeauthEntry  s_entries[DEAUTH_MAX_ENTRIES];
static int          s_entryCount  = 0;
static portMUX_TYPE s_mux         = portMUX_INITIALIZER_UNLOCKED;
static uint8_t      s_listenCh    = 1;
#define DEAUTH_MAX_GROUPS 16

struct GroupedEntry {
  uint8_t  oui[3];          // stable last3- MAC
  uint8_t  bestSrc[6];      // mostseen full src MAC
  uint8_t  worstDst[6];     // target with highest combined count
  uint32_t totalCount;
  uint16_t macVariants;     // how many different top-3 seen
  uint16_t worstCount;      // count on the worst target
  bool     expanded;        // tap to expand in grouped view
};

//Promiscuous mode
static void IRAM_ATTR deauthSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt =
      reinterpret_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = pkt->payload;
  uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < sizeof(ieee80211_hdr) + 2) return;   // need FC + addr fields + reason
  const ieee80211_hdr* hdr = reinterpret_cast<const ieee80211_hdr*>(payload);
  uint8_t ftype  = (hdr->frame_ctrl >> 2)  & 0x03;  // should be 0 (management)
  uint8_t fsub   = (hdr->frame_ctrl >> 4)  & 0x0F;

  // 0x0C = deauth,  0x0A = disassoc
  if (ftype != 0) return;
  if (fsub != 0x0C && fsub != 0x0A) return;
  uint8_t  frameType = (fsub == 0x0C) ? 0 : 1;
  uint8_t  reason    = (len >= sizeof(ieee80211_hdr) + 2)
                         ? payload[sizeof(ieee80211_hdr)]
                         : 0;

  portENTER_CRITICAL_ISR(&s_mux);

  // Check if we already have this 
  for (int i = 0; i < s_entryCount; i++) {
    if (memcmp(s_entries[i].src, hdr->addr2, 6) == 0 &&
        memcmp(s_entries[i].dst, hdr->addr1, 6) == 0 &&
        s_entries[i].channel == s_listenCh) {
      s_entries[i].count++;
      s_entries[i].lastSeen = millis();
      s_entries[i].reason   = reason;
      portEXIT_CRITICAL_ISR(&s_mux);
      return;
    }
  }

  // New entry 
  int slot = s_entryCount;
  if (slot >= DEAUTH_MAX_ENTRIES) {
    // Find oldest by lastSeen
    uint32_t oldest = s_entries[0].lastSeen;
    slot = 0;
    for (int i = 1; i < DEAUTH_MAX_ENTRIES; i++) {
      if (s_entries[i].lastSeen < oldest) { oldest = s_entries[i].lastSeen; slot = i; }
    }
  } else {
    s_entryCount++;
  }

  memcpy(s_entries[slot].src, hdr->addr2, 6);
  memcpy(s_entries[slot].dst, hdr->addr1, 6);
  s_entries[slot].channel  = s_listenCh;
  s_entries[slot].type     = frameType;
  s_entries[slot].reason   = reason;
  s_entries[slot].lastSeen = millis();
  s_entries[slot].count    = 1;
  portEXIT_CRITICAL_ISR(&s_mux);
}

//Helpers
static void macToStr(const uint8_t* mac, char* out) {
  // Short form: last 3 octets  XX:XX:XX  to save screen space
  snprintf(out, 9, "%02X:%02X:%02X", mac[3], mac[4], mac[5]);
}
static void ouiToStr(const uint8_t* oui, char* out) {
  // OUI is stored as last-3 octets (stable hardware fingerprint)
  snprintf(out, 9, "%02X:%02X:%02X", oui[0], oui[1], oui[2]);
}
static bool isBroadcast(const uint8_t* mac) {
  return mac[0] == 0xFF && mac[1] == 0xFF && mac[2] == 0xFF;
}

//Main menu
void wifiDeauthScanner() {
  // Layout
  const int ROW_H    = 38;
  const int COL_CH   = 4;
  const int COL_SRC  = 26;
  const int COL_DST  = 110;
  const int COL_CNT  = 195;
  const int COL_TYPE = 225;
  const int HDR_ROW  = HEADER_H + 2;
  const int LIST_Y   = HDR_ROW + 14;
  const int VISIBLE  = (SCREEN_H - LIST_Y) / ROW_H;

  // View mode
  bool groupedView = true;
  GroupedEntry groups[DEAUTH_MAX_GROUPS];
  int groupCount = 0;
  // Reset state
  portENTER_CRITICAL(&s_mux);
  s_entryCount = 0;
  memset(s_entries, 0, sizeof(s_entries));
  portEXIT_CRITICAL(&s_mux);
  // Start promiscuous on ch1
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  delay(100);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&deauthSniffer);
  esp_wifi_set_promiscuous(true);
  s_listenCh = 1;
  esp_wifi_set_channel(s_listenCh, WIFI_SECOND_CHAN_NONE);
  int dwellMs = 500;

  // Build grouped view 
  auto buildGroups = [&]() {
    // Save expanded 
    uint8_t savedOui[DEAUTH_MAX_GROUPS][3];
    bool    savedExpanded[DEAUTH_MAX_GROUPS];
    int     savedCount = groupCount;
    for (int i = 0; i < savedCount; i++) {
      memcpy(savedOui[i], groups[i].oui, 3);
      savedExpanded[i] = groups[i].expanded;
    }

    groupCount = 0;
    memset(groups, 0, sizeof(groups));

    DeauthEntry snap[DEAUTH_MAX_ENTRIES];
    int snapCount;
    portENTER_CRITICAL(&s_mux);
    snapCount = s_entryCount;
    memcpy(snap, s_entries, snapCount * sizeof(DeauthEntry));
    portEXIT_CRITICAL(&s_mux);

    for (int i = 0; i < snapCount; i++) {
      const uint8_t* oui = &snap[i].src[3];  
      // Find existing group
      int g = -1;
      for (int j = 0; j < groupCount; j++) {
        if (memcmp(groups[j].oui, oui, 3) == 0) { g = j; break; }
      }

      // New group
      if (g < 0) {
        if (groupCount >= DEAUTH_MAX_GROUPS) continue;
        g = groupCount++;
        memcpy(groups[g].oui, oui, 3);
        memcpy(groups[g].bestSrc, snap[i].src, 6);
        memcpy(groups[g].worstDst, snap[i].dst, 6);
        groups[g].totalCount  = snap[i].count;
        groups[g].worstCount  = snap[i].count;
        groups[g].macVariants = 1;
        groups[g].expanded    = false;
        continue;
      }

      groups[g].totalCount += snap[i].count;
      // Track most-seen
      if (snap[i].count > groups[g].worstCount) {
        memcpy(groups[g].bestSrc, snap[i].src, 6);
      }
      // Track worst (highest count)
      if (snap[i].count > groups[g].worstCount) {
        memcpy(groups[g].worstDst, snap[i].dst, 6);
        groups[g].worstCount = snap[i].count;
      }
      // Count unique MAC variants
      bool seen = false;
      for (int k = 0; k < i; k++) {
        if (memcmp(&snap[k].src[3], oui, 3) == 0 &&
            memcmp(snap[k].src, snap[i].src, 3) == 0) { seen = true; break; }
      }
      if (!seen) groups[g].macVariants++;
    }

    // Sort groups by total
    for (int i = 1; i < groupCount; i++) {
      GroupedEntry key = groups[i];
      int j = i - 1;
      while (j >= 0 && groups[j].totalCount < key.totalCount) {
        groups[j+1] = groups[j]; j--;
      }
      groups[j+1] = key;
    }

    // Restore by matching OUI
    for (int i = 0; i < groupCount; i++) {
      for (int s = 0; s < savedCount; s++) {
        if (memcmp(groups[i].oui, savedOui[s], 3) == 0) {
          groups[i].expanded = savedExpanded[s];
          break;
        }
      }
    }
  };

  // Draw static chrome
  auto drawChrome = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("Deauth Scanner", true);
    // Column headers
    tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString("CH",   COL_CH,   HDR_ROW);
    tft.drawString("SOURCE",COL_SRC,  HDR_ROW);
    tft.drawString("TARGET",COL_DST,  HDR_ROW);
    tft.drawString("CNT",  COL_CNT,  HDR_ROW);
    tft.drawString("T",    COL_TYPE, HDR_ROW);  // D=deauth X=disassoc
    tft.drawFastHLine(0, LIST_Y - 1, SCREEN_W, C_PANEL);
    // bar at bottom
    tft.fillRect(0, SCREEN_H - 18, SCREEN_W, 18, C_PANEL);
  };

  auto drawStatusBar = [&]() {
    tft.fillRect(0, SCREEN_H - 18, SCREEN_W, 18, C_PANEL);
    tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);

    // CH indicator left
    char buf[16];
    snprintf(buf, sizeof(buf), "CH:%d", s_listenCh);
    tft.setTextColor(C_SUBTEXT);
    tft.drawString(buf, 6, SCREEN_H - 14);

    // Event count
    int total = 0;
    portENTER_CRITICAL(&s_mux);
    total = s_entryCount;
    portEXIT_CRITICAL(&s_mux);
    char cnt[16]; snprintf(cnt, sizeof(cnt), "%d events", total);
    tft.setTextColor(total > 0 ? C_RED : C_SUBTEXT);
    tft.drawString(cnt, SCREEN_W / 2 - 24, SCREEN_H - 14);

    // GROUP toggle button right
    const char* label = groupedView ? "GROUP" : "RAW";
    int btnX = SCREEN_W - 46;
    tft.fillRect(btnX, SCREEN_H - 17, 44, 16, groupedView ? C_ACCENT : C_BTN);
    tft.setTextColor(groupedView ? C_BG : C_TEXT);
    tft.drawString(label, btnX + 4, SCREEN_H - 14);
  };

  // Scroll offset
  int scrollTop = 0;
  auto drawList = [&]() {
    tft.fillRect(0, LIST_Y, SCREEN_W, SCREEN_H - LIST_Y - 18, C_BG);
    tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
    // GROUPED VIEW
    if (groupedView) {
      buildGroups();

      if (groupCount == 0) {
        tft.setTextColor(C_SUBTEXT);
        tft.drawString("Listening... no deauth frames yet", 10, LIST_Y + 20);
        return;
      }

      // Count total display rows
      // We'll just render from scrollTop and stop when we hit SCREEN_H
      int rowY = LIST_Y;
      int rowIdx = 0;

      for (int g = 0; g < groupCount; g++) {
        GroupedEntry& grp = groups[g];

        // Count this group header row
        bool headerVisible = (rowIdx >= scrollTop);
        rowIdx++;

        if (headerVisible) {
          if (rowY >= SCREEN_H - 18) break;

          uint16_t rowBg = grp.totalCount >= 10 ? tft.color565(40, 0, 0) : C_BG;
          tft.fillRect(0, rowY, SCREEN_W, ROW_H - 2, rowBg);
          tft.drawFastHLine(0, rowY + ROW_H - 2, SCREEN_W, C_PANEL);
          tft.setTextColor(C_ACCENT);
          tft.drawString(grp.expanded ? "v" : ">", COL_CH, rowY + 4);
          char oui[9]; ouiToStr(grp.oui, oui);
          tft.setTextColor(C_RED);
          tft.drawString(oui, COL_SRC, rowY + 4);
          char bestSrc[9]; macToStr(grp.bestSrc, bestSrc);
          char varBuf[12]; snprintf(varBuf, sizeof(varBuf), "x%d %s", grp.macVariants, bestSrc);
          tft.setTextColor(C_SUBTEXT);
          tft.drawString(varBuf, COL_SRC, rowY + 16);

          if (isBroadcast(grp.worstDst)) {
            tft.setTextColor(C_YELLOW);
            tft.drawString("BCAST", COL_DST, rowY + 4);
            tft.setTextColor(C_SUBTEXT);
            tft.drawString("FF:FF:FF", COL_DST, rowY + 16);
          } else {
            char dst[9]; macToStr(grp.worstDst, dst);
            tft.setTextColor(C_TEXT);
            tft.drawString(dst, COL_DST, rowY + 4);
            char dstOui[9];
            snprintf(dstOui, sizeof(dstOui), "%02X:%02X:%02X",
                     grp.worstDst[0], grp.worstDst[1], grp.worstDst[2]);
            tft.setTextColor(C_SUBTEXT);
            tft.drawString(dstOui, COL_DST, rowY + 16);
          }

          char cnt[8]; snprintf(cnt, sizeof(cnt), "%lu", (unsigned long)grp.totalCount);
          tft.setTextColor(grp.totalCount >= 10 ? C_RED : grp.totalCount >= 3 ? C_YELLOW : C_TEXT);
          tft.drawString(cnt, COL_CNT, rowY + 4);
          tft.setTextColor(C_SUBTEXT);
          tft.drawString("TOT", COL_CNT, rowY + 16);

          rowY += ROW_H;
        }

        // Rows when expanded
        if (grp.expanded) {
          DeauthEntry snap[DEAUTH_MAX_ENTRIES];
          int snapCount;
          portENTER_CRITICAL(&s_mux);
          snapCount = s_entryCount;
          memcpy(snap, s_entries, snapCount * sizeof(DeauthEntry));
          portEXIT_CRITICAL(&s_mux);

          for (int i = 0; i < snapCount; i++) {
            if (memcmp(&snap[i].src[3], grp.oui, 3) != 0) continue;
            bool childVisible = (rowIdx >= scrollTop);
            rowIdx++;
            if (!childVisible) continue;
            if (rowY >= SCREEN_H - 18) break;
            const int CHILD_H = 28;
            tft.fillRect(0, rowY, SCREEN_W, CHILD_H - 1, tft.color565(10, 10, 30));
            tft.drawFastHLine(0, rowY + CHILD_H - 1, SCREEN_W, C_PANEL);
            tft.setTextColor(C_PANEL);
            tft.drawString("|", COL_CH, rowY + 4);
            char src[9]; macToStr(snap[i].src, src);
            char srcOui[9];
            snprintf(srcOui, sizeof(srcOui), "%02X:%02X:%02X",
                     snap[i].src[0], snap[i].src[1], snap[i].src[2]);
            tft.setTextColor(C_SUBTEXT);
            tft.drawString(src, COL_SRC, rowY + 4);
            tft.drawString(srcOui, COL_SRC, rowY + 16);
            if (isBroadcast(snap[i].dst)) {
              tft.setTextColor(C_YELLOW);
              tft.drawString("BCAST", COL_DST, rowY + 4);
            } else {
              char dst[9]; macToStr(snap[i].dst, dst);
              tft.setTextColor(C_TEXT);
              tft.drawString(dst, COL_DST, rowY + 4);
            }
            char childCnt[6]; snprintf(childCnt, sizeof(childCnt), "%d", snap[i].count);
            tft.setTextColor(snap[i].count >= 10 ? C_RED : C_TEXT);
            tft.drawString(childCnt, COL_CNT, rowY + 4);
            char rsn[6]; snprintf(rsn, sizeof(rsn), "R:%d", snap[i].reason);
            tft.setTextColor(C_SUBTEXT);
            tft.drawString(rsn, COL_CNT, rowY + 16);
            tft.setTextColor(snap[i].type == 0 ? C_RED : C_YELLOW);
            tft.drawString(snap[i].type == 0 ? "D" : "X", COL_TYPE, rowY + 4);
            rowY += CHILD_H;
          }
        }
      }


    // RAW 
    } else {
      DeauthEntry snap[DEAUTH_MAX_ENTRIES];
      int snapCount;
      portENTER_CRITICAL(&s_mux);
      snapCount = s_entryCount;
      memcpy(snap, s_entries, snapCount * sizeof(DeauthEntry));
      portEXIT_CRITICAL(&s_mux);

      if (snapCount == 0) {
        tft.setTextColor(C_SUBTEXT);
        tft.drawString("Listening... no deauth frames yet", 10, LIST_Y + 20);
        return;
      }

      for (int i = 1; i < snapCount; i++) {
        DeauthEntry key = snap[i]; int j = i - 1;
        while (j >= 0 && snap[j].count < key.count) { snap[j+1] = snap[j]; j--; }
        snap[j+1] = key;
      }

      int maxScroll = max(0, snapCount - VISIBLE);
      if (scrollTop > maxScroll) scrollTop = maxScroll;

      for (int i = 0; i < VISIBLE && (scrollTop + i) < snapCount; i++) {
        const DeauthEntry& e = snap[scrollTop + i];
        int y = LIST_Y + i * ROW_H;
        uint16_t rowBg = e.count >= 10 ? tft.color565(40, 0, 0) : C_BG;
        tft.fillRect(0, y, SCREEN_W, ROW_H - 2, rowBg);
        tft.drawFastHLine(0, y + ROW_H - 2, SCREEN_W, C_PANEL);
        char ch[4]; snprintf(ch, sizeof(ch), "%d", e.channel);
        tft.setTextColor(C_ACCENT);
        tft.drawString(ch, COL_CH, y + 4);
        char src[9], dst[9];
        macToStr(e.src, src); macToStr(e.dst, dst);
        tft.setTextColor(C_RED);
        tft.drawString(src, COL_SRC, y + 4);
        char srcFull[9];
        snprintf(srcFull, sizeof(srcFull), "%02X:%02X:%02X", e.src[0], e.src[1], e.src[2]);
        tft.setTextColor(C_SUBTEXT);
        tft.drawString(srcFull, COL_SRC, y + 16);
        if (isBroadcast(e.dst)) {
          tft.setTextColor(C_YELLOW); tft.drawString("BCAST", COL_DST, y + 4);
          tft.setTextColor(C_SUBTEXT); tft.drawString("FF:FF:FF", COL_DST, y + 16);
        } else {
          tft.setTextColor(C_TEXT); tft.drawString(dst, COL_DST, y + 4);
          char dstFull[9];
          snprintf(dstFull, sizeof(dstFull), "%02X:%02X:%02X", e.dst[0], e.dst[1], e.dst[2]);
          tft.setTextColor(C_SUBTEXT); tft.drawString(dstFull, COL_DST, y + 16);
        }
        char cnt[6]; snprintf(cnt, sizeof(cnt), "%d", e.count);
        tft.setTextColor(e.count >= 10 ? C_RED : e.count >= 3 ? C_YELLOW : C_TEXT);
        tft.drawString(cnt, COL_CNT, y + 4);
        char rsn[6]; snprintf(rsn, sizeof(rsn), "R:%d", e.reason);
        tft.setTextColor(C_SUBTEXT); tft.drawString(rsn, COL_CNT, y + 16);
        tft.setTextColor(e.type == 0 ? C_RED : C_YELLOW);
        tft.drawString(e.type == 0 ? "D" : "X", COL_TYPE, y + 4);
      }
      if (snapCount > VISIBLE) {
        int trackH = SCREEN_H - LIST_Y - 18;
        int thumbH = max(12, trackH * VISIBLE / snapCount);
        int thumbY = LIST_Y + (trackH - thumbH) * scrollTop / max(1, snapCount - VISIBLE);
        tft.fillRect(SCREEN_W - 3, LIST_Y, 3, trackH, C_PANEL);
        tft.fillRect(SCREEN_W - 3, thumbY, 3, thumbH, C_ACCENT);
      }
    }
  };

  drawChrome();
  drawList();
  drawStatusBar();
  uint32_t lastHop    = millis();
  uint32_t lastRedraw = millis();
  int16_t  touchStartY = -1;
  bool     touching    = false;
  int      scrollAtTouch = 0;
  while (true) {
    // Channel hop
    if (millis() - lastHop >= (uint32_t)dwellMs) {
      s_listenCh = (s_listenCh % 13) + 1;
      esp_wifi_set_channel(s_listenCh, WIFI_SECOND_CHAN_NONE);
      lastHop = millis();
    }

    // Redraw list every .5 sec
    if (millis() - lastRedraw >= 500) {
      drawList();
      drawStatusBar();
      lastRedraw = millis();
    }

    // Touch
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      if (!touching) {
        touching     = true;
        touchStartY  = ty;
        scrollAtTouch = scrollTop;
      } else {
        int drag = touchStartY - ty;
        int newScroll = scrollAtTouch + drag / ROW_H;
        portENTER_CRITICAL(&s_mux);
        int snapCount = s_entryCount;
        portEXIT_CRITICAL(&s_mux);
        newScroll = constrain(newScroll, 0, max(0, snapCount - VISIBLE));
        if (newScroll != scrollTop) { scrollTop = newScroll; drawList(); }
      }
    } else {
      if (touching) {
        touching = false;
        int drag = abs(touchStartY - ty);
        if (drag < 8) {
          // Tap — check back button
          if (backButtonHit(tx, ty)) {
            esp_wifi_set_promiscuous(false);
            esp_wifi_set_promiscuous_rx_cb(nullptr);
            WiFi.disconnect();
            return;
          }
          // Bottom status bar — toggle GROUP/RAW on right side
          if (ty >= SCREEN_H - 18) {
            if (tx >= SCREEN_W - 46) {
              groupedView = !groupedView;
              scrollTop = 0;
              drawList();
              drawStatusBar();
            }
          }
          // Tap on a group row to expand
          if (groupedView && ty >= LIST_Y && ty < SCREEN_H - 18) {
            buildGroups();
            int rowY = LIST_Y;
            for (int g = 0; g < groupCount; g++) {
              if (ty >= rowY && ty < rowY + ROW_H) {
                groups[g].expanded = !groups[g].expanded;
                drawList();
                break;
              }
              rowY += ROW_H;
              if (groups[g].expanded) {
                DeauthEntry snap[DEAUTH_MAX_ENTRIES];
                int snapCount;
                portENTER_CRITICAL(&s_mux);
                snapCount = s_entryCount;
                memcpy(snap, s_entries, snapCount * sizeof(DeauthEntry));
                portEXIT_CRITICAL(&s_mux);
                for (int i = 0; i < snapCount; i++) {
                  if (memcmp(&snap[i].src[3], groups[g].oui, 3) == 0) rowY += 28;
                }
              }
            }
          }
        }
      }
    }

    delay(10);
  }
}