// Setup for the ESP32-S3 + ILI9341 240x320 OpenWeather station
// (sketch ESP32_ILI9341_openweathermap_16)
// Selected in User_Setup_Select.h

#define USER_SETUP_ID 9341
#define USER_SETUP_INFO "ESP32S3_ILI9341_Weather"

#define DISABLE_ALL_LIBRARY_WARNINGS

#define ILI9341_DRIVER

#define TFT_MISO -1   // Not connected (no screen read-back)
#define TFT_MOSI 3
#define TFT_SCLK 4
#define TFT_CS   1    // Chip select control pin
#define TFT_DC   10   // Data Command control pin
#define TFT_RST  0    // Reset pin (GPIO0 is the S3 BOOT strapping pin)
// Backlight assumed wired to 3.3V, define TFT_BL if it is on a GPIO

#define USE_HSPI_PORT // Avoids a boot crash in TFT_eSPI::begin() on some ESP32-S3 core versions

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2  // Font 2. Small 16 pixel high font
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font
#define LOAD_FONT6  // Font 6. Large 48 pixel font, only 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, only 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel font, only 1234567890:-.
#define LOAD_GFXFF  // FreeFonts
#define SMOOTH_FONT // Needed for the .vlw anti-aliased fonts in LittleFS

#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
