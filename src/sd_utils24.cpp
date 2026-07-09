#include "sd_utils24.h"
#include "config24.h"

static bool _sdReady = false;
static SPIClass sdSPI(HSPI);

bool sdInit() {
  if (_sdReady) return true;   // one-shot — never re-init the bus

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("[SD] Mount failed");
    _sdReady = false;
    return false;
  }
  Serial.printf("[SD] Mounted. Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
  _sdReady = true;
  return true;
}

bool sdReady() { return _sdReady; }

String sdJoinPath(const String& dir, const String& name) {
  if (dir.endsWith("/")) return dir + name;
  return dir + "/" + name;
}

String sdFileExt(const String& name) {
  int dot = name.lastIndexOf('.');
  if (dot < 0) return "";
  String ext = name.substring(dot);
  ext.toLowerCase();
  return ext;
}

bool loadWifiConfig(String& ssid, String& pass) {
  if (!sdInit()) return false;
  File f = SD.open("/config.txt", FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("SSID="))      ssid = line.substring(5);
    else if (line.startsWith("PASS=")) pass = line.substring(5);
  }
  f.close();
  return (ssid.length() > 0);
}

void saveWifiConfig(const String& ssid, const String& pass) {
  if (!sdInit()) return;
  File f = SD.open("/config.txt", FILE_WRITE);
  if (!f) return;
  f.println("SSID=" + ssid);
  f.println("PASS=" + pass);
  f.close();
}