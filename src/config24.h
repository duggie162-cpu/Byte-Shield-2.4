#pragma once

//  Identity -
#define APP_NAME      "BYTE_SHIELD"
#define APP_VERSION   "v0.2.3-24"
#define APP_BOARD     "ESP32-2432S024R"

//  Display variant (manual choice on first boot via NVS) 
#define NVS_DISPLAY_NS    "disp24"
#define NVS_DISPLAY_KEY   "variant"
#define DISP_VARIANT_ILI  0
#define DISP_VARIANT_ST   1

// Dont go to the light
#define TFT_BL_PIN    27

// pants size
#define SCREEN_W   240
#define SCREEN_H   320

// Touch me driver XPT2046 r
// Confirmed on the 2.4" R variant: CLK=25 MOSI=32 MISO=39 CS=33 IRQ=36 lucky break
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33

// Touch calibration 
#define TOUCH_CAL_XMIN   420
#define TOUCH_CAL_XMAX   3660
#define TOUCH_CAL_YMIN   290
#define TOUCH_CAL_YMAX   3625

// Colors I cant see
#define C_BG       0x0000   // black
#define C_ACCENT   0x073F   // cyan - primary dosent work on ALTs 
#define C_ACCENT2  0xFD40   // orange - alert/active maybe red 
#define C_SUBTEXT  0x0228   // cyan
#define C_TEXT     0xFFFF   // white
extern uint16_t C_PANEL;    // alts redefined basied on board choise 
#define C_RED      0xF800   // alert red for buttons and warnings
extern uint16_t C_BTN;      // alts redefined basied on board choise 
#define C_BTN_HL   0x0228   // button highlight
#define C_YELLOW   0xFFE0   // mid-signal / warning
#define C_GREEN    0x07E0   // ok / active
#define C_CYAN     0x07FF   // info / uptime
#define C_BORDER   0x0228   // panel might need to go black 

// Layout
#define HEADER_H      28
#define GRID_COLS     2
#define GRID_ROWS     4

// Audio 
// TODO: confirm the buzzer/speaker GPIO on the 2.4" S024 before enabling.
// Leave as -1 to keep silent until verified on hardware if your using this on neew device or port never call high can blow amp be carfulll plz
#define SPEAKER_PIN   26

// microSD
// pin notes MISO-19, MOSI-23, SCK-18, CS-5
#define SD_MISO   19
#define SD_MOSI   23
#define SD_SCK    18
#define SD_CS     5

// GPS pins — Amazon/IPS variant uses RX=22 instead of RX=35
extern uint8_t GPS_RX_PIN;
extern uint8_t GPS_TX_PIN;

// alt 
extern uint8_t g_dispVariant;