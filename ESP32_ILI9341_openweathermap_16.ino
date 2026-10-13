/***************************************************************************************
**  ESP32-S3 OpenWeather station for a 240x320 ILI9341 TFT
**
**  Shows time, date, current weather, a 4 day forecast, sunrise/sunset, moon phase,
**  cloud cover and humidity from the OpenWeather 5 day / 3 hour forecast API.
**  Greek or English user interface, selected in the web dashboard.
**
**  Hardware : ESP32-S3 (tested on N16R8) + ILI9341 2.4"/2.8" SPI TFT
**  Display  : pins are set in the TFT_eSPI library, see
**             extras/Setup_ESP32S3_ILI9341_Weather.h and README.md
**  Files    : fonts and icons in the data folder must be uploaded to LittleFS
**
**  Configure : web dashboard at http://<station ip>/ or http://weather.local/
**              (WiFi, API key, location, units, language, time zone, rotation).
**              On first boot the station starts a setup access point, see WebConfig.h.
**              All_Settings.h holds the first boot defaults and the display texts.
**
**  Based on the TFT_eSPI_OpenWeather_LittleFS example by Bodmer:
**    https://github.com/Bodmer/OpenWeather
**  Original by Daniel Eichhorn (https://blog.squix.org), MIT license at end of file.
**  ESP32-S3 port, Greek UI and layout changes by ManosMar.
***************************************************************************************/

// Optional features
#define SERIAL_MESSAGES     // Print the full weather report to the Serial Monitor
//#define SCREEN_SERVER     // Send TFT screenshots to a PC (add ScreenGrabServer.ino from Bodmer's example)
//#define RANDOM_LOCATION   // Test only: random location on every refresh, to see all icons
//#define FORMAT_LittleFS   // Erase LittleFS on boot (deletes all files!), then disable again

/***************************************************************************************
**                          Libraries
***************************************************************************************/
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>          // Built into the ESP32 core (2.x/3.x)
#include <SPI.h>
#include <TFT_eSPI.h>          // https://github.com/Bodmer/TFT_eSPI
#include <FlickerFreePrint.h>  // https://github.com/KrisKasprzak/FlickerFreePrint
#include <ThingSpeak.h>        // https://github.com/mathworks/thingspeak-arduino

#include "GfxUi.h"             // BMP drawing and progress bar (part of this sketch)

// WiFi library for the selected board
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_ARCH_RP2040)
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFi.h>
#else
#include <WiFiNINA.h>
#endif
#else  // ESP32
#include <WiFi.h>
#endif

#include "All_Settings.h"      // Defaults, fixed settings and display texts
#include <JSON_Decoder.h>      // https://github.com/Bodmer/JSON_Decoder (not the Library Manager one)
#include <OpenWeather.h>       // https://github.com/Bodmer/OpenWeather
#include "NTP_Time.h"          // NTP time sync and time zones (part of this sketch)

// Anti-aliased fonts stored in LittleFS (data/fonts), Noto Sans (SIL Open Font License)
// with Greek characters, made with extras/fonts/mkvlw.py.
// New fonts can be made with the Processing sketch in TFT_eSPI/Tools/Create_Smooth_Font
#define AA_FONT_SMALL         "fonts/NotoSansBold15"  // 15 px Noto Sans Bold, most text
#define AA_FONT_LARGE         "fonts/NotoSansBold36"  // 36 px Noto Sans Bold, temperature
#define AA_FONT_SMALL_SPECIAL "fonts/NotoSans30"      // 30 px Noto Sans Regular, clock

/***************************************************************************************
**                          Globals and class instances
***************************************************************************************/
TFT_eSPI tft = TFT_eSPI();
FlickerFreePrint<TFT_eSPI> Data1(&tft, TFT_YELLOW, TFT_BACKGROUND);  // Flicker free numbers
OW_Weather ow;            // OpenWeather client
OW_forecast* forecast;    // Forecast data, created and deleted on every refresh
WiFiClient client;        // Used by ThingSpeak
GfxUi ui = GfxUi(&tft);   // BMP drawing and progress bar

boolean booted = true;    // True until the first pass of loop() has finished

// Optional ThingSpeak channel (tank monitor), see retrieveThingSpeak().
// Channel ID and read key come from secrets.h, see WebConfig.h
const int FieldNumber1 = 1;  // Volume (lt)
const int FieldNumber2 = 2;  // Humidity (%)
const int FieldNumber3 = 3;  // Temperature (oC)
const int FieldNumber4 = 4;  // Height (cm)
boolean refuel = false;      // True when the tank volume is below 100 lt

#include "WebConfig.h"       // Run time settings, setup access point and web dashboard

/***************************************************************************************
**                          Declare prototypes
***************************************************************************************/
void updateData();
void drawProgress(uint8_t percentage, String text);
void drawTime();
void drawDate();
void drawCurrentWeather();
void drawForecast();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
void separateFloat(float num, int& integerPart, int& decimalPart);
void drawDecTemp();
const char* getMeteoconIcon(uint16_t id, bool today);
String getMainDescription(String main);
void drawAstronomy();
void drawSeparator(uint16_t y);
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour);
void retrieveThingSpeak();

