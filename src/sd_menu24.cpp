#include "sd_menu24.h"
#include "sd_utils24.h"
#include "ui24.h"
#include "touch24.h"
#include "config24.h"
#include <SD.h>
#include <new>
extern TFT_eSPI tft;
#define SD_LIST_Y      (HEADER_H + 28)
#define SD_PATH_Y      (HEADER_H + 4)
#define SD_PATH_H      22
#define SD_ROW_H       26
#define SD_MAX_VISIBLE ((SCREEN_H - SD_LIST_Y) / SD_ROW_H)
#define SD_MAX_ENTRIES 64
#define SD_SCROLLBAR_W 5

struct SDEntry {
  String name;
  bool   isDir;
  size_t size;
};
static SDEntry entries[SD_MAX_ENTRIES];
static int     entryCount = 0;
static int     scrollTop  = 0;
static void loadDir(const String& path) {
  entryCount = 0;
  scrollTop  = 0;
  File dir = SD.open(path.c_str());
  if (!dir || !dir.isDirectory()) return;
  for (int pass = 0; pass < 2; pass++) {
    dir.rewindDirectory();
    File f = dir.openNextFile();
    while (f && entryCount < SD_MAX_ENTRIES) {
      String name = String(f.name());
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (name.length() == 0 || name[0] == '.' ||
          name == "System Volume Information" ||
          name == "RECYCLER" || name == "$RECYCLE.BIN") {
        f.close();
        f = dir.openNextFile();
        continue;
      }

      bool isDir = f.isDirectory();
      if ((pass == 0 && isDir) || (pass == 1 && !isDir)) {
        if (name.length() > 0 && name[0] != '.') {
          entries[entryCount++] = { name, isDir, isDir ? 0 : (size_t)f.size() };
        }
      }
      f.close();
      f = dir.openNextFile();
    }
  }
  dir.close();
}

static String fmtSize(size_t bytes) {
  if (bytes < 1024)           { char b[12]; snprintf(b, sizeof(b), "%uB", (unsigned)bytes);              return String(b); }
  else if (bytes < 1024*1024) { char b[12]; snprintf(b, sizeof(b), "%uK", (unsigned)(bytes/1024));        return String(b); }
  else                        { char b[12]; snprintf(b, sizeof(b), "%uM", (unsigned)(bytes/(1024*1024))); return String(b); }
}
static uint16_t entryColor(const SDEntry& e) {
  if (e.isDir) return C_YELLOW;
  String ext = sdFileExt(e.name);
  if (ext == ".ir")   return C_ACCENT;
  if (ext == ".txt")  return C_TEXT;
  if (ext == ".csv")  return C_CYAN;
  if (ext == ".json") return C_CYAN;
  return C_SUBTEXT;
}
static const char* entryIcon(const SDEntry& e) {
  if (e.isDir) return "D";
  String ext = sdFileExt(e.name);
  if (ext == ".ir")  return "I";
  if (ext == ".txt") return "T";
  return "F";
}
static void drawPathBar(const String& path) {
  tft.fillRect(0, SD_PATH_Y, SCREEN_W, SD_PATH_H, C_PANEL);
  tft.drawFastHLine(0, SD_PATH_Y + SD_PATH_H - 1, SCREEN_W, C_BORDER);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(C_SUBTEXT);
  String display = path;
  while (display.length() > 34) display = "~" + display.substring(display.indexOf('/', 2));
  tft.drawString(display.c_str(), 6, SD_PATH_Y + 6);
}
static void drawList(int highlighted = -1) {
  tft.fillRect(0, SD_LIST_Y, SCREEN_W - SD_SCROLLBAR_W, SCREEN_H - SD_LIST_Y, 0x0000);
  if (entryCount == 0) {
    tft.setFreeFont(NULL);
    tft.setTextColor(C_SUBTEXT);
    tft.setTextSize(1);
    tft.drawString("(empty)", 10, SD_LIST_Y + 10);
  }

  int visible = min(SD_MAX_VISIBLE, entryCount - scrollTop);
  for (int i = 0; i < visible; i++) {
    int idx = scrollTop + i;
    int y   = SD_LIST_Y + i * SD_ROW_H;
    SDEntry& e = entries[idx];
    bool hl = (idx == highlighted);
    if (hl) tft.fillRect(0, y, SCREEN_W - SD_SCROLLBAR_W, SD_ROW_H, C_PANEL);
    uint16_t col = entryColor(e);
    tft.fillRect(4, y + 5, 14, 16, col);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(0x0000);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(entryIcon(e), 7, y + 9);
    tft.setTextColor(hl ? C_ACCENT : col);
    String name = e.name;
    if (name.length() > 24) name = name.substring(0, 23) + "~";
    tft.drawString(name.c_str(), 22, y + 8);
    if (!e.isDir) {
      String sz = fmtSize(e.size);
      int sw = sz.length() * 6;
      tft.setTextColor(C_SUBTEXT);
      tft.drawString(sz.c_str(), SCREEN_W - SD_SCROLLBAR_W - sw - 4, y + 8);
    } else {
      tft.setTextColor(C_SUBTEXT);
      tft.drawString(">", SCREEN_W - SD_SCROLLBAR_W - 10, y + 8);
    }
    tft.drawFastHLine(0, y + SD_ROW_H - 1, SCREEN_W - SD_SCROLLBAR_W, C_PANEL);
  }

  if (entryCount > SD_MAX_VISIBLE) {
    int barH   = SCREEN_H - SD_LIST_Y;
    int thumbH = max(12, barH * SD_MAX_VISIBLE / entryCount);
    int thumbY = SD_LIST_Y + (barH - thumbH) * scrollTop / max(1, entryCount - SD_MAX_VISIBLE);
    tft.fillRect(SCREEN_W - SD_SCROLLBAR_W, SD_LIST_Y, SD_SCROLLBAR_W, barH, C_PANEL);
    tft.fillRect(SCREEN_W - SD_SCROLLBAR_W, thumbY, SD_SCROLLBAR_W, thumbH, C_ACCENT);
  }
}

