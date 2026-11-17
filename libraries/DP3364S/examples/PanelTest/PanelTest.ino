// DP3364S S-PWM P5 64x32 panel test for ESP32-S3 (PX-Board-x3 pinout).
// Board: ESP32S3 Dev Module, USB CDC On Boot: Disabled, Serial 115200.
// Press BOOT (IO0) to skip to the next test.

#include <DP3364S.h>

// ---- settings ----
#define PANEL_WIDTH 64      // 64 x number of chained panels
#define PANEL_HEIGHT 32     // 16, 32 or 64
#define CLOCK_HZ 8000000    // panels went dark at 16 MHz in kkwestt's tests
#define CLOCK_INVERT false  // try true if the image is garbled
#define BRIGHTNESS 128      // 0..255; full white at 255 draws ~4 A per panel
#define TEST_MS 4000
#define BUTTON_PIN 0

// R1 G1 B1 R2 G2 B2, A B C D E, LAT OE(ROW) CLK; E = -1 for 1/16 scan
const DP3364S_Pins PINS = {45, 39, 48, 47, 40, 21, 14, 41, 13, 42, -1, 38, 2, 1};

DP3364S panel(PANEL_WIDTH, PANEL_HEIGHT);

uint16_t hsv(uint16_t hue, uint8_t val) { // hue 0..1535
  const uint8_t h = hue >> 8, f = hue & 0xFF;
  uint8_t r, g, b;
  switch (h % 6) {
  case 0: r = 255, g = f, b = 0; break;
  case 1: r = 255 - f, g = 255, b = 0; break;
  case 2: r = 0, g = 255, b = f; break;
  case 3: r = 0, g = 255 - f, b = 255; break;
  case 4: r = f, g = 0, b = 255; break;
  default: r = 255, g = 0, b = 255 - f; break;
  }
  return DP3364S::color565(r * val / 255, g * val / 255, b * val / 255);
}

void solid(uint32_t, uint16_t c) { panel.fillScreen(c); }

void bars(uint32_t) {
  const uint16_t c[8] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000};
  for (int i = 0; i < 8; ++i) {
    panel.fillRect(i * PANEL_WIDTH / 8, 0, PANEL_WIDTH / 8, PANEL_HEIGHT, c[i]);
  }
}

void gradient(uint32_t) {
  const int band = PANEL_HEIGHT / 4;
  for (int x = 0; x < PANEL_WIDTH; ++x) {
    const uint8_t v = x * 255 / (PANEL_WIDTH - 1);
    for (int y = 0; y < band; ++y) {
      panel.drawPixelRGB888(x, y, v, 0, 0);
      panel.drawPixelRGB888(x, y + band, 0, v, 0);
      panel.drawPixelRGB888(x, y + 2 * band, 0, 0, v);
      panel.drawPixelRGB888(x, y + 3 * band, v, v, v);
    }
  }
}

void grid(uint32_t) {
  panel.fillScreen(0);
  for (int x = 0; x < PANEL_WIDTH; x += 8) panel.drawFastVLine(x, 0, PANEL_HEIGHT, 0x07E0);
  for (int y = 0; y < PANEL_HEIGHT; y += 8) panel.drawFastHLine(0, y, PANEL_WIDTH, 0x07E0);
  panel.drawRect(0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0xFFFF);
}

void corners(uint32_t) {
  panel.fillScreen(0);
  panel.fillRect(0, 0, 3, 3, 0xF800);                                // red: top-left
  panel.fillRect(PANEL_WIDTH - 3, 0, 3, 3, 0x07E0);                  // green: top-right
  panel.fillRect(0, PANEL_HEIGHT - 3, 3, 3, 0x001F);                 // blue: bottom-left
  panel.fillRect(PANEL_WIDTH - 3, PANEL_HEIGHT - 3, 3, 3, 0xFFFF);   // white: bottom-right
  panel.setTextColor(0xFFE0);
  panel.setCursor(PANEL_WIDTH / 2 - 21, PANEL_HEIGHT / 2 - 8);
  panel.print("DP3364S");
  panel.setTextColor(0x07FF);
  panel.setCursor(PANEL_WIDTH / 2 - 15, PANEL_HEIGHT / 2 + 1);
  panel.print(PANEL_WIDTH);
  panel.print('x');
  panel.print(PANEL_HEIGHT);
}

