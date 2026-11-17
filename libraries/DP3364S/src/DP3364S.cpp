#include "DP3364S.h"

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_private/gdma.h>
#include <esp_rom_gpio.h>
#include <esp_rom_sys.h>
#include <hal/dma_types.h>
#include <soc/gpio_sig_map.h>
#include <soc/lcd_cam_struct.h>
#include <string.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include <soc/system_struct.h>
#else
#include <driver/periph_ctrl.h>
#endif

namespace {

constexpr size_t kChunkBytes = 4092; // descriptor max 4095, kept 4-aligned
constexpr uint8_t kFramesPerInit = 16; // registers rewritten ~20x/s at 64x32
constexpr uint32_t kLcdSourceHz = 160000000;

size_t chunksFor(size_t bytes) { return (bytes + kChunkBytes - 1) / kChunkBytes; }

dma_descriptor_t *chain(dma_descriptor_t *d, void *buf, size_t bytes) {
  uint8_t *p = static_cast<uint8_t *>(buf);
  while (bytes) {
    const size_t n = bytes > kChunkBytes ? kChunkBytes : bytes;
    d->dw0.size = n;
    d->dw0.length = n;
    d->dw0.suc_eof = 0;
    d->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
    d->buffer = p;
    d->next = d + 1;
    ++d;
    p += n;
    bytes -= n;
  }
  return d;
}

void routePin(int pin, uint32_t signal, bool invert) {
  if (pin < 0) {
    return;
  }
  esp_rom_gpio_pad_select_gpio(pin);
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
  gpio_set_drive_capability(static_cast<gpio_num_t>(pin), GPIO_DRIVE_CAP_3);
  esp_rom_gpio_connect_out_signal(pin, signal, invert, false);
}

void *dmaCalloc(size_t bytes) {
  return heap_caps_calloc(1, bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
}

void enableLcdCam() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  // Same writes as lcd_ll.h, whose RCC lock macros differ per IDF release
  static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&lock);
  SYSTEM.perip_clk_en1.lcd_cam_clk_en = 1;
  SYSTEM.perip_rst_en1.lcd_cam_rst = 1;
  SYSTEM.perip_rst_en1.lcd_cam_rst = 0;
  portEXIT_CRITICAL(&lock);
#else
  periph_module_enable(PERIPH_LCD_CAM_MODULE);
  periph_module_reset(PERIPH_LCD_CAM_MODULE);
#endif
}

} // namespace

DP3364S::DP3364S(uint16_t width, uint16_t height) : Adafruit_GFX(width, height) {}

DP3364S::~DP3364S() {
  if (_dma) {
    LCD_CAM.lcd_user.lcd_start = 0;
    gdma_stop(_dma);
    gdma_disconnect(_dma);
    gdma_del_channel(_dma);
  }
  free(_descriptors);
  free(_frame);
  free(_registers);
  free(_pixels);
}