String strDate(time_t unixTime);
String strTime(time_t unixTime);
void printWeather(void);
int leftOffset(String text, String sub);
int rightOffset(String text, String sub);
int splitIndex(String text);
int getNextDayIndex(void);

float temperature;          // Current temperature
int int_part, dec_part;     // Temperature split into integer and one decimal digit
int statusCode = 0;         // Last ThingSpeak read status (200 = OK)
uint8_t offset_X = 40;      // X offset of the ThingSpeak values
unsigned long lastDownloadUpdate, lastTimeUpdate, lastThingspeakUpdate;  // millis() timers

/***************************************************************************************
**                          JPEG decoder callback, draws one decoded block
***************************************************************************************/
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Stop further decoding as image is running off bottom of screen
  if (y >= tft.height()) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  tft.pushImage(x, y, w, h, bitmap);

  // Return 1 to decode next block
  return 1;
}

/***************************************************************************************
**                          Setup
***************************************************************************************/
void setup() {
  Serial.begin(115200);
  // ESP32-S3 native USB: wait up to 3s for the Serial Monitor, then continue anyway
  while (!Serial && millis() < 3000) delay(10);
  Serial.println(F("Starting file " __FILE__ " from " __DATE__ ", " __TIME__ ));

  loadConfig();  // Settings saved from the web dashboard (or first boot defaults)

  tft.begin();
  tft.setRotation(tftRotation);  // Portrait 240x320: 0 = normal, 2 = upside down
  tft.fillScreen(TFT_BACKGROUND);

  // Mount LittleFS. If this fails the data folder has not been uploaded (see README)
  if (!LittleFS.begin()) {
    Serial.println("Αποτυχία αρχικοποίησης Flash FS!");
    while (1) yield();  // Nothing can be drawn without fonts and icons, stop here
  }
  Serial.println("\nFlash FS διαθέσιμη!");

// Erase LittleFS if FORMAT_LittleFS is defined (takes some time).
// Disable it again and re-upload, or the files are erased on every boot!
#ifdef FORMAT_LittleFS
  tft.setTextDatum(BC_DATUM);  // Bottom Centre datum
  tft.drawString(TXT_FORMAT_FS, 120, 195);
  LittleFS.format();
#endif

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);
  TJpgDec.setSwapBytes(true);  // May need to swap the jpg colour bytes (endianess)

  // Optional splash screen images (data/splash/*.jpg), skipped if not present
  if (LittleFS.exists("/splash/OpenWeather.jpg") == true) {
    TJpgDec.drawFsJpg(0, 0, "/splash/OpenWeather.jpg", LittleFS);
  }
  if (LittleFS.exists("/splash/Elefsina.jpg") == true) {
    TJpgDec.drawFsJpg(0, 125, "/splash/Elefsina.jpg", LittleFS);
  }

  delay(2000);

  // Clear bottom section of screen
  tft.fillRect(0, 206, 240, 320 - 206, TFT_BACKGROUND);

  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);  // Bottom Centre datum
  tft.setTextColor(TFT_LIGHTGREY, TFT_BACKGROUND);

  // Credits
  tft.drawString(TXT_ORIGINAL, 120, 280);
  tft.drawString(TXT_ADAPTED, 120, 300);

  tft.setTextColor(TFT_YELLOW, TFT_BACKGROUND);

  delay(2000);

  tft.fillRect(0, 206, 240, 320 - 206, TFT_BACKGROUND);

  tft.drawString(TXT_CONNECTING, 120, 240);
  tft.setTextPadding(240);  // Pad next drawString() text to full width to over-write old text

  // Connect to the saved WiFi. Without saved WiFi, or if the connection fails within
  // 30 seconds, start the setup access point (shown on the TFT). startConfigPortal()
  // does not return, the station restarts after the settings are saved.
  if (!connectWiFi(30000)) {
    tft.unloadFont();
    startConfigPortal();
  }
  startDashboard();  // Web dashboard in the background on the home network

  tft.setTextDatum(BC_DATUM);
  tft.setTextPadding(240);        // Pad next drawString() text to full width to over-write old text
  tft.drawString(" ", 120, 220);  // Clear line above using set padding width
  tft.drawString(TXT_FETCHING, 120, 240);

  // Show where the dashboard is, stays visible until the first weather screen
  tft.setTextColor(TFT_LIGHTGREY, TFT_BACKGROUND);
  tft.drawString(String(TXT_DASHBOARD) + " http://" + WiFi.localIP().toString(), 120, 300);

  // Start UDP for NTP, the first time sync is done in loop()
  udp.begin(localPort);
  tft.unloadFont();
  ThingSpeak.begin(client);
  lastDownloadUpdate = lastTimeUpdate = lastThingspeakUpdate = millis();
}

