/***************************************************************************************
**  Web dashboard and run time settings
**
**  - Settings are stored in flash (NVS, Preferences namespace "weather") and survive
**    power cycles and sketch uploads.
**  - On boot the station connects to the saved WiFi. If no WiFi is saved or the
**    connection fails, it starts a setup access point (captive portal). The TFT shows
**    the access point name, password and address.
**  - When connected, the dashboard runs in the background at http://<ip>/ and
**    http://weather.local/ for changing the settings, forcing a weather refresh and
**    seeing the status.
**
**  - Firmware and LittleFS updates over WiFi (OTA), only when a dashboard password
**    is set: from the browser at http://weather.local/update, or from the Arduino IDE
**    (network port "weather"). A new firmware that crashes before it has connected
**    is rolled back to the previous one by the bootloader.
**
**  Security: the dashboard uses plain HTTP and is meant for a home network. Set a
**  dashboard password in the page to protect it (HTTP Basic auth, user "admin").
**  Saved passwords and the API key are never sent back to the browser.
***************************************************************************************/

#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>       // Firmware / LittleFS update from the browser
#include <ArduinoOTA.h>   // Firmware upload from the Arduino IDE over WiFi
#include <esp_ota_ops.h>  // Rollback control

#include "Dashboard.h"  // HTML page of the dashboard

// Optional first boot defaults (WiFi, API key), see secrets_example.h
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef OWM_API_KEY
#define OWM_API_KEY ""
#endif
#ifndef TS_CHANNEL_ID
#define TS_CHANNEL_ID 0
#endif
#ifndef TS_READ_KEY
#define TS_READ_KEY ""
#endif

// Optional ThingSpeak channel (tank monitor), only set in secrets.h
const unsigned long counterChannelNumber = TS_CHANNEL_ID;
const char* myCounterReadAPIKey = TS_READ_KEY;

/***************************************************************************************
**                          Run time settings (loaded from flash)
***************************************************************************************/
String wifiSsid, wifiPass;   // Home WiFi
String api_key;              // OpenWeather API key
String latitude, longitude;  // Forecast location
String units;                // "metric" or "imperial"
String language;             // OpenWeather language code
String adminPass;            // Dashboard password, empty = no password
uint8_t tzIndex = DEFAULT_TZ_INDEX;
uint8_t tftRotation = DEFAULT_ROTATION;
unsigned long UPDATE_INTERVAL_SECS = DEFAULT_UPDATE_MIN * 60UL;

// Time zones that can be selected in the dashboard (rules are in NTP_Time.h)
Timezone* const tzList[] = { &euEET, &euCET, &UK, &usET, &usCT, &usMT, &usAZ, &usPT, &ausET };
const char* const tzNames[] = {
  "Eastern Europe (Athens, Helsinki)", "Central Europe (Berlin, Paris, Rome)", "UK (London)",
  "US Eastern (New York)", "US Central (Chicago)", "US Mountain (Denver)",
  "US Arizona (Phoenix)", "US Pacific (Los Angeles)", "Australia Eastern (Sydney)"
};
const uint8_t TZ_COUNT = sizeof(tzList) / sizeof(tzList[0]);
Timezone* tzActive = &euEET;  // Used through the TIMEZONE macro

/***************************************************************************************
**                          Dashboard state
***************************************************************************************/
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

bool configPortal = false;          // True while the setup access point is running
bool forceWeatherUpdate = false;    // Set by the "Refresh now" button
unsigned long restartAt = 0;        // millis() time for a planned restart, 0 = none
String apSsid;                      // Name of the setup access point

// OTA update state
bool otaEnabled = false;            // OTA needs a dashboard password
bool otaRunning = false;            // True while an update is being written
String otaError = "";               // Error of the last browser update

// Last weather download, shown on the dashboard
bool lastUpdateOk = false;
String lastUpdateText = "-";
float lastTemp = NAN;
String lastDesc = "";

