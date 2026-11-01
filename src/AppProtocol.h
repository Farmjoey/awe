#pragma once
// Wire protocol between the NOVA RIDE phone app (Web Bluetooth) and the board.
// Pure C++11 so it is unit-tested on a PC. See docs/APP_PROTOCOL.md.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace app {

static const uint8_t kProto = 1;

// CTRL characteristic (write): first byte = opcode.
static const uint8_t OP_INFO = 0x01;
static const uint8_t OP_OTA_BEGIN = 0x10;   // u32 size, u32 crc32 (little endian)
static const uint8_t OP_OTA_END = 0x11;
static const uint8_t OP_OTA_ABORT = 0x12;
static const uint8_t OP_THEME = 0x20;       // u8 index
static const uint8_t OP_BOOT_STYLE = 0x21;  // u8 style
static const uint8_t OP_BOOT_TEXT = 0x22;   // ASCII
static const uint8_t OP_IMG_BEGIN = 0x23;
static const uint8_t OP_IMG_CHUNK = 0x24;   // u16 offset, bytes
static const uint8_t OP_IMG_END = 0x25;
static const uint8_t OP_PREVIEW = 0x26;
static const uint8_t OP_LAYOUT_BEGIN = 0x40;  // custom dashboard (widgets)
static const uint8_t OP_LAYOUT_CHUNK = 0x41;  // u16 offset, bytes
static const uint8_t OP_LAYOUT_END = 0x42;    // validates, stores and turns the layout on
static const uint8_t OP_LAYOUT_USE = 0x43;    // u8: 1 custom dashboard, 0 standard dashboard
static const uint8_t OP_BACKLIGHT = 0x51;  // u8 10..100
static const uint8_t OP_ARCMAX = 0x52;     // u8: 0 = auto scale, else full-scale of the speed arc = value * 5 km/h (1..24)
static const uint8_t OP_ACCENT = 0x53;     // u8 r, g, b: accent colour of the CUSTOM theme
static const uint8_t ST_TELEM = 0x40;      // live data pushed to the phone about twice a second
static const uint8_t OP_STARTCAP = 0x50;   // u8: 1 = cap at 20 km/h at every power-on, 0 = off (phone app only)
static const uint8_t OP_MUSIC = 0x30;       // u8: 0 previous, 1 play/pause, 2 next

// STAT characteristic (notify): first byte = frame type.
static const uint8_t ST_INFO = 0x01;
static const uint8_t ST_OTA = 0x10;
static const uint8_t ST_ACK = 0x30;

enum OtaState { OTA_IDLE = 0, OTA_READY = 1, OTA_RECV = 2, OTA_VERIFY = 3, OTA_DONE = 4, OTA_ERROR = 5 };
enum OtaErr {
  E_NONE = 0, E_CLOSED = 1, E_RIDING = 2, E_SIZE = 3, E_HEADER = 4, E_CRC = 5,
  E_WRITE = 6, E_BEGIN = 7, E_OVERRUN = 8, E_BUSY = 9, E_TIMEOUT = 10
};

static const uint32_t kMinAppBytes = 65536;
static const uint32_t kMaxAppBytes = 0x140000;  // default 4 MB partition table: app slot size
static const size_t kBootImgBytes = 1024;       // 128 x 64, 1 bit per pixel, row-major, MSB first
static const int kBootImgW = 128, kBootImgH = 64;
static const int kBootStyles = 4;
static const size_t kBootTextMax = 15;

// Standard CRC-32 (zlib/IEEE). Chain calls by passing the previous result; start with 0.
inline uint32_t crc32Update(uint32_t crc, const uint8_t* d, size_t n) {
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return ~crc;
}

inline uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// ESP32 app image: magic 0xE9 and chip id 0x0000 (ESP32) at offset 12.
inline bool isEspImageHeader(const uint8_t* b, size_t n) { return n >= 14 && b[0] == 0xE9 && b[12] == 0x00 && b[13] == 0x00; }