static void viewTextFile(const String& path) {
  File f = SD.open(path.c_str());
  if (!f) {
    tft.fillScreen(0x0000);
    drawHeader("View File", true);
    tft.setFreeFont(NULL); tft.setTextSize(1);
    tft.setTextColor(C_RED);
    tft.drawString("Nope Try Again", 10, HEADER_H + 20);
    delay(1500);
    return;
  }

  const int MAX_CHARS = 2000;
  char* buf = (char*)malloc(MAX_CHARS + 1);
  if (!buf) { f.close(); return; }
  int len = 0;
  while (f.available() && len < MAX_CHARS) buf[len++] = (char)f.read();
  buf[len] = 0;
  f.close();
  const int MAX_LINES = 80;
  String* lines = (String*)malloc(MAX_LINES * sizeof(String));
  if (!lines) { free(buf); return; }
  for (int i = 0; i < MAX_LINES; i++) new(&lines[i]) String();
  int lineCount = 0;
  String current = "";
  for (int i = 0; i < len && lineCount < MAX_LINES; i++) {
    char c = buf[i];
    if (c == '\r') continue;
    if (c == '\n') { lines[lineCount++] = current; current = ""; }
    else {
      current += c;
      if (current.length() >= 34) { lines[lineCount++] = current; current = ""; }
    }
  }
  if (current.length() > 0 && lineCount < MAX_LINES) lines[lineCount++] = current;
  const int lineH  = 14;
  const int listY  = HEADER_H + 4;
  const int maxVis = (SCREEN_H - listY) / lineH;
  int vScrollTop   = 0;
  auto drawPage = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("View File", true);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    int visible = min(maxVis, lineCount - vScrollTop);
    for (int i = 0; i < visible; i++) {
      tft.setTextColor(C_TEXT);
      tft.drawString(lines[vScrollTop + i].c_str(), 4, listY + i * lineH);
    }
    if (lineCount > maxVis) {
      int barH   = SCREEN_H - listY;
      int thumbH = max(12, barH * maxVis / lineCount);
      int thumbY = listY + (barH - thumbH) * vScrollTop / max(1, lineCount - maxVis);
      tft.fillRect(SCREEN_W - 4, listY, 4, barH, C_PANEL);
      tft.fillRect(SCREEN_W - 4, thumbY, 4, thumbH, C_ACCENT);
    }
  };

  drawPage();
  int16_t touchStartY = -1, lastTouchY = -1;
  bool touching = false;
  while (true) {
    int16_t tx, ty;
    bool touched = tftTouched(tx, ty);
    if (touched) {
      if (!touching) { touchStartY = ty; lastTouchY = ty; touching = true; }
      else {
        int dy = lastTouchY - ty;
        if (abs(dy) >= lineH) {
          vScrollTop = constrain(vScrollTop + (dy > 0 ? 1 : -1), 0, max(0, lineCount - maxVis));
          lastTouchY = ty;
          drawPage();
        }
      }
    } else {
      if (touching) {
        int drag = abs(touchStartY - lastTouchY);
        touching = false;
        if (drag < 10 && backButtonHit(tx, ty)) {
          for (int i = 0; i < MAX_LINES; i++) lines[i].~String();
          free(lines);
          free(buf);
          return;
        }
      }
    }
    delay(16);
  }
}

