/***************************************************************************************
**  Settings for the ESP32-S3 OpenWeather station
**
**  Most settings (WiFi, API key, location, language, time zone...) are changed at run
**  time from the web dashboard and saved in flash, see WebConfig.h. The values below
**  are only the defaults used on first boot or after a factory reset.
**
**  Optional: put your own first boot defaults in secrets.h (copy secrets_example.h),
**  that file is in .gitignore and never uploaded.
***************************************************************************************/

//////////////////////////////////////////////////////////////////////////////////////////
//  Board and display notes
//
//  Arduino IDE, Tools menu (ESP32 core 3.x):
//    Board            : "ESP32S3 Dev Module"
//    USB CDC On Boot  : "Disabled" for boards with a USB-UART chip (CH343/CP2102),
//                       "Enabled" when using the S3 native USB port
//    Partition Scheme : one with at least 1.5MB SPIFFS, e.g.
//                       "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
//    PSRAM            : "OPI PSRAM" for N8R8/N16R8 modules, else "Disabled"
//
//  The fonts and icons in the data folder must be uploaded to LittleFS (see README).
//
//  TFT pins are set in the TFT_eSPI library, not here. A ready setup file is in
//  extras/Setup_ESP32S3_ILI9341_Weather.h (copy it to TFT_eSPI/User_Setups and
//  select it in TFT_eSPI/User_Setup_Select.h). Pins used:
//    MOSI 3, SCLK 4, CS 1, DC 10, RST 0, MISO not connected
//  Do NOT use GPIO 26-32 (SPI flash/PSRAM), nor 33-37 on octal PSRAM modules.
//////////////////////////////////////////////////////////////////////////////////////////

#if defined(ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
#warning "All_Settings.h is configured for ESP32-S3, check the board selection"
#endif

//////////////////////////////////////////////////////////////////////////////////////////
//  First boot defaults (all can be changed later in the web dashboard)
//////////////////////////////////////////////////////////////////////////////////////////
#define DEFAULT_LATITUDE     "38.0482"   // 90.0000 to -90.0000, negative for South
#define DEFAULT_LONGITUDE    "23.5407"   // 180.000 to -180.000, negative for West
#define DEFAULT_UNITS        "metric"    // "metric" (oC, m/s, mBar) or "imperial" (oF, mph, in)
#define DEFAULT_OWM_LANG     "en"        // "en" = sketch's own Greek/English descriptions
#define DEFAULT_UI_LANG      0           // Display language: 0 = Greek, 1 = English
#define DEFAULT_TZ_INDEX     0           // 0 = Eastern Europe, see tzNames[] in WebConfig.h
#define DEFAULT_UPDATE_MIN   15          // Weather download interval in minutes
#define DEFAULT_ROTATION     0           // TFT rotation: 0 or 2 (upside down)

// Setup access point, started when no WiFi is configured or the connection fails.
// The password is shown on the TFT. WPA2 needs at least 8 characters.
#define CONFIG_AP_PASSWORD   "weather123"
#define CONFIG_PORTAL_MINUTES 5          // Restart and retry WiFi after this time in setup mode
#define MDNS_HOSTNAME        "weather"   // Dashboard also at http://weather.local/

//////////////////////////////////////////////////////////////////////////////////////////
//  Fixed settings
//////////////////////////////////////////////////////////////////////////////////////////

// Screen background colour (the icons have a black background)
#define TFT_BACKGROUND  TFT_BLACK

// Time zone in use, selected in the dashboard (tzActive is set in WebConfig.h)
#define TIMEZONE (*tzActive)

// Clock redraw and optional ThingSpeak read intervals
const unsigned long UPDATE_INTERVAL_MILS = 950UL;          // Clock redraw, ~1 second
const unsigned long UPDATE_INTERVAL_2MIN = 2UL * 60000UL;  // ThingSpeak read (optional)

//////////////////////////////////////////////////////////////////////////////////////////
//  Display texts, index 0 = Greek, 1 = English. Translate here for other languages;
//  the fonts in data/fonts must contain the characters used.
//  The macros below pick the language selected in the dashboard (uiLang).
//////////////////////////////////////////////////////////////////////////////////////////
uint8_t uiLang = DEFAULT_UI_LANG;  // 0 = Greek, 1 = English, loaded from flash at boot