/***************************************************************************************
**                          Load / save settings
***************************************************************************************/
void loadConfig() {
  prefs.begin("weather", true);  // Read only
  wifiSsid  = prefs.getString("ssid", WIFI_SSID);
  wifiPass  = prefs.getString("pass", WIFI_PASSWORD);
  api_key   = prefs.getString("apikey", OWM_API_KEY);
  latitude  = prefs.getString("lat", DEFAULT_LATITUDE);
  longitude = prefs.getString("lon", DEFAULT_LONGITUDE);
  units     = prefs.getString("units", DEFAULT_UNITS);
  language  = prefs.getString("owmlang", DEFAULT_OWM_LANG);
  adminPass = prefs.getString("admin", "");
  uiLang    = prefs.getUChar("uilang", DEFAULT_UI_LANG);
  tzIndex   = prefs.getUChar("tz", DEFAULT_TZ_INDEX);
  tftRotation = prefs.getUChar("rot", DEFAULT_ROTATION);
  UPDATE_INTERVAL_SECS = prefs.getUShort("interval", DEFAULT_UPDATE_MIN) * 60UL;
  prefs.end();

  // Guard against invalid stored values
  if (uiLang > 1) uiLang = 0;
  if (tzIndex >= TZ_COUNT) tzIndex = 0;
  if (tftRotation != 0 && tftRotation != 2) tftRotation = 0;
  tzActive = tzList[tzIndex];
}

void saveConfig() {
  prefs.begin("weather", false);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putString("apikey", api_key);
  prefs.putString("lat", latitude);
  prefs.putString("lon", longitude);
  prefs.putString("units", units);
  prefs.putString("owmlang", language);
  prefs.putString("admin", adminPass);
  prefs.putUChar("uilang", uiLang);
  prefs.putUChar("tz", tzIndex);
  prefs.putUChar("rot", tftRotation);
  prefs.putUShort("interval", UPDATE_INTERVAL_SECS / 60);
  prefs.end();
}

void factoryReset() {
  prefs.begin("weather", false);
  prefs.clear();
  prefs.end();
}

/***************************************************************************************
**                          Helpers
***************************************************************************************/
// Escape a String for use inside a JSON string value
String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if ((uint8_t)c < 0x20) out += ' ';
    else out += c;
  }
  return out;
}

// HTTP Basic auth check, skipped in setup mode or when no password is set
bool checkAuth() {
  if (configPortal || adminPass.length() == 0) return true;
  if (!server.authenticate("admin", adminPass.c_str())) {
    server.requestAuthentication(BASIC_AUTH, "Weather station");
    return false;
  }
  return true;
}

void sendJsonError(const String& msg) {
  server.send(400, "application/json", "{\"ok\":false,\"error\":\"" + jsonEscape(msg) + "\"}");
}

/***************************************************************************************
**                          HTTP handlers
***************************************************************************************/
void handleRoot() {
  if (!checkAuth()) return;
  server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
}

