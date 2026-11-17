#pragma once

// Bitstream layout ported from kkwestt/RaspberryPi-HUB75-DP3364S-Matrix (MIT).

#include <stddef.h>
#include <stdint.h>

namespace dp3364s {

// One uint16 per CLK period; bit n drives LCD data line Dn.
constexpr uint16_t kRgbMask = 0x003F; // R1 G1 B1 R2 G2 B2 on D0-D5
constexpr uint8_t kAddrShift = 6;     // A-E on D6-D10
constexpr uint16_t kLat = 1u << 11;
constexpr uint16_t kRow = 1u << 12; // the HUB75 OE line carries ROW
constexpr uint8_t kBusLines = 13;

constexpr uint8_t kRegisterCount = 13;

struct Layout {
  uint16_t width;   // px across the whole chain
  uint16_t height;  // px, top half on R1 G1 B1
  uint8_t scanRows; // height / 2

  // 16 bits for each of the width / 16 chips on a data line
  uint32_t blockClocks() const { return width; }
  uint32_t latches() const { return (uint32_t)scanRows * 16; }
  uint32_t dataClocks() const { return latches() * blockClocks(); }
  uint32_t scanClocks() const;
  size_t frameWords() const;
  size_t registerWords() const;
  size_t dataIndex(uint32_t latch) const;
};

Layout layoutFor(uint16_t width, uint16_t height);
size_t streamBytes(uint16_t width, uint16_t height);

// Control bits only; pixel bits stay zero until encodeFrame().
void buildFrame(uint16_t *out, const Layout &l);
void buildRegisters(uint16_t *out, const Layout &l);
// level 0..255 scales the CIE 1931 curve linearly.
void buildGammaLut(uint16_t lut[256], uint8_t level);
// rgb: width * height * 3 bytes, row-major.
void encodeFrame(uint16_t *frame, const Layout &l, const uint16_t lut[256],
                 const uint8_t *rgb);

} // namespace dp3364s