/***************************************************************************************
**                          Loop
***************************************************************************************/
void loop() {

  // 0. Serve the web dashboard (also restarts the station after saving settings)
  handleWeb();
  if (otaRunning) return;  // Update in progress: keep the screen and LittleFS untouched

  // 1. Sync the clock with NTP on boot and then once per minute. This must run
  //    before the first weather screen, or the date is drawn as 01 JAN 1970.
  if (booted || minute() != lastMinute) {
    syncTime();  // Waits up to 500ms for the NTP reply
    lastMinute = minute();

#ifdef SCREEN_SERVER
    screenServer();
#endif
  }

  // 2. Download new weather data on boot and then every UPDATE_INTERVAL_SECS
  //    or when "Refresh weather now" is pressed in the dashboard
  if (booted || forceWeatherUpdate || (millis() - lastDownloadUpdate > 1000UL * UPDATE_INTERVAL_SECS)) {
    forceWeatherUpdate = false;
    updateData();
    lastDownloadUpdate = millis();
  }

  // 3. Redraw the clock about once per second, from the local Time library clock
  if (millis() - lastTimeUpdate > UPDATE_INTERVAL_MILS) {
    drawTime();
    lastTimeUpdate = millis();
  }

  // 4. Redraw the date at midnight, or when the first valid NTP time arrives late
  static int lastDay = -1;
  time_t local_now = TIMEZONE.toLocal(now(), &tz1_Code);
  if (!booted && day(local_now) != lastDay) {
    if (lastDay != -1) {  // Skip on boot, updateData() has just drawn the date
      drawDate();
      tft.unloadFont();   // drawDate() leaves the small font loaded
    }
    lastDay = day(local_now);
  }

  // Optional ThingSpeak tank monitor, disabled. Note: it draws over the weather screen.
//  if (millis() - lastThingspeakUpdate > UPDATE_INTERVAL_2MIN) {
//    retrieveThingSpeak();
//    lastThingspeakUpdate = millis();
//  }

  booted = false;
}

/***************************************************************************************
**                          Fetch the weather data  and update screen
***************************************************************************************/
// Shows progress (a bar on boot, a small circle top left afterwards) while downloading
void updateData() {

  if (booted) drawProgress(20, TXT_PROG_TIME);
  else fillSegment(22, 22, 0, (int)(20 * 3.6), 16, TFT_NAVY);

  if (booted) drawProgress(50, TXT_PROG_DATA);
  else fillSegment(22, 22, 0, (int)(50 * 3.6), 16, TFT_NAVY);

  // Create the structure that holds the retrieved weather
  forecast = new OW_forecast;
  //  current = new OW_current;

#ifdef RANDOM_LOCATION  // Randomly choose a place on Earth to test icons etc
  String latitude = "";
  latitude = (random(180) - 90);
  String longitude = "";
  longitude = (random(360) - 180);
  Serial.print("Γεωγρ. πλάτος = ");
  Serial.print(latitude);
  Serial.print(", Γεωγρ. μήκος = ");
  Serial.println(longitude);
#endif

  bool parsed = ow.getForecast(forecast, api_key, latitude, longitude, units, language);
  //  bool fullData = ow.getForecast(current, api_key, latitude, longitude, units, language);

  // Remember the result for the dashboard status
  lastUpdateOk = parsed;
  lastUpdateText = strDate(now());
  if (parsed) {
    lastTemp = forecast->temp[0];
    lastDesc = forecast->description[0];
  }

  if (parsed) Serial.println("Ελήφθησαν δεδομένα ΟΚ");
  else Serial.println("Αποτυχία λήψης δεδομένων");

  //Serial.print("Free heap = "); Serial.println(ESP.getFreeHeap(), DEC);

  printWeather();  // For debug, turn on output with #define SERIAL_MESSAGES

  if (booted) {
    drawProgress(100, TXT_PROG_READY);
    delay(2000);
    tft.fillScreen(TFT_BACKGROUND);
  } else {
    fillSegment(22, 22, 0, 360, 16, TFT_NAVY);
    fillSegment(22, 22, 0, 360, 22, TFT_BACKGROUND);
  }

  if (parsed) {
    tft.loadFont(AA_FONT_SMALL, LittleFS);
    drawCurrentWeather();
    drawDate();
    drawForecast();
    drawAstronomy();
    tft.unloadFont();

    // Draw the large temperature digits here, so the large font is loaded only once
    tft.loadFont(AA_FONT_LARGE, LittleFS);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BACKGROUND);

    tft.setTextPadding(tft.textWidth("-88"));  // Max width of values

    String weatherText = "";

    // Integer part must match the decimal drawn by drawDecTemp(), so round to tenths
    // once and split, instead of rounding the integer separately (20.7 showed "21.7")
    temperature = forecast->temp[0];
    separateFloat(temperature, int_part, dec_part);
    if (temperature <= -0.05f && int_part == 0) weatherText = "-0";  // e.g. -0.4 keeps its sign
    else weatherText = String(int_part);
    tft.drawString(weatherText, 209, 95);
    drawDecTemp();  // ".d" decimal digit in the small font
    tft.unloadFont();

  } else {
    Serial.println("Αποτυχία λήψης δεδομένων καιρού!");
  }
  delete forecast;  // Free the memory until the next refresh
}

/***************************************************************************************
**                Draw the decimal digit of the temperature, e.g. ".6"
***************************************************************************************/
void drawDecTemp() {
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(TR_DATUM);
  Data1.setTextColor(TFT_YELLOW, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth(".8"));
  // int_part and dec_part already set by separateFloat() in updateData()
  tft.setCursor(211, 111);
  tft.print(".");
  Data1.print(dec_part);
}