// Current settings. Secrets are not included, only whether they are set.
void handleGetConfig() {
  if (!checkAuth()) return;
  String j = "{";
  j += "\"ssid\":\"" + jsonEscape(wifiSsid) + "\",";
  j += "\"passSet\":" + String(wifiPass.length() ? "true" : "false") + ",";
  j += "\"apiKeySet\":" + String(api_key.length() ? "true" : "false") + ",";
  j += "\"apiKeyEnd\":\"" + jsonEscape(api_key.length() > 4 ? api_key.substring(api_key.length() - 4) : "") + "\",";
  j += "\"adminSet\":" + String(adminPass.length() ? "true" : "false") + ",";
  j += "\"otaEnabled\":" + String(otaEnabled ? "true" : "false") + ",";
  j += "\"lat\":\"" + jsonEscape(latitude) + "\",";
  j += "\"lon\":\"" + jsonEscape(longitude) + "\",";
  j += "\"units\":\"" + jsonEscape(units) + "\",";
  j += "\"owmLang\":\"" + jsonEscape(language) + "\",";
  j += "\"uiLang\":" + String(uiLang) + ",";
  j += "\"tz\":" + String(tzIndex) + ",";
  j += "\"rotation\":" + String(tftRotation) + ",";
  j += "\"interval\":" + String(UPDATE_INTERVAL_SECS / 60) + ",";
  j += "\"tzNames\":[";
  for (uint8_t i = 0; i < TZ_COUNT; i++) {
    if (i) j += ",";
    j += "\"" + jsonEscape(tzNames[i]) + "\"";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleStatus() {
  if (!checkAuth()) return;
  time_t local = TIMEZONE.toLocal(now(), &tz1_Code);
  char timeBuf[24];
  snprintf(timeBuf, sizeof(timeBuf), "%02d/%02d/%04d %02d:%02d:%02d",
           day(local), month(local), year(local), hour(local), minute(local), second(local));
  String j = "{";
  j += "\"mode\":\"" + String(configPortal ? "setup" : "station") + "\",";
  j += "\"ssid\":\"" + jsonEscape(configPortal ? apSsid : WiFi.SSID()) + "\",";
  j += "\"ip\":\"" + (configPortal ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  j += "\"rssi\":" + String(configPortal ? 0 : WiFi.RSSI()) + ",";
  j += "\"uptime\":" + String(millis() / 1000) + ",";
  j += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"time\":\"" + String(timeBuf) + "\",";
  j += "\"lastOk\":" + String(lastUpdateOk ? "true" : "false") + ",";
  j += "\"lastUpdate\":\"" + jsonEscape(lastUpdateText) + "\",";
  j += "\"temp\":" + (isnan(lastTemp) ? String("null") : String(lastTemp, 1)) + ",";
  j += "\"desc\":\"" + jsonEscape(lastDesc) + "\",";
  j += "\"units\":\"" + jsonEscape(units) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

// Save settings from the form, then restart to apply them
void handleSave() {
  if (!checkAuth()) return;

  String ssid = server.arg("ssid");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32) return sendJsonError("WiFi name must be 1-32 characters");

  String pass = server.arg("pass");  // Empty = keep the saved password
  if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63))
    return sendJsonError("WiFi password must be 8-63 characters");

  String key = server.arg("apikey");  // Empty = keep the saved key
  key.trim();
  if (key.length() > 0 && key.length() != 32) return sendJsonError("OpenWeather API key must be 32 characters");
  if (key.length() == 0 && api_key.length() == 0) return sendJsonError("OpenWeather API key is required");

  String lat = server.arg("lat"), lon = server.arg("lon");
  lat.trim();
  lon.trim();
  float latF = lat.toFloat(), lonF = lon.toFloat();
  if (lat.length() == 0 || latF < -90 || latF > 90) return sendJsonError("Latitude must be -90 to 90");
  if (lon.length() == 0 || lonF < -180 || lonF > 180) return sendJsonError("Longitude must be -180 to 180");

  String u = server.arg("units");
  if (u != "metric" && u != "imperial") return sendJsonError("Invalid units");

  String ol = server.arg("owmlang");
  ol.trim();
  ol.toLowerCase();
  if (ol.length() < 2 || ol.length() > 5) return sendJsonError("Invalid OpenWeather language code");

  int ul = server.arg("uilang").toInt();
  int tz = server.arg("tz").toInt();
  int rot = server.arg("rotation").toInt();
  int interval = server.arg("interval").toInt();
  if (ul < 0 || ul > 1) return sendJsonError("Invalid display language");
  if (tz < 0 || tz >= TZ_COUNT) return sendJsonError("Invalid time zone");
  if (rot != 0 && rot != 2) return sendJsonError("Invalid rotation");
  if (interval < 5 || interval > 180) return sendJsonError("Update interval must be 5-180 minutes");

  String admin = server.arg("admin");  // Empty = keep, unless "clearadmin" is set
  if (admin.length() > 0 && admin.length() < 4) return sendJsonError("Dashboard password must be at least 4 characters");

  // All values are valid, store them
  wifiSsid = ssid;
  if (pass.length()) wifiPass = pass;
  if (key.length()) api_key = key;
  latitude = lat;
  longitude = lon;
  units = u;
  language = ol;
  uiLang = ul;
  tzIndex = tz;
  tftRotation = rot;
  UPDATE_INTERVAL_SECS = interval * 60UL;
  if (server.hasArg("clearadmin")) adminPass = "";
  else if (admin.length()) adminPass = admin;
  saveConfig();

  server.send(200, "application/json", "{\"ok\":true}");
  restartAt = millis() + 1500;  // Restart after the reply has been sent
}

// List nearby WiFi networks, strongest first, without duplicates
void handleScan() {
  if (!checkAuth()) return;
  int n = WiFi.scanNetworks();
  String j = "[";
  int count = 0;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == s) dup = true;
    if (dup) continue;
    if (count++) j += ",";
    j += "{\"ssid\":\"" + jsonEscape(s) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  j += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

void handleRefresh() {
  if (!checkAuth()) return;
  forceWeatherUpdate = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleReset() {
  if (!checkAuth()) return;
  factoryReset();
  server.send(200, "application/json", "{\"ok\":true}");
  restartAt = millis() + 1500;
}

// In setup mode every unknown address (phone captive portal checks) goes to the page
void handleNotFound() {
  if (configPortal) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

/***************************************************************************************
**                          OTA: firmware / LittleFS update
***************************************************************************************/
// Progress on the TFT during an update (browser or Arduino IDE)
void drawOtaProgress(const char* what, uint8_t percent) {
  static int8_t lastPercent = -1;
  if (lastPercent < 0) {
    tft.unloadFont();
    tft.fillScreen(TFT_BACKGROUND);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
    tft.setTextPadding(0);
    tft.drawString(what, 120, 140, 4);  // Built in font 4, LittleFS may be updating
  }
  if (percent != lastPercent) {
    ui.drawProgressBar(20, 170, 200, 15, percent, TFT_WHITE, TFT_BLUE);
    lastPercent = percent;
  }
  if (percent >= 100) lastPercent = -1;
}

// Update page. Only with a dashboard password, the browser asks for it.
void handleUpdatePage() {
  if (!otaEnabled) {
    server.send(403, "text/plain", "Set a dashboard password first to enable updates.");
    return;
  }
  if (!checkAuth()) return;
  server.send_P(200, "text/html; charset=utf-8", UPDATE_HTML);
}

// Receives the uploaded file in chunks and writes it to flash.
// ?type=fw for firmware (.ino.bin), ?type=fs for a LittleFS image of the data folder.
void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaError = "";
    if (!otaEnabled || !server.authenticate("admin", adminPass.c_str())) {
      otaError = "Not authorised";
      return;
    }
    bool fs = server.arg("type") == "fs";
    Serial.printf("OTA %s update: %s
", fs ? "LittleFS" : "firmware", up.filename.c_str());
    if (fs) LittleFS.end();  // Unmount before overwriting
    otaRunning = true;
    drawOtaProgress(fs ? "Updating files..." : "Updating firmware...", 0);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, fs ? U_SPIFFS : U_FLASH)) otaError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaError.length()) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) otaError = Update.errorString();
    unsigned long total = server.header("Content-Length").toInt();
    if (total) drawOtaProgress("", min(99UL, (unsigned long)Update.progress() * 100UL / total));
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaError.length() == 0 && !Update.end(true)) otaError = Update.errorString();
    if (otaError.length() == 0) drawOtaProgress("", 100);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaError = "Upload aborted";
  }
}

