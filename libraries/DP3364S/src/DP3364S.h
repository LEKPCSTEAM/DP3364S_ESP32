#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "DP3364S_Stream.h"

struct gdma_channel_t;

struct DP3364S_Pins {
  int8_t r1, g1, b1, r2, g2, b2;
  int8_t a, b, c, d, e; // e = -1 if the panel has no E line
  int8_t lat, oe, clk;  // oe carries the DP3364S ROW signal
};

// S-PWM HUB75 panels: CLK feeds the grey-clock PLL, so it never stops.
class DP3364S : public Adafruit_GFX {
public:
  // width: 64 per chained panel; height: 16, 32 or 64.
  DP3364S(uint16_t width, uint16_t height);
  ~DP3364S();

  bool begin(const DP3364S_Pins &pins, uint32_t clockHz = 8000000,
             bool clockInvert = false);

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
  void drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b);

  // Sends the drawing to the panel; call after each frame.
  void show();
  void clear();
  // 0..255; takes effect on the next show().
  void setBrightness(uint8_t level);

  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }

  const char *error() const { return _error; }
  uint32_t restarts() const { return _restarts; }

private:
  void startOutput();
  void keepRunning();
  void put(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b);

  dp3364s::Layout _layout = {};
  uint16_t _lut[256] = {};
  uint8_t *_pixels = nullptr;   // RGB888, unrotated
  uint16_t *_frame = nullptr;   // internal RAM: flash writes stall PSRAM DMA
  uint16_t *_registers = nullptr;
  void *_descriptors = nullptr;
  gdma_channel_t *_dma = nullptr;
  uint32_t _restarts = 0;
  const char *_error = "";
};