/***************************************************************************************
**        Split a float into integer part and one decimal digit, rounded to 0.1
***************************************************************************************/
void separateFloat(float num, int& integerPart, int& decimalPart) {
  long tenths = lroundf(num * 10.0f);   // Round once to one decimal, e.g. 20.96 -> 210
  integerPart = tenths / 10;            // Truncates toward zero, e.g. -37 -> -3
  decimalPart = labs(tenths % 10);      // Always positive digit, e.g. -37 -> 7
}

/***************************************************************************************
**                          Update progress bar
***************************************************************************************/
void drawProgress(uint8_t percentage, String text) {
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  tft.setTextPadding(240);
  tft.drawString(text, 120, 260);

  ui.drawProgressBar(10, 269, 240 - 20, 15, percentage, TFT_WHITE, TFT_BLUE);

  tft.setTextPadding(0);
  tft.unloadFont();
}

/***************************************************************************************
**                          Draw the clock digits
***************************************************************************************/
void drawTime() {
  tft.loadFont(AA_FONT_SMALL_SPECIAL, LittleFS);

  // Convert UTC to local time, returns zone code in tz1_Code, e.g "GMT"
  time_t local_time = TIMEZONE.toLocal(now(), &tz1_Code);
  String timeNow = "";

  if (hour(local_time) < 10) timeNow += "0";
  timeNow += hour(local_time);
  timeNow += ":";
  if (minute(local_time) < 10) timeNow += "0";
  timeNow += minute(local_time);
  timeNow += ":";
  if (second(local_time) < 10) timeNow += "0";
  timeNow += second(local_time);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth("44:44:44"));  // Overwrite the old time completely
  tft.drawString(timeNow, 127, 53);

  drawSeparator(51);

  tft.setTextPadding(0);

  tft.unloadFont();
}

/***************************************************************************************
**                Draw the date: "13  ΣΕΠ" on the left, year on the right
***************************************************************************************/
void drawDate() {
  String dateNow = "";
  String yearNow = "";
  time_t local_date = TIMEZONE.toLocal(now(), &tz1_Code);
  tft.loadFont(AA_FONT_SMALL, LittleFS);

  if (day(local_date) < 10) dateNow += "0";
  dateNow += day(local_date);
  dateNow += "  ";
  dateNow += shortMONTH[month(local_date)];
  yearNow += year(local_date);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_RED, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth("88 JAN"));  // Overwrite the old date completely
  tft.drawString(dateNow, 34, 50);
  tft.setTextPadding(tft.textWidth("8888"));
  tft.drawString(yearNow, 213, 50);
  tft.setTextPadding(0);
}

/***************************************************************************************
**    Description of the weather condition id (Greek or English), used when language = "en"
**       See https://openweathermap.org/weather-conditions for the id list
***************************************************************************************/
const char* getWeatherDescription(uint16_t id, bool today) {
  if (uiLang == 0) {  // Greek
  if (today && id / 100 == 8 && (forecast->dt[0] < forecast->sunrise || forecast->dt[0] > forecast->sunset)) id += 1000;

  if (id == 800) return "Ηλιοφάνεια";
  if (id == 801) return "Λίγη νέφωση";
  if (id == 802) return "Αρκετή νέφωση";
  if (id == 803) return "Αυξημένη νέφωση";
  if (id == 804) return "Βαριά νέφωση";
  if (id == 1800) return "Ξαστεριά";
  if (id == 600) return "Ελαφρή χιονόπτωση";
  if (id == 601) return "Χιονόπτωση";
  if (id == 602) return "Σφοδρή χιονόπτωση";
  if (id == 500) return "Ελαφρά βροχόπτωση";
  if (id == 501) return "Ενισχυμένη βροχόπτωση";
  if ((id == 502) || (id == 503) || (id == 521)) return "Καταιγίδα";
  if (id == 522) return "Σφοδρή Καταιγίδα";
  if (id == 200) return "Καταιγίδα με κεραυνούς";
  if (id == 741) return "Ομίχλη";
  if (id == 751) return "Σκόνη";
  if (id == 721) return "Καταχνιά";  // 721 = haze (was "Χαλαζόπτωση", hail)
  if (id == 311) return "Ψιχάλα";    // 311 = drizzle rain (was "Χιονόνερο", sleet)
  return "Αγνωστο";
  }

  // English
  if (today && id / 100 == 8 && (forecast->dt[0] < forecast->sunrise || forecast->dt[0] > forecast->sunset)) id += 1000;

  if (id == 800) return "Sunny";
  if (id == 801) return "Few clouds";
  if (id == 802) return "Scattered clouds";
  if (id == 803) return "Broken clouds";
  if (id == 804) return "Overcast";
  if (id == 1800) return "Clear sky";
  if (id == 600) return "Light snow";
  if (id == 601) return "Snow";
  if (id == 602) return "Heavy snow";
  if (id == 500) return "Light rain";
  if (id == 501) return "Moderate rain";
  if ((id == 502) || (id == 503) || (id == 521)) return "Heavy rain";
  if (id == 522) return "Heavy showers";
  if (id == 200) return "Thunderstorm";
  if (id == 741) return "Fog";
  if (id == 751) return "Sand";
  if (id == 721) return "Haze";
  if (id == 311) return "Drizzle rain";
  return "Unknown";
}