void rowScan(uint32_t ms) {
  panel.fillScreen(0);
  panel.drawFastHLine(0, (ms / 100) % PANEL_HEIGHT, PANEL_WIDTH, 0x07FF);
}

void colScan(uint32_t ms) {
  panel.fillScreen(0);
  panel.drawFastVLine((ms / 50) % PANEL_WIDTH, 0, PANEL_HEIGHT, 0xFFE0);
}

void plasma(uint32_t ms) {
  const float t = ms / 1000.0f;
  for (int y = 0; y < PANEL_HEIGHT; ++y) {
    for (int x = 0; x < PANEL_WIDTH; ++x) {
      const float v = sinf(x / 7.0f + t) + sinf(y / 5.0f - 2 * t) +
                      sinf((x + y) / 9.0f + t) +
                      sinf(sqrtf((x - 32) * (x - 32) + (y - 16) * (y - 16)) / 4.0f - 2 * t);
      panel.drawPixel(x, y, hsv((uint16_t)((v + 4) * 192 + ms / 4) % 1536, 255));
    }
  }
}

void scrollText(uint32_t ms) {
  static const char msg[] = "Hello DP3364S  S-PWM  ESP32-S3";
  const int w = (sizeof(msg) - 1) * 6;
  panel.fillScreen(0);
  panel.setTextWrap(false);
  const int x = PANEL_WIDTH - (int)(ms / 30) % (w + PANEL_WIDTH);
  for (int i = 0; msg[i]; ++i) {
    panel.setTextColor(hsv((i * 96 + ms / 2) % 1536, 255));
    panel.setCursor(x + i * 6, PANEL_HEIGHT / 2 - 4);
    panel.print(msg[i]);
  }
}

struct Test {
  const char *name;
  void (*draw)(uint32_t);
};

const Test TESTS[] = {
    {"red", [](uint32_t ms) { solid(ms, 0xF800); }},
    {"green", [](uint32_t ms) { solid(ms, 0x07E0); }},
    {"blue", [](uint32_t ms) { solid(ms, 0x001F); }},
    {"white", [](uint32_t ms) { solid(ms, 0xFFFF); }},
    {"colour bars", bars},
    {"gradient (R, G, B, W)", gradient},
    {"grid", grid},
    {"corners: R=TL G=TR B=BL W=BR", corners},
    {"row scan (top to bottom)", rowScan},
    {"column scan (left to right)", colScan},
    {"plasma", plasma},
    {"scrolling text", scrollText},
};
const int TEST_COUNT = sizeof(TESTS) / sizeof(TESTS[0]);

int current = -1;
uint32_t startedMs = 0;

void nextTest() {
  current = (current + 1) % TEST_COUNT;
  startedMs = millis();
  Serial.printf("[%d/%d] %s\n", current + 1, TEST_COUNT, TESTS[current].name);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(300);

  if (!panel.begin(PINS, CLOCK_HZ, CLOCK_INVERT)) {
    Serial.printf("DP3364S init failed: %s\n", panel.error());
    while (true) delay(1000);
  }
  panel.setBrightness(BRIGHTNESS);
  Serial.printf("DP3364S %dx%d running at %lu Hz\n", PANEL_WIDTH, PANEL_HEIGHT,
                (unsigned long)CLOCK_HZ);
  nextTest();
}

void loop() {
  static bool wasPressed = false;
  const bool pressed = digitalRead(BUTTON_PIN) == LOW;
  if ((pressed && !wasPressed) || millis() - startedMs >= TEST_MS) {
    nextTest();
  }
  wasPressed = pressed;

  TESTS[current].draw(millis() - startedMs);
  panel.show();
  delay(16);
}