// Called after the upload has finished: report the result and restart
void handleUpdateDone() {
  // otaRunning stays true: loop() draws nothing until the restart (LittleFS may be unmounted)
  if (otaError.length()) {
    Serial.println("OTA failed: " + otaError);
    server.send(500, "application/json", "{"ok":false,"error":"" + jsonEscape(otaError) + ""}");
    restartAt = millis() + 3000;  // Restart to redraw the screen and remount LittleFS
    return;
  }
  Serial.println("OTA done, restarting");
  server.send(200, "application/json", "{"ok":true}");
  restartAt = millis() + 1500;
}

// Upload from the Arduino IDE (network port), same password as the dashboard
void startArduinoOTA() {
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  ArduinoOTA.setPassword(adminPass.c_str());
  ArduinoOTA.onStart([]() {
    otaRunning = true;
    bool fs = ArduinoOTA.getCommand() == U_SPIFFS;
    if (fs) LittleFS.end();
    Serial.println(fs ? "IDE OTA: LittleFS" : "IDE OTA: firmware");
    drawOtaProgress(fs ? "Updating files..." : "Updating firmware...", 0);
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    drawOtaProgress("", total ? done * 100UL / total : 0);
  });
  ArduinoOTA.onEnd([]() {
    drawOtaProgress("", 100);
    Serial.println("IDE OTA done");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("IDE OTA error %u
", e);
    restartAt = millis() + 3000;
  });
  ArduinoOTA.begin();  // Also starts mDNS with the hostname
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
}

/***************************************************************************************
**                          Call often from loop(): serve the dashboard
***************************************************************************************/
void handleWeb() {
  if (configPortal) dnsServer.processNextRequest();
  server.handleClient();
  if (otaEnabled) ArduinoOTA.handle();
  if (restartAt && (long)(millis() - restartAt) >= 0) {
    Serial.println("Restarting to apply the new settings...");
    delay(100);
    ESP.restart();
  }
}

/***************************************************************************************
**                          OTA rollback
***************************************************************************************/
// Tell the ESP32 core not to accept a new firmware automatically at boot. The sketch
// accepts it with markFirmwareValid() once its network services are running. If a new
// firmware crashes before that, the bootloader starts the previous one again.
// extern "C": replaces the weak C function of the same name in the ESP32 core.
extern "C" bool verifyRollbackLater() {
  return true;
}

void markFirmwareValid() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("New firmware accepted");
  }
}

