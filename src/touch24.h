#pragma once
#include <stdint.h>

void touchInit();                              
bool tftTouched(int16_t &tx, int16_t &ty);     // calibrated screen coords
void waitTouchRelease();                       // block until finger lifts

bool touchNeedsCalibration();                  // true if never calibrated
void touchCalibrate();                         // 4-corner poke, saves to NVS
void touchResetCalibration();                  // wipe cal (forces re-cal next boot)