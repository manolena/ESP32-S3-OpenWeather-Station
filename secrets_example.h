// OPTIONAL first boot defaults.
//
// Normally you do not need this file: on first boot the station starts a setup
// access point and you enter WiFi and API key in the web dashboard.
//
// If you prefer to build these values in, copy this file to "secrets.h" in the same
// folder and fill it in. They are used only until settings are saved in the dashboard
// (or after a factory reset). secrets.h is in .gitignore and never uploaded to GitHub.

#define WIFI_SSID      "your-wifi-name"
#define WIFI_PASSWORD  "your-wifi-password"

// OpenWeather API key, free sign up at https://openweathermap.org/
#define OWM_API_KEY    "your-openweather-api-key"

// Optional ThingSpeak channel, only used if retrieveThingSpeak() is enabled in loop()
#define TS_CHANNEL_ID  0                    // Channel ID
#define TS_READ_KEY    "your-read-api-key"  // Read API key