const String shortDOW_L[2][8] = {
  {"???", "ΚΥΡ", "ΔΕΥ", "ΤΡΙ", "ΤΕΤ", "ΠΕΜ", "ΠΑΡ", "ΣΑΒ"},
  {"???", "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}
};
const String shortMONTH_L[2][13] = {
  {"???", "ΙΑΝ", "ΦΕΒ", "ΜΑΡ", "ΑΠΡ", "ΜΑΙ", "ΙΟΝ", "ΙΟΛ", "ΑΥΓ", "ΣΕΠ", "ΟΚΤ", "ΝΟΕ", "ΔΕΚ"},
  {"???", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"}
};
const String moonPhase_L[2][8] = {
  {"Νέα Σελήνη", "Αύξουσα", "1ο τέταρτο", "Αύξουσα", "Πανσέληνος", "Φθίνουσα", "3ο τέταρτο", "Φθίνουσα"},
  {"New moon", "Waxing", "1st quarter", "Waxing", "Full moon", "Waning", "3rd quarter", "Waning"}
};
const char* const sunStr_L[2]      = {"Ήλιος", "Sun"};
const char* const cloudStr_L[2]    = {"Νέφωση", "Cloud"};
const char* const humidityStr_L[2] = {"Υγρασία", "Humidity"};
const char* const riseStr_L[2]     = {"A: ", "R: "};  // Sunrise (Ανατολή / Rise)
const char* const setStr_L[2]      = {"Δ: ", "S: "};  // Sunset (Δύση / Set)

const char* const TXT_FORMAT_FS_L[2]  = {"Φορμάρει το LittleFS, περιμένετε!", "Formatting LittleFS, please wait!"};
const char* const TXT_ORIGINAL_L[2]   = {"Πρωτότυπο από: blog.squix.org", "Original by: blog.squix.org"};
const char* const TXT_ADAPTED_L[2]    = {"Προσαρμογή από: ManosMar", "Adapted by: ManosMar"};
const char* const TXT_CONNECTING_L[2] = {"Σύνδεση με δίκτυο WiFi...", "Connecting to WiFi..."};
const char* const TXT_FETCHING_L[2]   = {"Λήψη δεδομένων καιρού...", "Fetching weather data..."};
const char* const TXT_PROG_TIME_L[2]  = {"Χρόνος ανανέωσης...", "Updating time..."};
const char* const TXT_PROG_DATA_L[2]  = {"Ανανέωση δεδομένων...", "Updating conditions..."};
const char* const TXT_PROG_READY_L[2] = {"Έτοιμο...", "Done..."};
const char* const TXT_SETUP_L[2]      = {"Ρυθμίσεις", "Setup"};
const char* const TXT_SETUP_WIFI_L[2] = {"Συνδεθείτε στο WiFi:", "Connect to WiFi:"};
const char* const TXT_SETUP_PASS_L[2] = {"Κωδικός:", "Password:"};
const char* const TXT_SETUP_OPEN_L[2] = {"και ανοίξτε:", "and open:"};
const char* const TXT_DASHBOARD_L[2]  = {"Ρυθμίσεις:", "Settings:"};

// Short names used in the sketch, they follow the selected language
#define shortDOW        (shortDOW_L[uiLang])
#define shortMONTH      (shortMONTH_L[uiLang])
#define moonPhase       (moonPhase_L[uiLang])
#define sunStr          (sunStr_L[uiLang])
#define cloudStr        (cloudStr_L[uiLang])
#define humidityStr     (humidityStr_L[uiLang])
#define riseStr         (riseStr_L[uiLang])
#define setStr          (setStr_L[uiLang])
#define TXT_FORMAT_FS   (TXT_FORMAT_FS_L[uiLang])
#define TXT_ORIGINAL    (TXT_ORIGINAL_L[uiLang])
#define TXT_ADAPTED     (TXT_ADAPTED_L[uiLang])
#define TXT_CONNECTING  (TXT_CONNECTING_L[uiLang])
#define TXT_FETCHING    (TXT_FETCHING_L[uiLang])
#define TXT_PROG_TIME   (TXT_PROG_TIME_L[uiLang])
#define TXT_PROG_DATA   (TXT_PROG_DATA_L[uiLang])
#define TXT_PROG_READY  (TXT_PROG_READY_L[uiLang])
#define TXT_SETUP       (TXT_SETUP_L[uiLang])
#define TXT_SETUP_WIFI  (TXT_SETUP_WIFI_L[uiLang])
#define TXT_SETUP_PASS  (TXT_SETUP_PASS_L[uiLang])
#define TXT_SETUP_OPEN  (TXT_SETUP_OPEN_L[uiLang])
#define TXT_DASHBOARD   (TXT_DASHBOARD_L[uiLang])