/***************************************************************************************
**          Short name of the weather group ("main" field), under the icon
***************************************************************************************/
String getMainDescription(String main) {
  if (uiLang != 0) return main;  // English: OpenWeather group name as is
  if (main == "Clear") return "Αίθριος";
  if (main == "Clouds") return "Συννεφιά";
  if (main == "Rain") return "Βροχή";
  if (main == "Drizzle") return "Ψιχάλα";
  if (main == "Thunderstorm") return "Καταιγίδα";
  if (main == "Snow") return "Χιόνι";
  if (main == "Mist" || main == "Fog" || main == "Haze") return "Ομίχλη";
  if (main == "Dust" || main == "Sand" || main == "Ash") return "Σκόνη";
  if (main == "Smoke") return "Καπνός";
  if (main == "Squall") return "Μπουρίνι";
  if (main == "Tornado") return "Ανεμοστρόβιλος";
  return main;
}

/***************************************************************************************
**   Draw the current weather: location, icon, description, temperature units, wind
**   and pressure. The large temperature digits are drawn in updateData().
***************************************************************************************/
void drawCurrentWeather() {

  // Location line, e.g. "Elefsina, 38.05N, 23.54E"
  String location = forecast->city_name;
  location += ", " + String(fabs(ow.lat), 2) + (ow.lat < 0 ? "S" : "N");
  location += ", " + String(fabs(ow.lon), 2) + (ow.lon < 0 ? "W" : "E");

  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  tft.setTextDatum(TC_DATUM);  // Centred at top of screen
  tft.setTextPadding(230);     // Clear old text across the full width
  tft.drawString(location, 120, 5);

  String weatherText = "";

  // 100x100 weather icon (day/night version chosen from sunrise/sunset)
  String weatherIcon = getMeteoconIcon(forecast->id[0], true);
  ui.drawBmp("/icon/" + weatherIcon + ".bmp", 0, 53);

  // Short weather group label under the icon (drawn after the icon so it is not overwritten)
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_SKYBLUE, TFT_BACKGROUND);
  tft.setTextPadding(96);  // Icon width less a small margin
  tft.drawString(getMainDescription(forecast->main[0]), 48, 136);

  // Description top right, split over two lines. With language = "en" the Greek table
  // above is used, otherwise OpenWeather's own translated description.
  if (language == "en") {
    weatherText = getWeatherDescription(forecast->id[0], false);
  } else
    weatherText = forecast->description[0];

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  int splitPoint = 0;
  int xpos = 235;
  splitPoint = splitIndex(weatherText);
  tft.setTextPadding(xpos - 100);  // xpos - icon width
  if (splitPoint) tft.drawString(weatherText.substring(0, splitPoint), xpos, 69);
  else tft.drawString(" ", xpos, 69);
  tft.drawString(weatherText.substring(splitPoint), xpos, 86);
  tft.setTextColor(TFT_YELLOW, TFT_BACKGROUND);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(0);
  if (units == "metric") tft.drawString("oC", 237, 95);
  else tft.drawString("oF", 237, 95);

  // Wind speed and pressure under the compass
  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  weatherText = String(forecast->wind_speed[0], 0);
  if (units == "metric") weatherText += " m/s";
  else weatherText += " mph";
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(tft.textWidth("888 m/s"));  // Widest expected value
  tft.drawString(weatherText, 120, 136);

  if (units == "imperial") {
    weatherText = forecast->pressure[0];
    weatherText += " in";
  } else {
    weatherText = String(forecast->pressure[0], 0);
    weatherText += " mBar";
  }

  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(tft.textWidth(" 8888mBar"));
  tft.drawString(weatherText, 230, 136);

  // Wind direction: 8 compass points, compass BMP from data/wind and its letter above
  int windAngle = (forecast->wind_deg[0] + 22.5) / 45;
  if (windAngle > 7) windAngle = 0;

  String wind[] = { "Β", "ΒΑ", "Α", "ΝΑ", "Ν", "ΝΔ", "Δ", "ΒΔ" };  // Greek letters (optional)
  String windIndex[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  ui.drawBmp("/wind/" + windIndex[windAngle] + ".bmp", 101, 86);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(tft.textWidth("NN"));
  tft.drawString(windIndex[windAngle], 125, 65);  // Use wind[windAngle] for Greek letters
  drawSeparator(153);
  tft.setTextDatum(TL_DATUM);  // Reset datum to normal
  tft.setTextPadding(0);       // Reset padding width to none
}

/***************************************************************************************
**                          Draw the 4 forecast columns
***************************************************************************************/
// Each column is one day (8 x 3 hour forecasts), starting tomorrow
void drawForecast() {

  int8_t dayIndex = getNextDayIndex();
  drawForecastDetail(8, 171, dayIndex);
  dayIndex += 8;
  drawForecastDetail(66, 171, dayIndex);
  dayIndex += 8;
  drawForecastDetail(124, 171, dayIndex);
  dayIndex += 8;
  drawForecastDetail(182, 171, dayIndex);
  drawSeparator(171 + 69);
}