static bool showOptionsMenu(const String& dirPath, int entryIdx) {
  SDEntry& e = entries[entryIdx];
  String fullPath = sdJoinPath(dirPath, e.name);
  const char* opts[4];
  int optCount = 0;
  if (!e.isDir) {
    String ext = sdFileExt(e.name);
    if (ext == ".txt") opts[optCount++] = "View";
    opts[optCount++] = "Rename";
    opts[optCount++] = "Delete";
  } else {
    opts[optCount++] = "Open";
    opts[optCount++] = "Delete";
  }
  opts[optCount++] = "Cancel";
  const int menuW  = SCREEN_W - 40;
  const int menuX  = 20;
  const int optH   = 38;
  const int titleH = 28;
  const int menuH  = titleH + optCount * optH + 8;
  const int menuY  = (SCREEN_H - menuH) / 2;
  for (int y = 0; y < SCREEN_H; y += 2) tft.drawFastHLine(0, y, SCREEN_W, C_BG);
  tft.fillRoundRect(menuX, menuY, menuW, menuH, 6, C_PANEL);
  tft.drawRoundRect(menuX, menuY, menuW, menuH, 6, C_BORDER);
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(C_ACCENT);
  String title = e.name;
  if (title.length() > 26) title = title.substring(0, 25) + "~";
  tft.drawString(title.c_str(), menuX + 8, menuY + 8);
  tft.drawFastHLine(menuX, menuY + titleH - 1, menuW, C_BORDER);

  for (int i = 0; i < optCount; i++) {
    int oy = menuY + titleH + i * optH;
    bool isCancel = (strcmp(opts[i], "Cancel") == 0);
    bool isDelete = (strcmp(opts[i], "Delete") == 0);
    tft.fillRect(menuX + 1, oy, menuW - 2, optH - 1, C_PANEL);
    tft.setFreeFont(NULL);
    tft.setTextSize(2);
    tft.setTextColor(isDelete ? C_RED : isCancel ? C_SUBTEXT : C_TEXT);
    int tw = strlen(opts[i]) * 12;
    tft.drawString(opts[i], menuX + (menuW - tw) / 2, oy + optH/2 - 8);
    if (i < optCount - 1) tft.drawFastHLine(menuX, oy + optH - 1, menuW, C_BORDER);
  }

  while (true) {
    int16_t tx, ty;
    if (tftTouched(tx, ty)) {
      waitTouchRelease();
      for (int i = 0; i < optCount; i++) {
        int oy = menuY + titleH + i * optH;
        if (tx >= menuX && tx < menuX + menuW && ty >= oy && ty < oy + optH) {
          const char* chosen = opts[i];
          if (strcmp(chosen, "Cancel") == 0) return false;
          if (strcmp(chosen, "Open") == 0)   return true;
          if (strcmp(chosen, "View") == 0) {
            tft.setFreeFont(NULL); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
            viewTextFile(fullPath);
            return false;
          }
          if (strcmp(chosen, "Rename") == 0) {
            char newName[64];
            strncpy(newName, e.name.c_str(), sizeof(newName));
            bool ok = onScreenKeyboard("Rename:", newName, sizeof(newName));
            if (ok && strlen(newName) > 0) {
              String newPath = sdJoinPath(dirPath, String(newName));
              if (SD.rename(fullPath.c_str(), newPath.c_str()))
                Serial.printf("[SD] Renamed %s -> %s\n", fullPath.c_str(), newPath.c_str());
              else
                Serial.println("[SD] Rename failed");
              return true;
            }
            return false;
          }

          if (strcmp(chosen, "Burn") == 0) {
            tft.fillScreen(C_BG);
            tft.fillRect(0, 0, SCREEN_W, HEADER_H, C_PANEL);
            tft.fillRect(0, 0, 3, HEADER_H, C_ACCENT2);
            tft.drawFastHLine(0, HEADER_H, SCREEN_W, C_ACCENT);
            tft.setFreeFont(NULL);
            tft.setTextSize(1);
            tft.setTextColor(C_RED);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("CONFIRM Burn", SCREEN_W / 2, HEADER_H / 2 + 2);
            tft.setTextDatum(TL_DATUM);
            tft.drawString("Burn this file?", 10, HEADER_H + 16);
            tft.setTextColor(C_TEXT);
            String dn = e.name;
            if (dn.length() > 28) dn = dn.substring(0, 27) + "~";
            tft.drawString(dn.c_str(), 10, HEADER_H + 34);

            int btnY = HEADER_H + 80;
            int btnW = (SCREEN_W - 30) / 2;
            tft.fillRoundRect(10,        btnY, btnW, 40, 4, C_RED);
            tft.fillRoundRect(20 + btnW, btnY, btnW, 40, 4, C_BTN);
            tft.drawRoundRect(20 + btnW, btnY, btnW, 40, 4, C_BORDER);
            tft.setTextColor(C_TEXT);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("YES", 10 + btnW / 2,        btnY + 20);
            tft.drawString("NO",  20 + btnW + btnW / 2, btnY + 20);
            tft.setTextDatum(TL_DATUM);

            while (true) {
              int16_t cx, cy;
              if (tftTouched(cx, cy)) {
                waitTouchRelease();
                if (backButtonHit(cx, cy)) return false;
                if (cx >= 10 && cx < 10 + btnW && cy >= btnY && cy < btnY + 40) {
                  bool removed = e.isDir ? SD.rmdir(fullPath.c_str())
                                         : SD.remove(fullPath.c_str());
                  Serial.printf("[SD] Burn %s: %s\n", fullPath.c_str(), removed ? "OK" : "FAIL");
                  return true;
                }
                if (cx >= 20 + btnW && cx < 20 + btnW + btnW && cy >= btnY && cy < btnY + 40)
                  return false;
              }
            }
          }
        }
      }
    }
  }
}

