2.4-Byte-Shield-
ByteShield DC-01 — 2.4" CYD Firmware
Blue Team firmware for the ESP32-2432S024R (CYD) — part of the ByteShield DC-01 project by Duggie Tech.

Current version: v0.2.3ish (see src/config24.h)

What it does
ByteShield turns a CYD board into a pocket warning system and flock detection tool.

WiFi — scan networks, connect/save credentials (including from SD), auto-reconnect on boot
BLE — scanning, fox-hunting (RSSI-based), and a Chameleon Ultra BLE client (slot management, tag scanning)
GPS — live position via ATGM336H Flock Detection reaches out and updates baised on location 
RFID - Not Live yet in 2.4 might not add
Tools — display invert, brightness, speaker test, touch calibration wipe/redo, screen timeout
SD — on-device file explorer
Info / About — device/firmware info screens
LeashLock — network watchdog: locks to your connected AP's channel and watches for deauth/disassoc attacks
Roku ECP remote — SSDP-discovers Roku devices on the LAN and drives them as a touchscreen remote
Hardware
Board: ESP32-2432S024R (2.4", ILI9341 or ST7789 panel — auto-detected/selected on first boot, saved to NVS)
Touch: XPT2046 resistive, dedicated SPI bus (CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36)

GPS: HardwareSerial(1), pins defined at runtime in GPS_RX_PIN/GPS_TX_PIN (main.cpp) — Amazon/IPS board variant uses RX=22 instead of RX=35
Speaker (GPIO26): do not toggle at audio frequency. Set once in setup() and leave alone — toggling it fast can damage the amplifier IC on this board family.
Full pin map: src/config24.h

Build
Requires PlatformIO.

pio run -e esp32-2432S024R
pio run -e esp32-2432S024R -t upload
Dependencies (see platformio.ini):

bodmer/TFT_eSPI
PaulStoffregen/XPT2046_Touchscreen
h2zero/NimBLE-Arduino
mikalhart/TinyGPSPlus
byteshield-core — shared board-agnostic library (LeashLock, HAL contract, BLE Chameleon Ultra protocol, UI primitives), consumed via local path during development
TFT_eSPI display config is set entirely through build_flags in platformio.ini — do not edit User_Setup.h.

Attributions
Some protocol/command details in this firmware are implemented from public, third-party documentation and open-source references rather than being reverse-engineered from scratch:

Chameleon Ultra BLE protocol (command codes, frame format) — cross-referenced against the RfidResearchGroup/ChameleonUltra firmware repo and the GameTec-live/ChameleonUltraGUI companion app.
BLE UART transport — uses the standard Nordic UART Service (NUS) UUIDs (6E400001/002/003), a public Nordic Semiconductor specification, not something specific to this project.
Roku remote control — implements Roku's official, published External Control Protocol (ECP), including SSDP discovery (roku:ecp) and the /keypress/, /query/device-info HTTP endpoints.


Some pins/config in config24.h are flagged in-line as unverified on specific hardware revisions (e.g. speaker GPIO) — check comments before relying on them.