/***************************************************************************************
**                          WiFi connection
***************************************************************************************/
// Try the saved WiFi, returns true when connected
bool connectWiFi(unsigned long timeoutMs) {
  if (wifiSsid.length() == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// Start the dashboard on the home network (station mode)
void startDashboard() {
  // Header used for the progress bar of browser updates
  const char* headers[] = { "Content-Length" };
  server.collectHeaders(headers, 1);
  setupRoutes();
  server.begin();

  otaEnabled = adminPass.length() > 0;
  if (otaEnabled) startArduinoOTA();  // Starts mDNS too
  else MDNS.begin(MDNS_HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  Serial.println(otaEnabled ? "OTA updates enabled" : "OTA updates disabled (no dashboard password)");
  markFirmwareValid();  // Network services run, keep this firmware

  Serial.print("Dashboard: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/  or  http://" MDNS_HOSTNAME ".local/");
}

// Show the setup instructions on the TFT
void drawSetupScreen() {
  tft.fillScreen(TFT_BACKGROUND);
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(0);

  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  tft.drawString(TXT_SETUP, 120, 40);
  tft.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  tft.drawString(TXT_SETUP_WIFI, 120, 90);
  tft.setTextColor(TFT_YELLOW, TFT_BACKGROUND);
  tft.drawString(apSsid, 120, 112);
  tft.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  tft.drawString(TXT_SETUP_PASS, 120, 150);
  tft.setTextColor(TFT_YELLOW, TFT_BACKGROUND);
  tft.drawString(CONFIG_AP_PASSWORD, 120, 172);
  tft.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  tft.drawString(TXT_SETUP_OPEN, 120, 210);
  tft.setTextColor(TFT_GREEN, TFT_BACKGROUND);
  tft.drawString("http://" + WiFi.softAPIP().toString(), 120, 232);
  tft.unloadFont();
}

// Setup mode: own access point with captive portal. Never returns, the station
// restarts after the settings are saved or after CONFIG_PORTAL_MINUTES.
void startConfigPortal() {
  configPortal = true;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[32];
  snprintf(name, sizeof(name), "WeatherStation-%02X%02X", mac[4], mac[5]);
  apSsid = name;

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);  // STA part is needed for the network scan
  WiFi.softAP(apSsid.c_str(), CONFIG_AP_PASSWORD);
  delay(200);
  dnsServer.start(53, "*", WiFi.softAPIP());  // Answer every DNS request with our IP
  setupRoutes();
  server.begin();

  markFirmwareValid();  // Setup mode works, keep this firmware

  Serial.printf("Setup mode: connect to WiFi \"%s\" (password \"%s\") and open http://%s/\n",
                apSsid.c_str(), CONFIG_AP_PASSWORD, WiFi.softAPIP().toString().c_str());
  drawSetupScreen();

  unsigned long portalStart = millis();
  while (true) {
    handleWeb();
    // Restart to retry the saved WiFi (e.g. after a router power cut), unless nothing
    // is configured yet or someone is connected to the setup access point
    if (WiFi.softAPgetStationNum() > 0) portalStart = millis();
    if (wifiSsid.length() && millis() - portalStart > CONFIG_PORTAL_MINUTES * 60000UL) ESP.restart();
    delay(2);
  }
}
