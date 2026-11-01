#pragma once
// Custom dashboard layouts ("widgets") 2.0. Binary format produced by app/widgetlib.js.
// The parser re-checks every rule, so a bad or hostile upload can never be stored or drawn.
// Version 1 layouts (stored by older firmware) are upgraded on load. Pure C++11 (host-tested).
// See docs/WIDGETS.md.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "Telemetry.h"

namespace wl {

static const int kMaxItems = 48, kMaxStr = 24, kStrLen = 16, kMaxColors = 16, kHeader = 12, kItemLen = 32, kMaxPages = 4;
static const size_t kMaxBytes = kHeader + kMaxStr * kStrLen + kMaxColors * 2 + kMaxItems * kItemLen;   // 1964

enum Type { T_TEXT = 1, T_NUM = 2, T_BAR = 3, T_ARC = 4, T_RECT = 6, T_CIRCLE = 7, T_LINE = 8, T_BUTTON = 9, T_SEGBAR = 10, T_SPARK = 11, T_DOT = 12, T_ICON = 13 };
enum Src {
  S_NONE = 0, S_SPEED, S_POWER, S_BATT, S_VOLT, S_CURRENT, S_TEMP, S_MTEMP, S_TRIP, S_TOTAL, S_MODE, S_FAULT,
  S_MAXSPEED, S_MAXPOWER, S_MINVOLT, S_ENERGY, S_RIDETIME,
  S_BLE, S_PHONE, S_LIMIT, S_DEMO, S_ALERT, S_REGEN, S_MOVING, S_LAST = S_MOVING
};
enum Action { A_NONE = 0, A_NEXTPAGE, A_PREVPAGE, A_WALK, A_ECO, A_DRIVE, A_SPORT, A_PREVTRACK, A_PLAYPAUSE, A_NEXTTRACK, A_SETTINGS, A_MESSAGES, A_MUSIC, A_SAFETY, A_LAST = A_SAFETY };
enum Flag { F_FILL = 1, F_ROUND = 2, F_BORDER = 4, F_KNOB = 8, F_TICKS = 16, F_CAPS = 32, F_BIDIR = 64, F_REV = 128 };
enum Pal { P_TEXT = 0, P_NEON, P_OK, P_WARN, P_BAD, P_DIM, P_PANEL, P_BG, P_GLOW, P_WHITE, P_BLACK, P_LASTNAMED = P_BLACK, P_CUSTOM = 16 };
static const uint8_t kPageAll = 15, kNoStr = 255;

inline bool isNumeric(uint8_t s) { return (s >= S_SPEED && s <= S_TOTAL) || (s >= S_MAXSPEED && s <= S_RIDETIME); }
inline bool isStatus(uint8_t s) { return s >= S_BLE && s <= S_MOVING; }

struct Item {
  uint8_t type, src, color, color2, font, align, dp, page, str, str2, flags, a0, sweep, thick, showSrc, showOp, thrFlags;
  int16_t minv;
  uint16_t maxv;
  int16_t x, y;
  uint16_t w, h;
  int16_t showVal, warn, bad;
};

struct Layout {
  bool valid;
  uint8_t count, strCount, colorCount, pages, bg, flags;
  char str[kMaxStr][kStrLen];
  uint16_t colors[kMaxColors];
  Item items[kMaxItems];
  Layout() { reset(); }
  void reset() { valid = false; count = strCount = colorCount = 0; pages = 1; bg = P_BG; flags = 0; memset(str, 0, sizeof(str)); memset(colors, 0, sizeof(colors)); memset(items, 0, sizeof(items)); }
};

inline int16_t rd16s(const uint8_t* p) { return (int16_t)(uint16_t)(p[0] | (p[1] << 8)); }
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

inline bool validPal(uint8_t c, uint8_t colorCount) { return c <= P_LASTNAMED || (c >= P_CUSTOM && c < P_CUSTOM + colorCount); }

// ---- version 2 -------------------------------------------------------------------------------------------------
inline bool parseV2(const uint8_t* d, size_t n, Layout& out) {
  if (n < (size_t)kHeader || n > kMaxBytes) return false;
  const int count = d[3], sc = d[4], cc = d[5];
  const int pages = d[8];
  if (count < 1 || count > kMaxItems || sc > kMaxStr || cc > kMaxColors || pages < 1 || pages > kMaxPages) return false;
  const size_t total = (size_t)kHeader + (size_t)sc * kStrLen + (size_t)cc * 2 + (size_t)count * kItemLen;
  if (rd16(d + 6) != total || n != total) return false;
  if (!validPal(d[9], (uint8_t)cc)) return false;
  for (int i = 0; i < sc; ++i) {
    const uint8_t* s = d + kHeader + i * kStrLen;
    bool ended = false;
    for (int k = 0; k < kStrLen; ++k) {
      if (s[k] == 0) ended = true;
      else if (ended || s[k] < 0x20 || s[k] > 0x7E) return false;   // printable ASCII, NUL padded
    }
    if (!ended) return false;
    memcpy(out.str[i], s, kStrLen);
  }
  const uint8_t* cp = d + kHeader + sc * kStrLen;
  for (int i = 0; i < cc; ++i) out.colors[i] = rd16(cp + i * 2);
  const uint8_t* p = cp + cc * 2;
  for (int i = 0; i < count; ++i, p += kItemLen) {
    Item& it = out.items[i];
    it.type = p[0]; it.src = p[1]; it.color = p[2]; it.color2 = p[3]; it.font = p[4];
    it.align = p[5] & 3; it.dp = (uint8_t)((p[5] >> 2) & 3); it.page = (uint8_t)(p[5] >> 4); it.str = p[6]; it.str2 = p[7];
    it.maxv = rd16(p + 8); it.minv = rd16s(p + 10); it.x = rd16s(p + 12); it.y = rd16s(p + 14); it.w = rd16(p + 16); it.h = rd16(p + 18);
    it.flags = p[20]; it.a0 = p[21]; it.sweep = p[22]; it.thick = p[23]; it.showSrc = p[24]; it.showOp = (uint8_t)(p[25] & 7); it.thrFlags = (uint8_t)(p[25] >> 3);
    it.showVal = rd16s(p + 26); it.warn = rd16s(p + 28); it.bad = rd16s(p + 30);
    if (!validPal(it.color, (uint8_t)cc) || !validPal(it.color2, (uint8_t)cc)) return false;
    if (it.font > 5 || it.align > 2 || it.dp > 2) return false;
    if (it.page != kPageAll && it.page >= pages) return false;
    if (it.str != kNoStr && it.str >= sc) return false;
    if (it.str2 != kNoStr && it.str2 >= sc) return false;
    if (it.a0 > 179 || (p[25] >> 5) != 0) return false;
    if (it.showSrc > S_LAST || it.showSrc == S_MODE || it.showOp > 6 || (it.showSrc == 0 && it.showOp != 0) || (it.showSrc != 0 && it.showOp == 0)) return false;
    if (it.src > S_LAST) return false;
    const int x = it.x, y = it.y;
    if (x < -200 || x > 520 || y < -200 || y > 520) return false;
    const int w = it.w, h = it.h, th = it.thick;
    const int mn = it.minv, mx = it.maxv;
    const int minWH = w < h ? w : h;
    switch (it.type) {
      case T_TEXT:
        if (it.src != S_NONE || it.font > 2 || it.str == kNoStr) return false;
        break;
      case T_NUM:
        if (it.src == S_NONE) return false;
        if (it.src == S_MODE && it.font > 2) return false;
        if (it.font > 2 && (it.str != kNoStr || it.str2 != kNoStr)) return false;   // fonts 6, 7, 8 only draw digits
        break;
      case T_BAR:
        if (!isNumeric(it.src) || mx <= mn || w < 4 || w > 320 || h < 2 || h > 200 || th > minWH / 2) return false;
        break;
      case T_ARC:
        if (!isNumeric(it.src) || mx <= mn || w < 8 || w > 160 || h < 1 || h >= w || it.sweep < 1 || it.sweep > 180) return false;
        break;
      case T_RECT:
        if (w < 2 || w > 640 || h < 2 || h > 640 || th > minWH / 2) return false;
        break;
      case T_CIRCLE:
        if (w < 2 || w > 160 || th > w) return false;
        break;
      case T_LINE:
        if (w > 640 || h > 640 || (w == 0 && h == 0) || th < 1 || th > 12) return false;
        break;
      case T_BUTTON:
        if (w < 16 || w > 320 || h < 12 || h > 200 || it.src > A_LAST || it.str == kNoStr || it.font > 2 || th > minWH / 2) return false;
        break;
      case T_SEGBAR:
        if (!isNumeric(it.src) || mx <= mn || th < 2 || th > 40 || h < 2 || h > 100 || w < th * 2 || w > 320) return false;
        break;
      case T_SPARK:
        if (!isNumeric(it.src) || mx <= mn || w < 16 || w > 320 || h < 8 || h > 200 || th < 1 || th > 6) return false;
        break;
      case T_DOT:
        if (it.src == S_NONE || it.src == S_MODE || w < 2 || w > 40) return false;
        break;
      case T_ICON:
        if (th > 7 || w < 8 || w > 96) return false;
        break;
      default:
        return false;
    }
  }
  out.count = (uint8_t)count; out.strCount = (uint8_t)sc; out.colorCount = (uint8_t)cc; out.pages = (uint8_t)pages;
  out.bg = d[9]; out.flags = d[10];
  out.valid = true;
  return true;
}

// ---- version 1 (older firmware stored these) -> upgraded to the version 2 structure ------------------------------
static const int kV1Top = 30, kV1Header = 8, kV1StrLen = 12, kV1Item = 16, kV1MaxItems = 16, kV1MaxStr = 8;
inline bool parseV1(const uint8_t* d, size_t n, Layout& out) {
  if (n < 8 || n > (size_t)(kV1Header + kV1MaxStr * kV1StrLen + kV1MaxItems * kV1Item)) return false;
  const int count = d[3], sc = d[4];
  if (count < 1 || count > kV1MaxItems || sc > kV1MaxStr) return false;
  const size_t total = (size_t)kV1Header + (size_t)sc * kV1StrLen + (size_t)count * kV1Item;
  if (rd16(d + 6) != total || n != total) return false;
  for (int i = 0; i < sc; ++i) {
    const uint8_t* s = d + kV1Header + i * kV1StrLen;
    bool ended = false;
    for (int k = 0; k < kV1StrLen; ++k) {
      if (s[k] == 0) ended = true;
      else if (ended || s[k] < 0x20 || s[k] > 0x7E) return false;
    }
    if (!ended) return false;
    memcpy(out.str[i], s, kV1StrLen);          // slots are 16 wide now, the rest stays NUL
  }
  int strs = sc;
  const uint8_t* p = d + kV1Header + sc * kV1StrLen;
  int o = 0;
  for (int i = 0; i < count; ++i, p += kV1Item) {
    const uint8_t type = p[0], src = p[1], color = p[2], font = p[3], align = (uint8_t)(p[4] & 0x0F), dp = (uint8_t)(p[4] >> 4), str = p[5];
    const uint16_t maxv = rd16(p + 6), w = rd16(p + 12), h = rd16(p + 14);
    const int16_t x = rd16s(p + 8), y = rd16s(p + 10);
    if (type < 1 || type > 6 || src > S_FAULT || color > 6 || font > 4 || align > 2 || dp > 2) return false;
    if (str != 255 && str >= sc) return false;
    const bool numeric = src >= S_SPEED && src <= S_TOTAL;
    switch (type) {
      case 1: if (src != S_NONE || font > 1 || str == 255 || y < kV1Top) return false; break;
      case 2: if (src == S_NONE || y < kV1Top || (src == S_MODE && font > 1)) return false; break;
      case 3: if (!numeric || maxv < 1 || w < 8 || w > 320 || h < 3 || h > 40 || y < kV1Top) return false; break;
      case 4:
        if (!numeric || maxv < 1 || w < 20 || w > 100 || h < 4 || h > 30 || h >= w) return false;
        if (y - (int)w < kV1Top || x - (int)w < 0 || x + (int)w > 320 || y + (int)w > 240) return false;
        break;
      case 5: break;
      case 6: if (w < 4 || w > 320 || h < 4 || h > 210 || y < kV1Top) return false; break;
    }
    if (type != 4 && type != 5 && (x < 0 || x > 319 || y > 239)) return false;
    if (type == 5) {                                       // four mode buttons along the bottom
      static const char* const nm[4] = {"WALK", "ECO", "DRIVE", "SPORT"};
      static const uint8_t act[4] = {A_WALK, A_ECO, A_DRIVE, A_SPORT};
      if (o + 4 > kMaxItems || strs + 4 > kMaxStr) return false;
      for (int k = 0; k < 4; ++k) {
        strncpy(out.str[strs], nm[k], kStrLen - 1);
        Item& b = out.items[o++];
        memset(&b, 0, sizeof(b));
        b.type = T_BUTTON; b.src = act[k]; b.color = P_TEXT; b.color2 = P_NEON; b.font = 1; b.page = kPageAll; b.str = (uint8_t)strs++; b.str2 = kNoStr;
        b.x = (int16_t)(4 + k * 79); b.y = 206; b.w = 75; b.h = 30; b.thick = 9; b.sweep = 1;
      }
      continue;
    }
    if (o >= kMaxItems) return false;
    Item& it = out.items[o++];
    memset(&it, 0, sizeof(it));
    it.src = src; it.color = color; it.color2 = P_PANEL; it.font = (uint8_t)(font + 1); it.align = align; it.dp = dp; it.page = kPageAll;
    it.str = str; it.str2 = kNoStr; it.maxv = maxv; it.minv = 0; it.x = x; it.y = y; it.w = w; it.h = h; it.sweep = 1;
    switch (type) {
      case 1: it.type = T_TEXT; break;
      case 2: it.type = T_NUM; break;
      case 3: it.type = T_BAR; it.flags = F_BORDER; it.thick = 0; break;
      case 4: it.type = T_ARC; it.a0 = 68; it.sweep = 135; break;        // 136 deg start, 270 deg wide: the old ring
      case 6: it.type = T_RECT; it.flags = F_BORDER; it.thick = 4; it.str = kNoStr; break;
    }
  }
  out.count = (uint8_t)o; out.strCount = (uint8_t)strs; out.colorCount = 0; out.pages = 1; out.bg = P_BG; out.flags = 1;
  out.valid = true;
  return true;
}

// Returns true only for a completely valid layout.
inline bool parse(const uint8_t* d, size_t n, Layout& out) {
  out.reset();
  if (!d || n < 8) return false;
  if (d[0] != 0x4E || d[1] != 0x57) return false;
  bool ok = false;
  if (d[2] == 2) ok = parseV2(d, n, out);
  else if (d[2] == 1) ok = parseV1(d, n, out);
  if (!ok) out.reset();
  return ok;
}

// ---- live values -------------------------------------------------------------------------------------------------
struct Live {
  bool ble, phone, cap, demo;
  float maxSpeed, maxPower, minVolt, wh;
  uint32_t rideMin;
  Live() : ble(false), phone(false), cap(false), demo(false), maxSpeed(0), maxPower(0), minVolt(0), wh(0), rideMin(0) {}
};

// Numeric value of a source, only when the reading is valid (never invented). Status sources are always valid: 0 or 1.
inline bool valueFor(uint8_t src, const Telemetry& t, const Live& L, float& v) {
  switch (src) {
    case S_SPEED: if (!t.speedValid) return false; v = t.speedKmh; return true;
    case S_POWER: if (!t.powerValid) return false; v = (float)t.powerW; return true;
    case S_BATT: if (!t.battPctValid) return false; v = (float)t.battPct; return true;
    case S_VOLT: if (!t.voltValid) return false; v = t.volts; return true;
    case S_CURRENT: if (!t.currentValid) return false; v = t.currentA; return true;
    case S_TEMP: if (!t.tempValid) return false; v = t.tempC; return true;
    case S_MTEMP: if (!t.motorTempValid) return false; v = t.motorTempC; return true;
    case S_TRIP: if (!t.tripValid) return false; v = t.tripKm; return true;
    case S_TOTAL: if (!t.totalValid) return false; v = t.totalKm; return true;
    case S_FAULT: if (!t.faultValid) return false; v = (float)t.faultCode; return true;
    case S_MAXSPEED: if (!t.speedValid) return false; v = L.maxSpeed; return true;
    case S_MAXPOWER: if (!t.powerValid) return false; v = L.maxPower; return true;
    case S_MINVOLT: if (!t.voltValid || L.minVolt <= 0.0f) return false; v = L.minVolt; return true;
    case S_ENERGY: if (!t.powerValid) return false; v = L.wh; return true;
    case S_RIDETIME: v = (float)L.rideMin; return true;
    case S_BLE: v = L.ble ? 1.0f : 0.0f; return true;
    case S_PHONE: v = L.phone ? 1.0f : 0.0f; return true;
    case S_LIMIT: v = L.cap ? 1.0f : 0.0f; return true;
    case S_DEMO: v = (L.demo || t.isDemo) ? 1.0f : 0.0f; return true;
    case S_ALERT: v = (t.faultValid && t.faultCode != 0) ? 1.0f : 0.0f; return true;
    case S_REGEN: if (!t.powerValid) return false; v = t.powerW < 0 ? 1.0f : 0.0f; return true;
    case S_MOVING: if (!t.speedValid) return false; v = t.speedKmh > 1.0f ? 1.0f : 0.0f; return true;
    default: return false;
  }
}
inline bool valueFor(uint8_t src, const Telemetry& t, float& v) { Live L; return valueFor(src, t, L, v); }

// Fraction 0..1 of the range minv..maxv.
inline float fractionRange(float v, int16_t minv, uint16_t maxv) {
  const float span = (float)maxv - (float)minv;
  if (span <= 0.0f) return 0.0f;
  const float f = (v - (float)minv) / span;
  return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}
// Fraction for old-style use: magnitude against 0..maxv (negative values, e.g. regeneration, use their magnitude).
inline float fraction(float v, uint16_t maxv) {
  if (maxv == 0) return 0.0f;
  if (v < 0) v = -v;
  const float f = v / (float)maxv;
  return f > 1.0f ? 1.0f : f;
}

// "Show only when" rule. An unavailable reading hides the item.
inline bool compare(float v, uint8_t op, int16_t c) {
  switch (op) {
    case 1: return v > (float)c;
    case 2: return v < (float)c;
    case 3: return v == (float)c;
    case 4: return v != (float)c;
    case 5: return v >= (float)c;
    case 6: return v <= (float)c;
    default: return true;
  }
}
inline bool visible(const Item& it, uint8_t page, const Telemetry& t, const Live& L) {
  if (it.page != kPageAll && it.page != page) return false;
  if (it.showSrc == 0) return true;
  float v;
  if (!valueFor(it.showSrc, t, L, v)) return false;
  return compare(v, it.showOp, it.showVal);
}
// 0 = normal colour, 1 = warn colour, 2 = bad colour (thresholds, optionally "below" for things like battery)
inline uint8_t severity(const Item& it, float v) {
  if (!(it.thrFlags & 1)) return 0;
  const bool below = (it.thrFlags & 2) != 0;
  if (below ? v <= (float)it.bad : v >= (float)it.bad) return 2;
  if (below ? v <= (float)it.warn : v >= (float)it.warn) return 1;
  return 0;
}

// ---- session statistics and history for graphs ---------------------------------------------------------------------
static const int kHistN = 60, kHistSrc = S_RIDETIME + 1;
struct Stats {
  Live live;
  uint32_t startMs, lastMs, lastHistMs;
  bool started;
  float hist[kHistSrc][kHistN];
  uint8_t histCount;
  Stats() { reset(0); }
  void reset(uint32_t now) {
    live = Live(); startMs = lastMs = lastHistMs = now; started = false; histCount = 0;
    memset(hist, 0, sizeof(hist));
  }
  // Call every frame. ble/phone/cap are given by the caller; the rest is derived here.
  void update(const Telemetry& t, uint32_t now, bool ble, bool phone, bool cap) {
    if (!started) { started = true; startMs = lastMs = lastHistMs = now; }
    live.ble = ble; live.phone = phone; live.cap = cap; live.demo = t.isDemo;
    const uint32_t dt = now - lastMs;
    lastMs = now;
    if (t.speedValid && t.speedKmh > live.maxSpeed) live.maxSpeed = t.speedKmh;
    if (t.powerValid) {
      if ((float)t.powerW > live.maxPower) live.maxPower = (float)t.powerW;
      if (t.powerW > 0 && dt < 1000) live.wh += (float)t.powerW * (float)dt / 3600000.0f;   // W * ms -> Wh
    }
    if (t.voltValid && t.volts > 5.0f && (live.minVolt <= 0.0f || t.volts < live.minVolt)) live.minVolt = t.volts;
    live.rideMin = (now - startMs) / 60000u;
    if (now - lastHistMs >= 2000) {
      lastHistMs = now;
      for (int s = 1; s < kHistSrc; ++s) {
        if (!isNumeric((uint8_t)s)) continue;
        float v = 0.0f;
        if (!valueFor((uint8_t)s, t, live, v)) v = 0.0f;
        memmove(&hist[s][0], &hist[s][1], sizeof(float) * (kHistN - 1));
        hist[s][kHistN - 1] = v;
      }
      if (histCount < kHistN) ++histCount;
    }
  }
};

}  // namespace wl
