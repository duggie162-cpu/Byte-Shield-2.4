#pragma once
#include <TFT_eSPI.h>
#include "config24.h"

extern TFT_eSPI tft;

struct Button {
  int16_t x, y, w, h;
  const char* label;
  uint16_t color;
  uint16_t textColor;
};

// Screens / components
void showBootScreen();
void drawHeader(const char* title, bool showBack = false);
void drawMainGrid(const char* labels[8]);
int8_t mainGridHit(int16_t tx, int16_t ty);
bool backButtonHit(int16_t tx, int16_t ty);

void drawButton(const Button& b, bool pressed = false);
void showToast(const char* msg, uint16_t color = C_ACCENT);
void textViewer(const char* title, const char* body);
bool onScreenKeyboard(const char* prompt, char* buf, uint8_t bufLen);

// LeashLock status indicator in the bottom status bar
void leashlockDrawIndicator();