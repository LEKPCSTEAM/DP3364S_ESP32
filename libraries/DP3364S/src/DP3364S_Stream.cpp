#include "DP3364S_Stream.h"

#include <math.h>

namespace dp3364s {

namespace {

constexpr uint8_t kVsyncClocks = 3;
constexpr uint8_t kVsyncIdle = 17;
constexpr uint8_t kPreActClocks = 14;
constexpr uint8_t kWrCfgClocks = 5;
constexpr uint32_t kRowPeriod = 128; // clocks per ROW pulse, as captured
constexpr uint32_t kRowPulse = 4;    // ROW high clocks (W4)
constexpr uint32_t kTailRows = 64;   // ROW pulses after the last data latch
constexpr size_t kFrameAlign = 32;   // words

// address << 8 | value; captured from a working card, REG02 set per layout
constexpr uint16_t kRegisters[kRegisterCount] = {
    0x1100, 0x021F, 0x033F, 0x043F, 0x0504, 0x0642, 0x0700,
    0x08BF, 0x0960, 0x0ABE, 0x0B8B, 0x0C08, 0x0D12,
};

size_t registerBlockWords(const Layout &l) {
  return kVsyncClocks + kVsyncIdle + kPreActClocks + 1 + l.blockClocks() + 1;
}

inline uint16_t addrBits(uint32_t row) {
  return (uint16_t)((row & 0x1F) << kAddrShift);
}

size_t writeRegister(uint16_t *out, const Layout &l, uint16_t reg) {
  size_t n = 0;
  for (uint8_t i = 0; i < kVsyncClocks; ++i) {
    out[n++] = kLat;
  }
  for (uint8_t i = 0; i < kVsyncIdle; ++i) {
    out[n++] = 0;
  }
  for (uint8_t i = 0; i < kPreActClocks; ++i) {
    out[n++] = kLat;
  }
  out[n++] = 0;
  const uint32_t clocks = l.blockClocks();
  // Every chip in the chain gets the same word, MSB first
  for (uint32_t i = 0; i < clocks; ++i) {
    const uint8_t bit = 15 - (i % 16);
    uint16_t v = ((reg >> bit) & 1) ? kRgbMask : 0;
    if (i >= clocks - kWrCfgClocks) {
      v |= kLat;
    }
    out[n++] = v;
  }
  out[n++] = (reg & 1) ? kRgbMask : 0;
  return n;
}

} // namespace

uint32_t Layout::scanClocks() const {
  return dataClocks() + kTailRows * kRowPeriod;
}

size_t Layout::frameWords() const {
  const size_t raw = kVsyncClocks + kVsyncIdle + scanClocks() + latches();
  return (raw + kFrameAlign - 1) / kFrameAlign * kFrameAlign;
}

size_t Layout::registerWords() const {
  const size_t words = registerBlockWords(*this) * kRegisterCount;
  return words + (words & 1); // DMA chunks need 4-byte multiples
}

size_t Layout::dataIndex(uint32_t latch) const {
  return kVsyncClocks + kVsyncIdle + (size_t)latch * (blockClocks() + 1);
}

Layout layoutFor(uint16_t width, uint16_t height) {
  Layout l;
  l.width = width;
  l.height = height;
  l.scanRows = (uint8_t)(height / 2);
  return l;
}

size_t streamBytes(uint16_t width, uint16_t height) {
  const Layout l = layoutFor(width, height);
  return (l.frameWords() + l.registerWords()) * sizeof(uint16_t);
}

void buildFrame(uint16_t *out, const Layout &l) {
  size_t n = 0;
  uint32_t row = l.scanRows - 1; // first ROW pulse moves to row 0
  for (uint8_t i = 0; i < kVsyncClocks; ++i) {
    out[n++] = kLat | addrBits(row);
  }
  for (uint8_t i = 0; i < kVsyncIdle; ++i) {
    out[n++] = addrBits(row);
  }
  const uint32_t scan = l.scanClocks();
  const uint32_t data = l.dataClocks();
  const uint32_t block = l.blockClocks();
  for (uint32_t p = 0; p < scan; ++p) {
    // DATA_LATCH clock; the scan counter does not advance on it
    if (p > 0 && p <= data && p % block == 0) {
      out[n++] = kLat | addrBits(row);
    }
    if (p % kRowPeriod == 0) {
      row = (row + 1) % l.scanRows;
    }
    uint16_t v = addrBits(row);
    if (p % kRowPeriod < kRowPulse) {
      v |= kRow;
    }
    out[n++] = v;
  }
  const size_t words = l.frameWords();
  while (n < words) {
    out[n++] = addrBits(row);
  }
}

void buildRegisters(uint16_t *out, const Layout &l) {
  size_t n = 0;
  for (uint8_t r = 0; r < kRegisterCount; ++r) {
    uint16_t reg = kRegisters[r];
    if ((reg >> 8) == 0x02) {
      reg = 0x0200 | (uint8_t)(l.scanRows - 1);
    }
    n += writeRegister(out + n, l, reg);
  }
  if (n < l.registerWords()) {
    out[n] = 0;
  }
}

void buildGammaLut(uint16_t lut[256], uint8_t level) {
  // 11-bit PWM, MSB-aligned to the 12 bits REG03 = REG04 = 0x3F show
  const float scale = 2047.0f * level / 255.0f;
  for (int c = 0; c < 256; ++c) {
    const float l = c * 100.0f / 255.0f; // CIE L*
    const float y = l <= 8.0f ? l / 902.3f : powf((l + 16.0f) / 116.0f, 3.0f);
    lut[c] = (uint16_t)(lroundf(scale * y) << 1);
  }
}

void encodeFrame(uint16_t *frame, const Layout &l, const uint16_t lut[256],
                 const uint8_t *rgb) {
  const uint32_t slots = l.width / 16;
  const size_t stride = (size_t)l.width * 3;
  uint16_t w[6];
  for (uint32_t line = 0; line < l.scanRows; ++line) {
    const uint8_t *upper = rgb + line * stride;
    const uint8_t *lower = rgb + (line + l.scanRows) * stride;
    for (uint32_t ch = 0; ch < 16; ++ch) {
      uint16_t *dst = frame + l.dataIndex(line * 16 + ch);
      for (uint32_t s = 0; s < slots; ++s) {
        const size_t x3 = (s * 16 + ch) * 3;
        for (uint8_t c = 0; c < 3; ++c) {
          w[c] = lut[upper[x3 + c]];
          w[3 + c] = lut[lower[x3 + c]];
        }
        for (int b = 15; b >= 0; --b) {
          const uint16_t bits =
              ((w[0] >> b) & 1) | (((w[1] >> b) & 1) << 1) |
              (((w[2] >> b) & 1) << 2) | (((w[3] >> b) & 1) << 3) |
              (((w[4] >> b) & 1) << 4) | (((w[5] >> b) & 1) << 5);
          *dst = (uint16_t)((*dst & ~kRgbMask) | bits);
          ++dst;
        }
      }
    }
  }
}

} // namespace dp3364s