/***************************************************************************************
**                          Draw 1 forecast column at x, y
***************************************************************************************/
// Day name, max/min temperature of the day and the icon of the midday (index + 4) forecast
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {

  if (dayIndex >= MAX_DAYS * 8) return;
  String day = shortDOW[weekday(TIMEZONE.toLocal(forecast->dt[dayIndex + 4], &tz1_Code))];
  day.toUpperCase();
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth("WWW"));
  tft.drawString(day, x + 25, y);
  tft.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth("-88   -88"));

  // Find the temperature min and max during the day
  float tmax = -9999;
  float tmin = 9999;
  for (int i = 0; i < 8; i++)
    if (forecast->temp_max[dayIndex + i] > tmax) tmax = forecast->temp_max[dayIndex + i];
  for (int i = 0; i < 8; i++)
    if (forecast->temp_min[dayIndex + i] < tmin) tmin = forecast->temp_min[dayIndex + i];

  String highTemp = String(tmax, 0);
  String lowTemp = String(tmin, 0);
  tft.drawString(highTemp + "/" + lowTemp, x + 25, y + 17);
  String weatherIcon = getMeteoconIcon(forecast->id[dayIndex + 4], false);
  ui.drawBmp("/icon50/" + weatherIcon + ".bmp", x, y + 18);
  tft.setTextPadding(0);  // Reset padding width to none
}

/***************************************************************************************
**              Draw sunrise/sunset, moon phase, cloud cover and humidity
***************************************************************************************/
void drawAstronomy() {

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  // Pad to the widest moon phase name so a shorter one fully overwrites it
  int moonPad = 0;
  for (int i = 0; i < 8; i++) moonPad = max(moonPad, (int)tft.textWidth(" " + moonPhase[i] + " "));
  tft.setTextPadding(moonPad);

  time_t local_time = TIMEZONE.toLocal(forecast->dt[0], &tz1_Code);
  uint16_t y = year(local_time);
  uint8_t m = month(local_time);
  uint8_t d = day(local_time);
  uint8_t h = hour(local_time);
  int ip;
  uint8_t icon = moon_phase(y, m, d, h, &ip);

  tft.drawString(moonPhase[ip], 120, 319);
  ui.drawBmp("/moon/moonphase_L" + String(icon) + ".bmp", 120 - 30, 318 - 16 - 60);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  tft.setTextPadding(0);  // Reset padding width to none
  tft.drawString(sunStr, 40, 270);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth(" 88:88 "));

  String rising = riseStr;
  rising += strTime(forecast->sunrise) + " ";
  int dt = rightOffset(rising, ":");  // Align both times on the colon
  tft.drawString(rising, 20 + dt, 290);

  String setting = setStr;
  setting += strTime(forecast->sunset) + " ";
  dt = rightOffset(setting, ":");
  tft.drawString(setting, 20 + dt, 308);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  tft.drawString(cloudStr, 195, 260);

  String cloudCover = "";
  cloudCover += forecast->clouds_all[0];
  cloudCover += "%";

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth(" 100%"));
  tft.drawString(cloudCover, 210, 277);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BACKGROUND);
  tft.drawString(humidityStr, 195, 300 - 2);

  String humidity = "";
  humidity += forecast->humidity[0];
  humidity += "%";

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth("100%"));
  tft.drawString(humidity, 210, 315);

  tft.setTextPadding(0);  // Reset padding width to none
}

/***************************************************************************************
**                          Get the icon file name from the index number
***************************************************************************************/
const char* getMeteoconIcon(uint16_t id, bool today) {
  if (today && id / 100 == 8 && (forecast->dt[0] < forecast->sunrise || forecast->dt[0] > forecast->sunset)) id += 1000;

  if (id / 100 == 2) return "thunderstorm";
  if (id / 100 == 3) return "drizzle";
  if (id / 100 == 4) return "unknown";
  if (id == 500) return "lightRain";
  else if (id == 511) return "sleet";
  else if (id / 100 == 5) return "rain";
  if (id >= 611 && id <= 616) return "sleet";
  else if (id / 100 == 6) return "snow";
  if (id / 100 == 7) return "fog";
  if (id == 800) return "clear-day";
  if (id == 801) return "partly-cloudy-day";
  if (id == 802) return "cloudy";
  if (id == 803) return "cloudy";
  if (id == 804) return "cloudy";
  if (id == 1800) return "clear-night";
  if (id == 1801) return "partly-cloudy-night";
  if (id == 1802) return "cloudy";
  if (id == 1803) return "cloudy";
  if (id == 1804) return "cloudy";

  return "unknown";
}

/***************************************************************************************
**                          Draw screen section separator line
***************************************************************************************/
// if you don't want separators, comment out the tft-line
void drawSeparator(uint16_t y) {
  tft.drawFastHLine(10, y, 240 - 2 * 10, 0x4228);
}

/***************************************************************************************
**                          Determine place to split a line line
***************************************************************************************/
// determine the "space" split point in a long string
int splitIndex(String text) {
  uint16_t index = 0;
  while ((text.indexOf(' ', index) >= 0) && (index <= text.length() / 2)) {
    index = text.indexOf(' ', index) + 1;
  }
  if (index) index--;
  return index;
}

/***************************************************************************************
**                          Right side offset to a character
***************************************************************************************/
// Calculate coord delta from end of text String to start of sub String contained within that text
// Can be used to vertically right align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int rightOffset(String text, String sub) {
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(index));
}

/***************************************************************************************
**                          Left side offset to a character
***************************************************************************************/
// Calculate coord delta from start of text String to start of sub String contained within that text
// Can be used to vertically left align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int leftOffset(String text, String sub) {
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(0, index));
}

