#pragma once
// Original ESP32-2432S028R. USB is CH340 serial, not USB host.
#define USER_SETUP_LOADED
#define ILI9341_2_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21
#define TFT_BACKLIGHT_ON HIGH
#define USE_HSPI_PORT
#define SPI_FREQUENCY 27000000
#define SPI_READ_FREQUENCY 16000000
#define LOAD_GLCD
#define FM1_TOUCH_SCK 25
#define FM1_TOUCH_MISO 39
#define FM1_TOUCH_MOSI 32
#define FM1_TOUCH_CS 33
#define FM1_TOUCH_IRQ 36
#define FM1_RGB_RED 4
#define FM1_RGB_GREEN 16
#define FM1_RGB_BLUE 17