bool DP3364S::begin(const DP3364S_Pins &pins, uint32_t clockHz,
                    bool clockInvert) {
  if (WIDTH % 16 != 0 || !(HEIGHT == 16 || HEIGHT == 32 || HEIGHT == 64)) {
    _error = "width must be a multiple of 16, height 16/32/64";
    return false;
  }
  _layout = dp3364s::layoutFor(WIDTH, HEIGHT);
  const size_t frameBytes = _layout.frameWords() * sizeof(uint16_t);
  const size_t regBytes = _layout.registerWords() * sizeof(uint16_t);
  const size_t pixelBytes = (size_t)WIDTH * HEIGHT * 3;

  _registers = static_cast<uint16_t *>(dmaCalloc(regBytes));
  _frame = static_cast<uint16_t *>(dmaCalloc(frameBytes));
  _pixels = static_cast<uint8_t *>(
      heap_caps_calloc(1, pixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!_pixels) {
    _pixels = static_cast<uint8_t *>(calloc(1, pixelBytes));
  }
  const size_t descCount =
      chunksFor(regBytes) + chunksFor(frameBytes) * kFramesPerInit;
  auto *desc = static_cast<dma_descriptor_t *>(
      dmaCalloc(descCount * sizeof(dma_descriptor_t)));
  _descriptors = desc;
  if (!_registers || !_frame || !_pixels || !desc) {
    _error = "not enough internal RAM for the bitstream";
    return false;
  }

  dp3364s::buildRegisters(_registers, _layout);
  dp3364s::buildFrame(_frame, _layout);
  dp3364s::buildGammaLut(_lut, 255);

  dma_descriptor_t *next = chain(desc, _registers, regBytes);
  for (uint8_t f = 0; f < kFramesPerInit; ++f) {
    next = chain(next, _frame, frameBytes);
  }
  desc[descCount - 1].next = desc;
  desc[descCount - 1].dw0.suc_eof = 1;

  enableLcdCam();
  LCD_CAM.lcd_user.lcd_reset = 1;
  esp_rom_delay_us(100);

  uint32_t div = kLcdSourceHz / (clockHz ? clockHz : 1);
  div = div < 2 ? 2 : (div > 255 ? 255 : div);
  LCD_CAM.lcd_clock.clk_en = 1;
  LCD_CAM.lcd_clock.lcd_clk_sel = 3; // PLL_F160M
  LCD_CAM.lcd_clock.lcd_clkm_div_num = div;
  LCD_CAM.lcd_clock.lcd_clkm_div_a = 1; // 0 stops the clock entirely
  LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;
  LCD_CAM.lcd_clock.lcd_clkcnt_n = 1; // must not be 0
  LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 1;
  LCD_CAM.lcd_clock.lcd_ck_idle_edge = 0;
  LCD_CAM.lcd_clock.lcd_ck_out_edge = 0; // data set while low, latched on rise
  LCD_CAM.lcd_ctrl.lcd_rgb_mode_en = 0;  // i8080
  LCD_CAM.lcd_rgb_yuv.lcd_conv_bypass = 0;
  LCD_CAM.lcd_misc.lcd_next_frame_en = 0;
  LCD_CAM.lcd_misc.lcd_bk_en = 1;
  LCD_CAM.lcd_misc.lcd_vfk_cyclelen = 0;
  LCD_CAM.lcd_misc.lcd_vbk_cyclelen = 0;
  LCD_CAM.lcd_data_dout_mode.val = 0;
  LCD_CAM.lcd_user.lcd_always_out_en = 1;
  LCD_CAM.lcd_user.lcd_8bits_order = 0;
  LCD_CAM.lcd_user.lcd_bit_order = 0;
  LCD_CAM.lcd_user.lcd_byte_order = 0;
  LCD_CAM.lcd_user.lcd_2byte_en = 1;
  LCD_CAM.lcd_user.lcd_cmd = 0;
  LCD_CAM.lcd_user.lcd_dummy = 0;
  LCD_CAM.lcd_user.lcd_dummy_cyclelen = 0;
  LCD_CAM.lcd_user.lcd_dout = 1;
  LCD_CAM.lcd_user.lcd_update = 1;

  // Order fixes the bit each signal takes in the stream words
  const int bus[dp3364s::kBusLines] = {pins.r1, pins.g1, pins.b1, pins.r2,
                                       pins.g2, pins.b2, pins.a,  pins.b,
                                       pins.c,  pins.d,  pins.e,  pins.lat,
                                       pins.oe};
  for (uint8_t i = 0; i < dp3364s::kBusLines; ++i) {
    routePin(bus[i], LCD_DATA_OUT0_IDX + i, false);
  }
  routePin(pins.clk, LCD_PCLK_IDX, clockInvert);

  gdma_channel_alloc_config_t alloc = {};
  alloc.direction = GDMA_CHANNEL_DIRECTION_TX;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  const esp_err_t err = gdma_new_ahb_channel(&alloc, &_dma);
#else
  const esp_err_t err = gdma_new_channel(&alloc, &_dma);
#endif
  if (err != ESP_OK) {
    _dma = nullptr;
    _error = "no free GDMA channel";
    return false;
  }
  gdma_connect(_dma, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));
  gdma_strategy_config_t strategy = {};
  strategy.owner_check = false;
  strategy.auto_update_desc = false;
  gdma_apply_strategy(_dma, &strategy);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
  gdma_transfer_config_t transfer = {};
  transfer.max_data_burst_size = 4; // 4-byte alignment, as core 2.x used
  transfer.access_ext_mem = false;
  gdma_config_transfer(_dma, &transfer);
#else
  gdma_transfer_ability_t ability = {};
  ability.sram_trans_align = 4;
  ability.psram_trans_align = 64;
  gdma_set_transfer_ability(_dma, &ability);
#endif

  startOutput();
  return true;
}

void DP3364S::startOutput() {
  LCD_CAM.lcd_user.lcd_start = 0;
  gdma_stop(_dma);
  esp_rom_delay_us(100);
  gdma_reset(_dma);
  esp_rom_delay_us(1000);
  LCD_CAM.lcd_user.lcd_dout = 1;
  LCD_CAM.lcd_user.lcd_update = 1;
  LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
  gdma_start(_dma, reinterpret_cast<intptr_t>(_descriptors));
  esp_rom_delay_us(100);
  LCD_CAM.lcd_user.lcd_start = 1;
}

// After a cold power-up LCD_CAM sometimes drops lcd_start by itself.
void DP3364S::keepRunning() {
  if (!_dma || LCD_CAM.lcd_user.lcd_start) {
    return;
  }
  startOutput();
  ++_restarts;
}

void DP3364S::put(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
  if (!_pixels || x < 0 || y < 0 || x >= _width || y >= _height) {
    return;
  }
  int16_t t;
  switch (rotation) {
  case 1:
    t = x, x = WIDTH - 1 - y, y = t;
    break;
  case 2:
    x = WIDTH - 1 - x, y = HEIGHT - 1 - y;
    break;
  case 3:
    t = x, x = y, y = HEIGHT - 1 - t;
    break;
  default:
    break;
  }
  uint8_t *p = _pixels + ((size_t)y * WIDTH + x) * 3;
  p[0] = r;
  p[1] = g;
  p[2] = b;
}

void DP3364S::drawPixel(int16_t x, int16_t y, uint16_t c) {
  const uint8_t r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
  put(x, y, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

void DP3364S::drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g,
                              uint8_t b) {
  put(x, y, r, g, b);
}

void DP3364S::fillScreen(uint16_t c) {
  if (!_pixels) {
    return;
  }
  const uint8_t r5 = c >> 11, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
  const uint8_t rgb[3] = {(uint8_t)((r5 << 3) | (r5 >> 2)),
                          (uint8_t)((g6 << 2) | (g6 >> 4)),
                          (uint8_t)((b5 << 3) | (b5 >> 2))};
  for (size_t i = 0; i < (size_t)WIDTH * HEIGHT; ++i) {
    memcpy(_pixels + i * 3, rgb, 3);
  }
}

void DP3364S::show() {
  if (!_frame) {
    return;
  }
  dp3364s::encodeFrame(_frame, _layout, _lut, _pixels);
  keepRunning();
}

void DP3364S::clear() {
  fillScreen(0);
  show();
}

void DP3364S::setBrightness(uint8_t level) { dp3364s::buildGammaLut(_lut, level); }