void sdMenu() {
  if (!sdReady()) {
    if (!sdInit()) {
      tft.fillScreen(C_BG);
      drawHeader("SD Card", true);
      tft.setFreeFont(NULL);
      tft.setTextSize(2);
      tft.setTextColor(C_RED);
      tft.drawString("No SD card", 30, HEADER_H + 40);
      tft.setTextSize(1);
      tft.setTextColor(C_SUBTEXT);
      tft.drawString("Insert card and reboot", 8, HEADER_H + 70);
      while (true) {
        int16_t tx, ty;
        if (tftTouched(tx, ty)) {
          waitTouchRelease();
          if (backButtonHit(tx, ty)) return;
        }
      }
    }
  }

  String pathStack[8];
  int    depth = 0;
  pathStack[0] = "/";
  auto currentPath = [&]() -> String { return pathStack[depth]; };
  auto fullRedraw = [&]() {
    tft.fillScreen(C_BG);
    drawHeader("SD Card", true);
    drawPathBar(currentPath());
    drawList();
  };
  loadDir(currentPath());
  fullRedraw();

  int16_t touchStartY = -1, lastTouchY = -1;
  bool    touching    = false;
  while (true) {
    int16_t tx, ty;
    bool touched = tftTouched(tx, ty);
    if (touched) {
      if (!touching) { touchStartY = ty; lastTouchY = ty; touching = true; }
      else {
        int dy = lastTouchY - ty;
        if (abs(dy) >= SD_ROW_H / 2) {
          int delta  = dy > 0 ? 1 : -1;
          int newTop = constrain(scrollTop + delta, 0, max(0, entryCount - SD_MAX_VISIBLE));
          if (newTop != scrollTop) { scrollTop = newTop; drawList(); }
          lastTouchY = ty;
        }
      }
    } else {
      if (touching) {
        int dragDist = abs(touchStartY - lastTouchY);
        touching = false;
        if (dragDist < 8 && backButtonHit(tx, ty)) {
          if (depth > 0) { depth--; loadDir(currentPath()); fullRedraw(); }
          else return;
          continue;
        }
        if (dragDist < 10 && ty >= SD_LIST_Y) {
          int row = (ty - SD_LIST_Y) / SD_ROW_H;
          int idx = scrollTop + row;
          if (idx >= 0 && idx < entryCount) {
            drawList(idx);
            delay(80);
            SDEntry& e = entries[idx];
            if (e.isDir) {
              if (depth < 7) { depth++; pathStack[depth] = sdJoinPath(currentPath(), e.name); }
              loadDir(currentPath());
              fullRedraw();
            } else {
              bool reload = showOptionsMenu(currentPath(), idx);
              if (reload) loadDir(currentPath());
              fullRedraw();
            }
          }
        }
      }
    }
    delay(16);
  }
}