/***************************************************************************************
**                          Draw circle segment
***************************************************************************************/
// Draw a segment of a circle, centred on x,y with defined start_angle and subtended sub_angle
// Angles are defined in a clockwise direction with 0 at top
// Segment has radius r and it is plotted in defined colour
// Can be used for pie charts etc, in this sketch it is used for wind direction
#define DEG2RAD 0.0174532925  // Degrees to Radians conversion factor
#define INC 2                 // Minimum segment subtended angle and plotting angle increment (in degrees)
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour) {
  // Calculate first pair of coordinates for segment start
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);
  uint16_t x1 = sx * r + x;
  uint16_t y1 = sy * r + y;

  // Draw colour blocks every INC degrees
  for (int i = start_angle; i < start_angle + sub_angle; i += INC) {

    // Calculate pair of coordinates for segment end
    int x2 = cos((i + 1 - 90) * DEG2RAD) * r + x;
    int y2 = sin((i + 1 - 90) * DEG2RAD) * r + y;

    tft.fillTriangle(x1, y1, x2, y2, x, y, colour);

    // Copy segment end to segment start for next segment
    x1 = x2;
    y1 = y2;
  }
}

/***************************************************************************************
**                          Get 3 hourly index at start of next day
***************************************************************************************/
int getNextDayIndex(void) {
  int index = 0;
  String today = forecast->dt_txt[0].substring(8, 10);
  for (index = 0; index < 9; index++) {
    if (forecast->dt_txt[index].substring(8, 10) != today) break;
  }
  return index;
}

/***************************************************************************************
**                          Print the weather info to the Serial Monitor
***************************************************************************************/
void printWeather(void) {
#ifdef SERIAL_MESSAGES
  Serial.println("Weather from OpenWeather\n");

  Serial.println("############### Παρών καιρός ###############\n");
  Serial.print("Πόλη                      : ");
  Serial.println(forecast->city_name);
  Serial.print("Ανατολή ηλίου             : ");
  Serial.println(strTime(forecast->sunrise));
  Serial.print("Δύση ηλίου                : ");
  Serial.println(strTime(forecast->sunset));
  Serial.print("Γεωγρ. πλάτος             : ");
  Serial.println(ow.lat, 4);
  Serial.print("Γεωγρ. μήκος              : ");
  Serial.println(ow.lon, 4);
  // We can use the timezone to set the offset eventually...
  Serial.print("Ζώνη ώρας                 : ");
  Serial.println(forecast->timezone);
  /*
  Serial.print("Τρέχουσα θερμοκρασία      : ");
  Serial.println(current->temp);
  Serial.print("Αίσθηση σαν               : ");
  Serial.println(current->feels_like);
  Serial.print("Ατμοσφαιρική πίεση        : ");
  Serial.println(current->pressure);
  Serial.print("Ταχύτητα ανέμου           : ");
  Serial.println(current->wind_speed);
  Serial.print("Διεύθυνση ανέμου          : ");
  Serial.println(current->wind_deg);
  Serial.print("Ταχύτητα ριπών ανέμου     : ");
  Serial.println(current->wind_gust);
  Serial.print("Υγρασία                   : ");
  Serial.println(current->humidity);
  Serial.print("Ορατότητα                 : ");
  Serial.println(current->visibility);
  Serial.print("Δείκτης υπεριώδους ακτινοβολίας : ");
  Serial.println(current->uvi);
*/
  Serial.println();

  if (forecast) {
    Serial.println("###############  Πρόγνωση καιρού  ###############\n");
    for (int i = 0; i < (MAX_DAYS * 8); i++) {
      Serial.print("3ωρες προβλέψεις ");
      if (i < 10) Serial.print(" ");
      Serial.print(i);
      Serial.println();
      Serial.print("dt (ώρα)              : ");
      Serial.println(strTime(forecast->dt[i]));

      Serial.print("Θερμοκρασία           : ");
      Serial.println(forecast->temp[i]);
      Serial.print("Ελάχιστη              : ");
      Serial.println(forecast->temp_min[i]);
      Serial.print("Μέγιστη               : ");
      Serial.println(forecast->temp_max[i]);

      Serial.print("Ατμ. πίεση            : ");
      Serial.println(forecast->pressure[i]);
      Serial.print("Σε επιφάνεια θαλάσσης : ");
      Serial.println(forecast->sea_level[i]);
      Serial.print("Σε επιφάνεια εδάφους  : ");
      Serial.println(forecast->grnd_level[i]);
      Serial.print("Υγρασία               : ");
      Serial.println(forecast->humidity[i]);

      Serial.print("Νέφωση                : ");
      Serial.println(forecast->clouds_all[i]);
      Serial.print("Ταχύτητα ανέμου       : ");
      Serial.println(forecast->wind_speed[i]);
      Serial.print("Διεύθυνση ανέμου      : ");
      Serial.println(forecast->wind_deg[i]);
      Serial.print("Ριπές ανέμου          : ");
      Serial.println(forecast->wind_gust[i]);

      Serial.print("Ορατότητα             : ");
      Serial.println(forecast->visibility[i]);
      Serial.print("pop                   : ");
      Serial.println(forecast->pop[i]);
      Serial.println();

      Serial.print("dt_txt           : ");
      Serial.println(forecast->dt_txt[i]);
      Serial.print("id               : ");
      Serial.println(forecast->id[i]);
      Serial.print("main             : ");
      Serial.println(forecast->main[i]);
      Serial.print("description      : ");
      Serial.println(forecast->description[i]);
      Serial.print("icon             : ");
      Serial.println(forecast->icon[i]);

      Serial.println();
    }
  }
#endif
}
/***************************************************************************************
**             Convert Unix time to a "local time" time string "12:34"
***************************************************************************************/
String strTime(time_t unixTime) {
  time_t local_time = TIMEZONE.toLocal(unixTime, &tz1_Code);

  String localTime = "";

  if (hour(local_time) < 10) localTime += "0";
  localTime += hour(local_time);
  localTime += ":";
  if (minute(local_time) < 10) localTime += "0";
  localTime += minute(local_time);

  return localTime;
}

