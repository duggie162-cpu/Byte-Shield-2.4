#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

bool sdInit();
bool sdReady();


String sdJoinPath(const String& dir, const String& name);
String sdFileExt(const String& name);  

bool loadWifiConfig(String& ssid, String& pass);
void saveWifiConfig(const String& ssid, const String& pass);