inline size_t buildOtaStat(uint8_t* out, uint8_t state, uint8_t err, uint32_t recv, uint32_t total) {
  out[0] = ST_OTA; out[1] = state; out[2] = err;
  for (int i = 0; i < 4; ++i) { out[3 + i] = (uint8_t)(recv >> (8 * i)); out[7 + i] = (uint8_t)(total >> (8 * i)); }
  return 11;
}
inline size_t buildAck(uint8_t* out, uint8_t op, uint8_t result) { out[0] = ST_ACK; out[1] = op; out[2] = result; return 3; }

// Keeps printable ASCII only, so the font can always draw it.
inline void sanitizeBootText(char* dst, size_t cap, const uint8_t* src, size_t n) {
  size_t k = 0;
  for (size_t i = 0; i < n && k + 1 < cap && k < kBootTextMax; ++i)
    if (src[i] >= 0x20 && src[i] < 0x7F) dst[k++] = (char)src[i];
  dst[k] = 0;
}

inline bool bootPixel(const uint8_t* img, int x, int y) {
  if (x < 0 || y < 0 || x >= kBootImgW || y >= kBootImgH) return false;
  const int bit = y * kBootImgW + x;
  return (img[bit >> 3] >> (7 - (bit & 7))) & 1;
}

// INFO frame: type, proto, theme, bootStyle, flags, then "version\0profile\0bootText\0".
// flags: bit0 update window open, bit1 boot image stored, bit2 phone linked, bit3 scooter linked,
// bit4 custom dashboard on, bit5 layout stored, bit6 start cap (20 km/h at power-on) on.
inline size_t buildInfo(uint8_t* out, size_t cap, uint8_t theme, uint8_t style, uint8_t flags,
                        const char* version, const char* profile, const char* bootText, const char* extra = "") {
  const size_t need = 5 + strlen(version) + 1 + strlen(profile) + 1 + strlen(bootText) + 1 + strlen(extra) + 1;
  if (cap < need) return 0;
  size_t k = 0;
  out[k++] = ST_INFO; out[k++] = kProto; out[k++] = theme; out[k++] = style; out[k++] = flags;
  const char* parts[4] = {version, profile, bootText, extra};
  for (int i = 0; i < 4; ++i) { size_t l = strlen(parts[i]) + 1; memcpy(out + k, parts[i], l); k += l; }
  return k;
}

// Live data for the phone. valid bits: 1 speed, 2 percent, 4 volts, 8 power, 16 current, 32 controller temp,
// 64 motor temp, 128 odometer. mode 0..3 (walk, eco, drive, sport) or 255; cap: 0 not capped, 1 capped, 2 unknown.
static const size_t kTelemLen = 19;
inline size_t buildTelem(uint8_t* o, uint8_t valid, uint16_t speed10, uint8_t pct, uint16_t volt10, int16_t powerW,
                         int16_t amp10, int8_t tempC, int8_t motorC, uint8_t mode, uint8_t cap, uint32_t odo10) {
  o[0] = ST_TELEM; o[1] = valid; o[2] = speed10 & 0xFF; o[3] = speed10 >> 8; o[4] = pct; o[5] = volt10 & 0xFF; o[6] = volt10 >> 8;
  o[7] = (uint8_t)(powerW & 0xFF); o[8] = (uint8_t)((uint16_t)powerW >> 8); o[9] = (uint8_t)(amp10 & 0xFF); o[10] = (uint8_t)((uint16_t)amp10 >> 8);
  o[11] = (uint8_t)tempC; o[12] = (uint8_t)motorC; o[13] = mode; o[14] = cap;
  o[15] = odo10 & 0xFF; o[16] = (odo10 >> 8) & 0xFF; o[17] = (odo10 >> 16) & 0xFF; o[18] = (odo10 >> 24) & 0xFF;
  return kTelemLen;
}

}  // namespace app