/***************************************************************************************
**  Convert Unix time to a local date + time string "Oct 16 17:18"
***************************************************************************************/
String strDate(time_t unixTime) {
  time_t local_time = TIMEZONE.toLocal(unixTime, &tz1_Code);

  String localDate = "";

  localDate += monthShortStr(month(local_time));
  localDate += " ";
  localDate += day(local_time);
  localDate += " " + strTime(unixTime);

  return localDate;
}
/***************************************************************************************
**  Optional: read a ThingSpeak channel (tank monitor) and show its 4 values.
**  Not called by default, see loop(). Channel ID and read key are set in secrets.h.
**  Note: it clears the weather area of the screen.
***************************************************************************************/
void retrieveThingSpeak() {

  tft.fillRect(0, 53, 240, 267, TFT_BACKGROUND);
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(ML_DATUM);
  Data1.setTextColor(TFT_WHITE, TFT_BACKGROUND);
  tft.setTextPadding(tft.textWidth("ΘΕΡΜΟΚΡΑΣΙΑ:"));

  float Celsius = ThingSpeak.readFloatField(counterChannelNumber, FieldNumber3, myCounterReadAPIKey);
  statusCode = ThingSpeak.getLastReadStatus();
  if (statusCode == 200)
  {
    tft.setTextDatum(MC_DATUM);
    Data1.setTextColor(TFT_GREEN, TFT_BACKGROUND);
    tft.setCursor(80 + offset_X, 155);
    Data1.print(Celsius, 1);
    tft.print(" oC");
  }

  float hum_ = ThingSpeak.readFloatField(counterChannelNumber, FieldNumber2, myCounterReadAPIKey);
  statusCode = ThingSpeak.getLastReadStatus();
  if (statusCode == 200)
  {
    tft.setCursor(80 + offset_X, 195);
    Data1.print(hum_, 1);
    tft.print(" %");
  }

  float volume = ThingSpeak.readFloatField(counterChannelNumber, FieldNumber1, myCounterReadAPIKey);
  statusCode = ThingSpeak.getLastReadStatus();
  if (volume < 100.0) {
    refuel = true;
  }
  else refuel = false;
  if (statusCode == 200)
  {
    tft.setCursor(80 + offset_X, 235);
    Data1.print(volume, 1);
    tft.print(" lt");
  }

  float height = ThingSpeak.readFloatField(counterChannelNumber, FieldNumber4, myCounterReadAPIKey);
  statusCode = ThingSpeak.getLastReadStatus();
  if (statusCode == 200)
  {
    tft.setCursor(80 + offset_X, 275);
    Data1.print(height, 1);
    tft.print(" cm");
  }
}
/**The MIT License (MIT)
  Copyright (c) 2015 by Daniel Eichhorn
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  See more at http://blog.squix.ch
*/

//  Changes made by Bodmer:

//  Minor changes to text placement and auto-blanking out old text with background colour padding
//  Moon phase text added (not provided by OpenWeather)
//  Forecast text lines are automatically split onto two lines at a central space (some are long!)
//  Time is printed with colons aligned to tidy display
//  Min and max forecast temperatures spaced out
//  New smart splash startup screen and updated progress messages
//  Display does not need to be blanked between updates
//  Icons nudged about slightly to add wind direction + speed
//  Barometric pressure added

//  Adapted to use the OpenWeather library: https://github.com/Bodmer/OpenWeather
//  Moon phase/rise/set (not provided by OpenWeather) replace with  and cloud cover humidity
//  Created and added new 100x100 and 50x50 pixel weather icons, these are in the
//  sketch data folder, press Ctrl+K to view
//  Add moon icons, eliminate all downloads of icons (may lose server!)
//  Adapted to use anti-aliased fonts, tweaked coords
//  Added forecast for 4th day
//  Added cloud cover and humidity in lieu of Moon rise/set
//  Adapted to be compatible with ESP32

//  Changes made by ManosMar:

//  Ported to ESP32-S3 (ESP32 core 3.x, built in LittleFS)
//  Greek and English user interface
//  Web dashboard with setup access point: WiFi, API key, location, language, time zone,
//  rotation and update interval are set from a browser and stored in flash
//  Free Noto Sans fonts instead of Century Gothic / Arial
//  Large clock with seconds, date and year on the top line, centred location line
//  Decimal digit of the temperature, Greek group name under the weather icon
//  No credentials in the source code
//  Fixes: temperature rounding, NTP request rate, date shown as 1970 after boot
