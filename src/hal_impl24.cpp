#include "bs_hal.h"
#include "touch24.h"
#include "config24.h"
#include <Arduino.h>

namespace hal {

  bool touched(int16_t &x, int16_t &y) {
    return tftTouched(x, y);
  }

  void waitRelease() {
    waitTouchRelease();
  }

  uint32_t millisNow() {
    return millis();
  }

  void alertBeep() {
    if (SPEAKER_PIN < 0) return;
    pinMode(SPEAKER_PIN, OUTPUT);          // please DONT MESS WITH THIS PIN HIGH, it can blow the audio amp on the 2.4" S024
    for (int i = 0; i < 80; i++) {
        digitalWrite(SPEAKER_PIN, HIGH);
        delayMicroseconds(500);
        digitalWrite(SPEAKER_PIN, LOW);
        delayMicroseconds(500);
    }
}

} 