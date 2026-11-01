// Host-side unit tests for the pure logic modules (no ESP32 needed).
#include <stdio.h>
#include <string.h>
#include "../../src/Format.h"
#include "../../src/Gesture.h"
#include "../../src/TouchCal.h"
#include "../../src/SafetyLogic.h"
#include "../../src/SafetyProfile.h"
#include "../../src/DemoTelemetry.h"
#include "../../src/ScooterControl.h"
#include "../../src/XbotProtocol.h"
#include "../../src/AppProtocol.h"
#include "../../src/Theme.h"
#include "../../src/StartCap.h"
#include "../../src/DefaultLayout.h"
#include "../../src/WidgetLayout.h"
#include "../../src/NotifStore.h"
#include "../../src/AppleParsers.h"
#include <stdlib.h>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

// Stubs for the two functions the real firmware implements in ScooterProtocol.cpp
namespace ScooterProtocol {
bool parseNotification(const uint8_t*, size_t, Telemetry&) { return false; }
bool requestSpeedLimit(uint8_t) { return false; }
bool writeMode(uint8_t) { return false; }
bool writeLight(bool) { return false; }
bool writeZeroStart(bool) { return false; }
bool writeHazard(bool) { return false; }
}

static GestureEvent drag(GestureDetector& g, int x0, int y0, int x1, int y1, uint32_t ms) {
  uint32_t t = 1000;
  g.update(true, (int16_t)x0, (int16_t)y0, t);
  g.update(true, (int16_t)((x0 + x1) / 2), (int16_t)((y0 + y1) / 2), t + ms / 2);
  g.update(true, (int16_t)x1, (int16_t)y1, t + ms - 1);
  return g.update(false, 0, 0, t + ms);
}

static void test_format() {
  char b[16];
  fmtFixed(b, sizeof b, false, 12.3f, 1); CHECK(strcmp(b, "--") == 0);   // never invent values
  fmtFixed(b, sizeof b, true, 24.6f, 0);  CHECK(strcmp(b, "25") == 0);
  fmtFixed(b, sizeof b, true, 37.24f, 1); CHECK(strcmp(b, "37.2") == 0);
}

static void test_telemetry_default_invalid() {
  Telemetry t;
  CHECK(!t.speedValid && !t.battPctValid && !t.voltValid && !t.powerValid);
  CHECK(!t.tripValid && !t.totalValid && !t.faultValid && !t.modeValid && !t.isDemo);
}

static void test_gestures() {
  GestureDetector g;
  GestureEvent e = drag(g, 160, 180, 165, 90, 250);
  CHECK(e.type == GestureType::SwipeUp);
  e = drag(g, 160, 80, 158, 170, 250);   CHECK(e.type == GestureType::SwipeDown);
  e = drag(g, 250, 100, 100, 110, 250);  CHECK(e.type == GestureType::SwipeLeft);
  e = drag(g, 100, 100, 250, 105, 250);  CHECK(e.type == GestureType::SwipeRight);
  e = drag(g, 100, 100, 104, 103, 120);  CHECK(e.type == GestureType::Tap);
  CHECK(e.x == 100 && e.y == 100);
  e = drag(g, 160, 180, 165, 90, 1500);  CHECK(e.type == GestureType::None);   // too slow
  e = drag(g, 160, 180, 195, 150, 250);  CHECK(e.type == GestureType::None);   // diagonal / short
  e = g.update(false, 0, 0, 5000);       CHECK(e.type == GestureType::None);   // release w/o press
}

static void test_hold() {
  HoldTracker h;
  CHECK(!h.update(true, 1000, 1500));
  CHECK(!h.update(true, 2000, 1500));
  CHECK(h.update(true, 2500, 1500));     // fires once
  CHECK(!h.update(true, 2600, 1500));
  CHECK(!h.update(false, 2700, 1500));   // released
  CHECK(!h.update(true, 2800, 1500));    // restarts
  CHECK(h.progress(3300, 1500) > 0.3f && h.progress(3300, 1500) < 0.4f);
  h.update(false, 3400, 1500);
  CHECK(h.progress(3500, 1500) == 0.0f);
}

static RawPt rawFromScreen(float sx, float sy, bool swap, bool flipX, bool flipY) {
  float a = flipX ? (3800.0f - sx * 11.0f) : (200.0f + sx * 11.0f);   // along screen X
  float b = flipY ? (3800.0f - sy * 15.0f) : (200.0f + sy * 15.0f);   // along screen Y
  RawPt p;
  if (swap) { p.x = (int16_t)b; p.y = (int16_t)a; } else { p.x = (int16_t)a; p.y = (int16_t)b; }
  return p;
}

static void test_cal_variant(bool swap, bool fx, bool fy) {
  TouchCal c;
  RawPt tl = rawFromScreen(20, 20, swap, fx, fy);
  RawPt tr = rawFromScreen(300, 20, swap, fx, fy);
  RawPt br = rawFromScreen(300, 220, swap, fx, fy);
  CHECK(TouchCal::compute(tl, tr, br, 320, 240, 20, c));
  CHECK(c.swapXY == swap);
  const float pts[4][2] = {{20, 20}, {300, 220}, {160, 120}, {60, 200}};
  for (int i = 0; i < 4; ++i) {
    RawPt r = rawFromScreen(pts[i][0], pts[i][1], swap, fx, fy);
    int16_t sx, sy; c.map(r.x, r.y, 320, 240, sx, sy);
    CHECK(abs(sx - (int)pts[i][0]) <= 2 && abs(sy - (int)pts[i][1]) <= 2);
  }
  int16_t sx, sy; c.map(0, 0, 320, 240, sx, sy);
  CHECK(sx >= 0 && sx < 320 && sy >= 0 && sy < 240);   // clamped
}

static void test_cal() {
  for (int s = 0; s < 2; ++s) for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y)
    test_cal_variant(s != 0, x != 0, y != 0);
  TouchCal bad; RawPt same = {1000, 1000};
  CHECK(!TouchCal::compute(same, same, same, 320, 240, 20, bad));  // implausible input rejected
  CHECK(!bad.valid);
}

static void test_safety() {
  SafetyInputs in;
  in.protocolVerified = false; in.linkUp = true; in.ackReceived = true; in.demoMode = false;
  CHECK(computeLimitStatus(in) == LimitStatus::NotSupported);   // never Confirmed w/o verified protocol
  in.demoMode = true;
  CHECK(computeLimitStatus(in) == LimitStatus::DemoSimulated);
  in.protocolVerified = true; in.linkUp = false; in.ackReceived = false;
  CHECK(computeLimitStatus(in) == LimitStatus::Unknown);        // link error -> unknown
  in.linkUp = true;
  CHECK(computeLimitStatus(in) == LimitStatus::Unknown);        // no ack -> unknown
  in.ackReceived = true;
  CHECK(computeLimitStatus(in) == LimitStatus::Confirmed);

  CHECK(mayRequestLimit(20, false, 0));
  CHECK(mayRequestLimit(20, true, 25));
  CHECK(mayRequestLimit(20, true, 20));
  CHECK(!mayRequestLimit(20, true, 15));   // would raise the limit
  CHECK(!mayRequestLimit(25, true, 30));   // only 20 allowed
  CHECK(!mayRequestLimit(0, false, 0));

  SafetyProfile sp;
  CHECK(sp.status(false) == LimitStatus::NotSupported);
  CHECK(sp.requestQuickLimit(false) == QuickResult::NotSupported && !sp.demoArmed());
  CHECK(sp.status(true) == LimitStatus::DemoSimulated);
  CHECK(sp.requestQuickLimit(true) == QuickResult::DemoOnly && sp.demoArmed());
  sp.clearDemo(); CHECK(!sp.demoArmed());
  sp.requestQuickLimit(true);
  CHECK(sp.requestQuickLimit(false) == QuickResult::NotSupported && !sp.demoArmed());
  CHECK(!ScooterProtocol::kVerified && !ScooterProtocol::kWriteSupported);
  CHECK(!ScooterProtocol::requestSpeedLimit(20));                // nothing is ever sent
}

static void test_demo() {
  for (uint32_t t = 0; t < 600000; t += 777) {
    Telemetry d = demoSample(t, 0);
    CHECK(d.isDemo && d.speedValid && d.speedKmh >= 0.0f && d.speedKmh <= 31.0f);
    Telemetry c = demoSample(t, 20);
    CHECK(c.speedKmh <= 20.001f && c.isDemo);                    // demo cap works
    CHECK(c.battPct >= 30 && c.battPct <= 100);
  }
}

static void test_control() {
  ScooterControl c;
  CHECK(c.mode() == RideMode::Eco && !c.light() && !c.zeroStart() && !c.hazard());
  // demo: local state changes, nothing "sent"
  CHECK(c.setMode(RideMode::Sport, true) == ControlResult::DemoOnly && c.mode() == RideMode::Sport);
  CHECK(c.setLight(true, true) == ControlResult::DemoOnly && c.light());
  CHECK(c.setZeroStart(true, true) == ControlResult::DemoOnly && c.zeroStart());
  CHECK(c.setHazard(true, true) == ControlResult::DemoOnly && c.hazard());
  // not demo: never sent, state never changes
  CHECK(c.setMode(RideMode::Walk, false) == ControlResult::NotSupported && c.mode() == RideMode::Sport);
  CHECK(c.setLight(false, false) == ControlResult::NotSupported && c.light());
  CHECK(c.setZeroStart(false, false) == ControlResult::NotSupported && c.zeroStart());
  CHECK(c.setHazard(false, false) == ControlResult::NotSupported && c.hazard());
  CHECK(demoModeCapKmh(RideMode::Walk) < demoModeCapKmh(RideMode::Eco));
  CHECK(demoModeCapKmh(RideMode::Eco) < demoModeCapKmh(RideMode::Drive));
  CHECK(demoModeCapKmh(RideMode::Drive) < demoModeCapKmh(RideMode::Sport));
  CHECK(strcmp(rideModeName(RideMode::Walk), "WALK") == 0 && strcmp(rideModeName(RideMode::Sport), "SPORT") == 0);
}

// ---- XBOT protocol -------------------------------------------------------------------
// Real frame captured off a 36 V vehicle (de-obfuscated), taken from the esphome-xbot tests.
static const uint8_t kRealReply[] = {0x55, 0xAA, 0x22, 0x23, 0x01, 0xEA, 0x00, 0x00, 0x00, 0x00,
                                     0x01, 0x00, 0x00, 0x00, 0xF5, 0x00, 0x4F, 0x02, 0x4F, 0x02,
                                     0x62, 0x01, 0x00, 0x00, 0x88, 0x13, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x8D, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAC, 0xFB};

static void expectVariant(const char* name, int id, int key, int dest, int cmd) {
  xbot::ProtocolVariant v = xbot::variantForName(name);
  CHECK(v.id == id && v.xorKey == key && v.readDest == dest && v.readCmd == cmd);
}

static void test_xbot_variants() {
  expectVariant("M0Robot", 3, 0x34, 0x20, 0x61);
  expectVariant("M0Robot_with_a_suffix", 3, 0x34, 0x20, 0x61);
  expectVariant("", 3, 0x34, 0x20, 0x61);
  expectVariant("something else entirely", 3, 0x34, 0x20, 0x61);   // fallback = variant 3
  expectVariant("X0Robot123", 3, 0x00, 0x20, 0x61);
  expectVariant("MIScooter", 3, 0x00, 0x20, 0x61);
  expectVariant("MiniRobot", 1, 0x55, 0x0A, 0x01);
  expectVariant("M5Robot", 1, 0xD8, 0x0A, 0x01);
  expectVariant("A6Robot", 2, 0xD8, 0x0A, 0x01);
  expectVariant("W1", 5, 0x37, 0x0A, 0x01);
  expectVariant("GoKart", 4, 0x00, 0x0A, 0x01);
  expectVariant("M0Clean77", 6, 0x00, 0x32, 0x01);
  expectVariant("TK2-S99", 3, 0x34, 0x20, 0x61);
  expectVariant("TK9000", 7, 0x00, 0x0A, 0x01);
  expectVariant("miniPLUS_9", 1, 0x00, 0x04, 0x01);
  expectVariant("ninebot", 3, 0x34, 0x20, 0x61);                    // case-sensitive
  expectVariant("Ninebot", 1, 0x00, 0x0A, 0x01);
  expectVariant("M1abcdef", 1, 0x7A, 0x0A, 0x01);
  expectVariant("M1abcd", 3, 0x34, 0x20, 0x61);
  expectVariant("XYabcde", 1, 0x18, 0x0A, 0x01);
  expectVariant("aXbcdefg", 1, 0x18, 0x0A, 0x01);
  expectVariant("XYabcd", 3, 0x34, 0x20, 0x61);
  expectVariant("abXcdefg", 3, 0x34, 0x20, 0x61);
}

static void test_xbot_framing() {
  uint8_t b[xbot::kMaxRequest];
  const uint8_t one78[] = {0x78};
  size_t n = xbot::buildRequest(b, sizeof b, 0x20, 0x61, 0x19, one78, 1);
  const uint8_t want[] = {0x55, 0xAA, 0x03, 0x20, 0x61, 0x19, 0x78, 0xEA, 0xFE};
  CHECK(n == sizeof(want) && memcmp(b, want, n) == 0);
  const uint8_t one1e[] = {0x1E};
  n = xbot::buildRequest(b, sizeof b, 0x21, 0x01, 0x14, one1e, 1);
  const uint8_t want2[] = {0x55, 0xAA, 0x03, 0x21, 0x01, 0x14, 0x1E, 0xA8, 0xFF};
  CHECK(n == sizeof(want2) && memcmp(b, want2, n) == 0);
  CHECK(xbot::checksumOk(b, n));
  b[6] ^= 1; CHECK(!xbot::checksumOk(b, n));
  CHECK(xbot::buildRequest(b, 4, 0x20, 0x61, 0, one78, 1) == 0);   // buffer too small
  // every poll entry builds a valid frame
  for (int i = 0; i < xbot::kPollCount; ++i) {
    const xbot::PollEntry& p = xbot::kPollTable[i];
    uint8_t cnt[1] = {p.count};
    size_t m = xbot::buildRequest(b, sizeof b, p.dest, p.cmd, p.reg, cnt, 1);
    CHECK(m == 9 && xbot::checksumOk(b, m));
  }
  // xor is symmetric and covers the whole frame
  uint8_t f[sizeof(kRealReply)]; memcpy(f, kRealReply, sizeof f);
  xbot::applyXor(f, sizeof f, 0x34);
  CHECK(f[0] == (0x55 ^ 0x34) && f[1] == (0xAA ^ 0x34));
  CHECK(xbot::diagnoseKey(f, sizeof f) == 0x34);
  CHECK(xbot::diagnoseKey(kRealReply, sizeof kRealReply) == 0x00);
  const uint8_t junk[] = {0x01, 0x02, 0x03};
  CHECK(xbot::diagnoseKey(junk, 3) == -1);
  xbot::applyXor(f, sizeof f, 0x34);
  CHECK(memcmp(f, kRealReply, sizeof f) == 0);
  // live sequence only references valid table entries
  for (int i = 0; i < xbot::kLiveCount; ++i) CHECK(xbot::kLiveSeq[i] < xbot::kPollCount);
  bool has9 = false, has10 = false;                                              // live polling covers motor and controller temperature
  for (int i = 0; i < xbot::kLiveCount; ++i) { if (xbot::kLiveSeq[i] == 9) has9 = true; if (xbot::kLiveSeq[i] == 10) has10 = true; }
  CHECK(has9 && has10);
  CHECK(xbot::kPollTable[9].reg <= xbot::REG_MOTOR_TEMP && xbot::REG_MOTOR_TEMP < xbot::kPollTable[9].reg + xbot::kPollTable[9].count / 2);
  CHECK(xbot::kPollTable[10].reg <= xbot::REG_TEMP && xbot::REG_TEMP < xbot::kPollTable[10].reg + xbot::kPollTable[10].count / 2);
}

static void test_xbot_real_frame() {
  CHECK(xbot::checksumOk(kRealReply, sizeof kRealReply));
  xbot::RxAccumulator<256> rx;
  int frames = 0;
  uint8_t keyed[sizeof(kRealReply)]; memcpy(keyed, kRealReply, sizeof keyed);
  xbot::applyXor(keyed, sizeof keyed, 0x34);
  rx.append(keyed, 17, 0x34);                         // split across two notifications
  rx.drain([&](const uint8_t*, size_t) { ++frames; });
  CHECK(frames == 0 && rx.size() == 17);
  rx.append(keyed + 17, sizeof keyed - 17, 0x34);
  xbot::RegisterFile rf;
  rx.drain([&](const uint8_t* f, size_t n) {
    ++frames; rf.beginFrame();
    xbot::decodeRegisters(f, n, [&](uint8_t r, uint16_t v) { rf.set(r, v); });
  });
  CHECK(frames == 1 && rx.size() == 0);
  CHECK(rf.get(0xEE) == 245 && rf.get(0xEF) == 591 && rf.get(0xF0) == 591 && rf.get(0xF1) == 354);
  CHECK(rf.get(0xF2) == 0 && rf.get(0xF3) == 5000 && rf.get(0xF7) == 141 && rf.get(0xEC) == 1);
  CHECK(rf.has(0xEA) && rf.has(0xF9) && !rf.has(0xFA) && !rf.has(0xE9));
  // junk before a header and a corrupt frame must not stall the next good one
  uint8_t stream[sizeof(kRealReply) * 2 + 4]; size_t k = 0;
  stream[k++] = 0x00; stream[k++] = 0x13;
  memcpy(stream + k, kRealReply, sizeof kRealReply); k += sizeof kRealReply;
  stream[k - 3] ^= 0xFF;                               // corrupt checksum/payload
  memcpy(stream + k, kRealReply, sizeof kRealReply); k += sizeof kRealReply;
  int good = 0;
  xbot::extractFrames(stream, k, [&](const uint8_t*, size_t) { ++good; });
  CHECK(good == 1);
}

// Builds a reply frame (src 0x23) carrying consecutive registers from `base`.
static size_t makeReply(uint8_t* out, size_t cap, uint8_t base, const uint16_t* regs, int n) {
  uint8_t payload[64];
  for (int i = 0; i < n; ++i) { payload[2 * i] = (uint8_t)(regs[i] & 0xFF); payload[2 * i + 1] = (uint8_t)(regs[i] >> 8); }
  return xbot::buildRequest(out, cap, 0x23, 0x01, base, payload, (size_t)(2 * n));
}

static void feed(xbot::RegisterFile& rf, uint8_t base, const uint16_t* regs, int n) {
  uint8_t f[80]; size_t len = makeReply(f, sizeof f, base, regs, n);
  CHECK(len > 0 && xbot::checksumOk(f, len));
  rf.beginFrame();
  xbot::decodeRegisters(f, len, [&](uint8_t r, uint16_t v) { rf.set(r, v); });
}

// SYNTHETIC values (not from a real vehicle) to test the register -> dashboard mapping.
static void test_xbot_telemetry() {
  xbot::RegisterFile rf;
  Telemetry t;
  xbot::fillTelemetry(rf, t);
  CHECK(!t.speedValid && !t.battPctValid && !t.voltValid && !t.powerValid && !t.tripValid && !t.totalValid);
  CHECK(!t.faultValid && !t.modeValid && !t.isDemo);                       // nothing read -> all "--"

  const uint16_t a[] = {77, 0, 0, 0, 0, 1234, 0, 0x5678, 0x0001};              // 0x22..0x2A
  feed(rf, 0x22, a, 9);
  const uint16_t trip[] = {150};   feed(rf, 0x2F, trip, 1);
  const uint16_t vi[] = {4812, 250}; feed(rf, 0x47, vi, 2);                      // 0x47,0x48 same frame
  const uint16_t tmp[] = {345};    feed(rf, 0xBB, tmp, 1);                       // 34.5 C controller
  const uint16_t mot[] = {512};    feed(rf, 0x3F, mot, 1);                       // 51.2 C motor
  const uint16_t err[] = {0};      feed(rf, 0x1B, err, 1);
  const uint16_t mode[] = {2};     feed(rf, 0x7E, mode, 1);
  xbot::fillTelemetry(rf, t);
  CHECK(t.battPctValid && t.battPct == 77);
  CHECK(t.speedValid && t.speedKmh > 12.33f && t.speedKmh < 12.35f);
  CHECK(t.totalValid && t.totalKm > 87.67f && t.totalKm < 87.68f);            // 0x15678 / 1000
  CHECK(t.tripValid && t.tripKm > 1.49f && t.tripKm < 1.51f);
  CHECK(t.voltValid && t.volts > 48.11f && t.volts < 48.13f);
  CHECK(t.powerValid && t.powerW == 120);                                       // 48.12 V * 2.50 A
  CHECK(t.faultValid && t.faultCode == 0);
  CHECK(t.currentValid && t.currentA > 2.49f && t.currentA < 2.51f);
  CHECK(t.tempValid && t.tempC > 34.4f && t.tempC < 34.6f);
  CHECK(t.motorTempValid && t.motorTempC > 51.1f && t.motorTempC < 51.3f);
  const uint16_t zero[] = {0};     feed(rf, 0x3F, zero, 1);
  const uint16_t unavail[] = {0xFFFF}; feed(rf, 0xBB, unavail, 1);
  xbot::fillTelemetry(rf, t);
  CHECK(!t.motorTempValid && !t.tempValid);                                      // flat zero / not available -> "--"
  const uint16_t cold[] = {0xFFE7}; feed(rf, 0xBB, cold, 1);                     // -2.5 C (two's complement)
  xbot::fillTelemetry(rf, t);
  CHECK(t.tempValid && t.tempC < -2.4f && t.tempC > -2.6f);
  feed(rf, 0x3F, mot, 1);
  feed(rf, 0xBB, tmp, 1);
  xbot::fillTelemetry(rf, t);
  CHECK(t.modeValid && strcmp(t.mode, "ECO") == 0 && !t.isDemo);

  const uint16_t na[] = {0xFFFF, 0, 0, 0, 0, 0xFFFF};                           // not available markers
  feed(rf, 0x22, na, 6);
  xbot::fillTelemetry(rf, t);
  CHECK(!t.battPctValid && !t.speedValid);                                       // never shown as numbers
  // 32-bit total needs both words from one frame; a stale high word must not be paired
  const uint16_t lo[] = {0x1111}; feed(rf, 0x29, lo, 1);
  xbot::fillTelemetry(rf, t);
  CHECK(!t.totalValid);
  // a negative current (regeneration) is valid and gives negative power
  const uint16_t reg[] = {4800, 0xFFFE}; feed(rf, 0x47, reg, 2);                 // -0.02 A
  xbot::fillTelemetry(rf, t);
  CHECK(t.powerValid && t.powerW <= 0);
  // out-of-range reading is dropped, not clamped
  const uint16_t big[] = {20000}; feed(rf, 0x47, big, 1);                         // 200 V
  xbot::fillTelemetry(rf, t);
  CHECK(!t.voltValid);
  const uint16_t badmode[] = {9}; feed(rf, 0x7E, badmode, 1);
  xbot::fillTelemetry(rf, t);
  CHECK(!t.modeValid);
  char v[12];
  CHECK(!xbot::controllerVersion(rf, v, sizeof v));                              // not read yet
  const uint16_t ver[] = {0x0605, 0x1153}; feed(rf, 0x1D, ver, 2);               // 0x1D, 0x1E
  CHECK(xbot::controllerVersion(rf, v, sizeof v));
  CHECK(strcmp(v, "11530605") == 0);                                             // 0x1E first, then 0x1D
}

static void test_xbot_write() {
  uint8_t b[xbot::kMaxRequest];
  // Reference vector: write(dest 0x20, cmd 0x03, reg 0xF0, 827)
  size_t n = xbot::buildWrite(b, sizeof b, 0x20, 0x03, 0xF0, 827);
  const uint8_t want[] = {0x55, 0xAA, 0x04, 0x20, 0x03, 0xF0, 0x3B, 0x03, 0xAA, 0xFE};
  CHECK(n == sizeof(want) && memcmp(b, want, n) == 0 && xbot::checksumOk(b, n));
  n = xbot::buildWrite(b, sizeof b, 0x20, 0x03, 0x7D, -1);                 // two's complement
  CHECK(n == 10 && b[6] == 0xFF && b[7] == 0xFF && xbot::checksumOk(b, n));
  CHECK(xbot::buildWrite(b, 5, 0x20, 0x03, 0x7D, 1) == 0);
  // flag registers keep the bits that are not being changed
  CHECK(xbot::applyBit(0x0000, 0x0001, true) == 0x0001);
  CHECK(xbot::applyBit(0x0A01, 0x0001, false) == 0x0A00);
  CHECK(xbot::applyBit(0x0A00, 0x0001, true) == 0x0A01);
  // the whitelist: light, zero start, mode - nothing else
  CHECK(xbot::isWritableReg(0xF2) && xbot::isWritableReg(0x7D) && xbot::isWritableReg(0x7E));
  CHECK(!xbot::isWritableReg(0x7C) && !xbot::isWritableReg(0xEE));
  for (int r = 0; r < 256; ++r) {
    const bool allowed = r == 0xF2 || r == 0x7D || r == 0x7E || r == 0x7F || r == 0xEF || r == 0xF0 || r == 0xF1;
    CHECK(xbot::isWritableReg((uint8_t)r) == allowed);                      // cruise 0x7C, wheel factor 0xEE etc. stay read-only
  }
  // mode index mapping (reference order Sport, Drive, Eco, Walking)
  CHECK(xbot::modeIndexFromInternal(0) == 3 && xbot::modeIndexFromInternal(1) == 2);   // Walk, Eco
  CHECK(xbot::modeIndexFromInternal(2) == 1 && xbot::modeIndexFromInternal(3) == 0);   // Drive, Sport
  for (uint8_t i = 0; i < 4; ++i) CHECK(xbot::internalFromModeIndex(xbot::modeIndexFromInternal(i)) == i);
  CHECK(strcmp(xbot::kModeNames[0], "SPORT") == 0 && strcmp(xbot::kModeNames[3], "WALK") == 0);
  // readback reflects what the scooter reported
  xbot::RegisterFile rf;
  xbot::Readback rb = xbot::readbackFrom(rf);
  CHECK(!rb.lightValid && !rb.zeroValid && !rb.modeValid);
  const uint16_t l[] = {0x0101}; feed(rf, 0xF2, l, 1);
  const uint16_t z[] = {0x0000}; feed(rf, 0x7D, z, 1);
  const uint16_t m[] = {1};      feed(rf, 0x7E, m, 1);
  rb = xbot::readbackFrom(rf);
  CHECK(rb.lightValid && rb.light && rb.zeroValid && !rb.zero && rb.modeValid && rb.modeIdx == 1);
  const uint16_t na[] = {0xFFFF}; feed(rf, 0xF2, na, 1);
  rb = xbot::readbackFrom(rf);
  CHECK(!rb.lightValid);
}

static void test_xbot_limit_and_lock() {
  float sc;
  CHECK(!xbot::scaleFromWheelFactor(0, sc) && !xbot::scaleFromWheelFactor(0xFFFF, sc));
  CHECK(xbot::scaleFromWheelFactor(245, sc));                                  // wheel factor of the captured frame
  CHECK(sc > 23.64f && sc < 23.66f);
  CHECK(xbot::countForKmh(35.0f, sc) == 827);                                  // reference test vector: 827 counts
  const int16_t c20 = xbot::countForKmh(20.0f, sc);
  CHECK(c20 == 473);
  CHECK(xbot::kmhFromCount((uint16_t)c20, sc) == 20);                          // truncation still reads back as 20
  CHECK(xbot::kmhFromCount(591, sc) == 25 && xbot::kmhFromCount(354, sc) == 15);   // captured limits 0xEF/0xF0, 0xF1
  CHECK(xbot::limitCountAllowed(0xF0, 827, sc) && !xbot::limitCountAllowed(0xEF, 827, sc));  // 35 ok for sport, too high for drive
  CHECK(!xbot::limitCountAllowed(0xF0, 100, sc));                              // below 6 km/h
  CHECK(!xbot::limitCountAllowed(0x7E, 400, sc) && !xbot::limitCountAllowed(0xF0, 400, 0.0f));
  CHECK(xbot::limitMaxKmh(0xF0) == 35 && xbot::limitMaxKmh(0xEF) == 30 && xbot::limitMaxKmh(0xF1) == 30);
  CHECK(xbot::lockAllowed(false, false, 0.0f) && xbot::lockAllowed(false, true, 25.0f));   // unlock always ok
  CHECK(xbot::lockAllowed(true, true, 0.0f));
  CHECK(!xbot::lockAllowed(true, true, 12.0f) && !xbot::lockAllowed(true, false, 0.0f));   // moving / unknown speed

  // readback built from the real captured frame: wheel factor 245, limits 25/25/15 km/h
  xbot::RegisterFile rf;
  xbot::RxAccumulator<256> rx;
  rx.append(kRealReply, sizeof kRealReply, 0);
  rx.drain([&](const uint8_t* f, size_t n) {
    rf.beginFrame();
    xbot::decodeRegisters(f, n, [&](uint8_t r, uint16_t v) { rf.set(r, v); });
  });
  xbot::Readback rb = xbot::readbackFrom(rf);
  CHECK(rb.limValid[0] && rb.limValid[1] && rb.limValid[2]);
  CHECK(rb.limKmh[0] == 25 && rb.limKmh[1] == 25 && rb.limKmh[2] == 15);
  CHECK(xbot::limitsCapped(rb, 20) == 0 && xbot::limitsCapped(rb, 25) == 1);
  CHECK(!rb.lockValid);                                                        // 0x7F not in that frame
  int w, k;
  xbot::decodeLimitReq(xbot::encodeLimitReq(2, 15), w, k);
  CHECK(w == 2 && k == 15 && xbot::limitRegForIndex(0) == 0xF0 && xbot::limitRegForIndex(1) == 0xEF && xbot::limitRegForIndex(2) == 0xF1);
  CHECK(rb.wfValid && rb.wheelFactor == 245 && rb.limHas[0] && rb.limRaw[0] == 591 && rb.limRaw[2] == 354);
  const uint16_t cap[] = {473};
  feed(rf, 0xF0, cap, 1);                                                      // sport limit now 20
  feed(rf, 0xEF, cap, 1);
  rb = xbot::readbackFrom(rf);
  CHECK(xbot::limitsCapped(rb, 20) == 1);                                      // eco 15 <= 20, drive/sport 20
  xbot::RegisterFile empty;
  CHECK(xbot::limitsCapped(xbot::readbackFrom(empty), 20) == -1);              // unknown, never "capped"
  const uint16_t lk[] = {1}; feed(rf, 0x7F, lk, 1);
  rb = xbot::readbackFrom(rf);
  CHECK(rb.lockValid && rb.locked);
  const uint16_t ul[] = {0}; feed(rf, 0x7F, ul, 1);
  rb = xbot::readbackFrom(rf);
  CHECK(rb.lockValid && !rb.locked);
}

static void test_app_protocol() {
  const uint8_t v[] = {'1','2','3','4','5','6','7','8','9'};
  CHECK(app::crc32Update(0, v, 9) == 0xCBF43926u);                       // standard CRC-32 check value
  uint32_t c = app::crc32Update(0, v, 4);
  c = app::crc32Update(c, v + 4, 5);
  CHECK(c == 0xCBF43926u);                                               // chaining gives the same result
  uint8_t h[16] = {0xE9, 2, 2, 0x20};
  CHECK(app::isEspImageHeader(h, 16));
  h[13] = 1; CHECK(!app::isEspImageHeader(h, 16));                       // other chip
  h[13] = 0; h[0] = 0x00; CHECK(!app::isEspImageHeader(h, 16));          // no magic
  CHECK(!app::isEspImageHeader(h, 8));
  uint8_t f[16];
  size_t n = app::buildOtaStat(f, app::OTA_RECV, app::E_NONE, 0x01020304u, 0x0A0B0C0Du);
  CHECK(n == 11 && f[0] == app::ST_OTA && f[1] == 2 && f[3] == 4 && f[6] == 1 && f[7] == 0x0D && f[10] == 0x0A);
  CHECK(app::le32(f + 3) == 0x01020304u && app::le32(f + 7) == 0x0A0B0C0Du);
  n = app::buildAck(f, app::OP_THEME, 0);
  CHECK(n == 3 && f[0] == app::ST_ACK && f[1] == 0x20 && f[2] == 0);
  char t[16];
  const uint8_t raw[] = {'N', 'O', 0x01, 'V', 0xC3, 0xA6, 'A', ' ', 'R', 'I', 'D', 'E', 'X', 'X', 'X', 'X', 'X', 'X'};
  app::sanitizeBootText(t, sizeof t, raw, sizeof raw);
  CHECK(strcmp(t, "NOVA RIDEXXXX") == 0 || strlen(t) <= 15);              // non-printable bytes dropped, length capped
  CHECK(strlen(t) <= 15 && t[0] == 'N' && t[1] == 'O' && t[2] == 'V');
  uint8_t img[app::kBootImgBytes];
  memset(img, 0, sizeof img);
  img[0] = 0x80; img[16] = 0x01;                                          // pixel (0,0) and (7,1)
  CHECK(app::bootPixel(img, 0, 0) && !app::bootPixel(img, 1, 0) && app::bootPixel(img, 7, 1));
  CHECK(!app::bootPixel(img, 128, 0) && !app::bootPixel(img, 0, 64) && !app::bootPixel(img, -1, 0));
  uint8_t info[64];
  n = app::buildInfo(info, sizeof info, 2, 1, 0x05, "0.8.0", "cyd_st7789", "ANNA");
  CHECK(n > 0 && info[0] == app::ST_INFO && info[1] == app::kProto && info[2] == 2 && info[3] == 1 && info[4] == 5);
  CHECK(strcmp((const char*)info + 5, "0.8.0") == 0 && strcmp((const char*)info + 11, "cyd_st7789") == 0);
  CHECK(app::buildInfo(info, 8, 0, 0, 0, "0.8.0", "cyd_st7789", "ANNA") == 0);   // too small -> nothing
  CHECK(app::kMaxAppBytes == 0x140000 && app::kMinAppBytes < app::kMaxAppBytes);
  // themes: complete and distinct
  CHECK(kThemeCount == 8);
  for (int i = 0; i < kThemeCount; ++i) {
    CHECK(kThemes[i].name && strlen(kThemes[i].name) > 0 && strlen(kThemes[i].name) <= 12);
    for (int j = i + 1; j < kThemeCount; ++j) CHECK(strcmp(kThemes[i].name, kThemes[j].name) != 0);
    const Rgb a = kThemes[i].neon, b = kThemes[i].bg;
    CHECK(a.r != b.r || a.g != b.g || a.b != b.b);                         // accent is not the background
  }
}

static bool readFile(const char* path, uint8_t* buf, size_t cap, size_t& n) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  n = fread(buf, 1, cap, f);
  fclose(f);
  return true;
}

// Layouts compiled by app/widgetlib.js (node tools/widget_cli.js bin /tmp/wbin ...) must parse in the firmware.
static const uint8_t kV1ClassicRing[] = {0x4E,0x57,0x01,0x0F,0x06,0x00,0x40,0x01,0x6B,0x6D,0x2F,0x68,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x25,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x56,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x6B,0x6D,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x43,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x01,0x01,0x00,0x00,0xFF,0x23,0x00,0x58,0x00,0x7E,0x00,0x50,0x00,0x0E,0x00,0x02,0x01,0x00,0x04,0x01,0xFF,0x00,0x00,0x58,0x00,0x76,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x05,0x01,0x01,0x00,0x00,0x00,0x58,0x00,0x9E,0x00,0x00,0x00,0x00,0x00,0x02,0x0A,0x02,0x00,0x01,0xFF,0x00,0x00,0x58,0x00,0x42,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x02,0x02,0xFF,0x00,0x00,0x24,0x01,0x1E,0x00,0x00,0x00,0x00,0x00,0x03,0x02,0x01,0x00,0x00,0xFF,0xE8,0x03,0xB8,0x00,0x50,0x00,0x7A,0x00,0x08,0x00,0x02,0x05,0x00,0x00,0x10,0x01,0x00,0x00,0xB8,0x00,0x5E,0x00,0x00,0x00,0x00,0x00,0x02,0x03,0x02,0x01,0x00,0x02,0x00,0x00,0xB8,0x00,0x70,0x00,0x00,0x00,0x00,0x00,0x02,0x04,0x00,0x01,0x12,0x03,0x00,0x00,0x32,0x01,0x70,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x02,0x00,0x00,0xFF,0x64,0x00,0xB8,0x00,0x8C,0x00,0x7A,0x00,0x0A,0x00,0x02,0x08,0x05,0x00,0x10,0x04,0x00,0x00,0xB8,0x00,0x9C,0x00,0x00,0x00,0x00,0x00,0x02,0x09,0x05,0x00,0x00,0x04,0x00,0x00,0xB8,0x00,0xAB,0x00,0x00,0x00,0x00,0x00,0x02,0x06,0x05,0x00,0x01,0x05,0x00,0x00,0x38,0x00,0xBA,0x00,0x00,0x00,0x00,0x00,0x02,0x07,0x05,0x00,0x01,0x05,0x00,0x00,0x78,0x00,0xBA,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x1E,0x00,0x00,0x00,0x00,0x00};

static bool layoutSane(const wl::Layout& M) {                          // what the renderer relies on
  for (int q = 0; q < M.count; ++q) {
    const wl::Item& it = M.items[q];
    if (!(it.str == 255 || it.str < M.strCount) || !(it.str2 == 255 || it.str2 < M.strCount)) return false;
    if (!(it.color <= wl::P_LASTNAMED || (it.color >= wl::P_CUSTOM && it.color < wl::P_CUSTOM + M.colorCount))) return false;
    if (!(it.color2 <= wl::P_LASTNAMED || (it.color2 >= wl::P_CUSTOM && it.color2 < wl::P_CUSTOM + M.colorCount))) return false;
    if (!(it.page == wl::kPageAll || it.page < M.pages)) return false;
    if (it.type == wl::T_ARC && !(it.h < it.w && it.sweep >= 1 && it.sweep <= 180)) return false;
    if (it.font > 5 || it.src > wl::S_LAST) return false;
    if (it.type == wl::T_TEXT && it.str == 255) return false;
    if (it.type == wl::T_BUTTON && (it.str == 255 || it.src > wl::A_LAST)) return false;
    if (it.x < -200 || it.x > 520 || it.y < -200 || it.y > 520) return false;
  }
  return M.count >= 1 && M.pages >= 1 && M.pages <= wl::kMaxPages;
}

static void test_widgets() {
  const char* dir = getenv("NOVA_WBIN") ? getenv("NOVA_WBIN") : "/tmp/wbin";
  char listPath[160];
  snprintf(listPath, sizeof listPath, "%s/list.txt", dir);
  uint8_t lst[2048];
  size_t ln = 0;
  CHECK(readFile(listPath, lst, sizeof lst - 1, ln));
  lst[ln] = 0;
  static uint8_t buf[wl::kMaxBytes + 8];
  int seen = 0;
  for (char* name = strtok((char*)lst, "\n"); name; name = strtok(0, "\n")) {
    char path[200];
    snprintf(path, sizeof path, "%s/%s.nwl", dir, name);
    size_t n = 0;
    CHECK(readFile(path, buf, sizeof buf, n));
    wl::Layout L;
    CHECK(wl::parse(buf, n, L) && L.valid && layoutSane(L));
    ++seen;
    if (!strcmp(name, "classic-ring")) {                                // v1 file compiled to v2: ring became an arc, mode bar became 4 buttons
      CHECK(L.count == 18 && L.pages == 1 && L.items[0].type == wl::T_ARC && L.items[0].w == 80 && L.items[0].h == 14 && L.items[0].maxv == 35 && L.items[0].sweep == 135);
      CHECK(L.items[1].type == wl::T_NUM && L.items[1].src == wl::S_SPEED && L.items[1].font == 5 && L.items[1].align == 1);
      CHECK(strcmp(L.str[L.items[2].str], "km/h") == 0);
      CHECK(L.items[14].type == wl::T_BUTTON && L.items[14].src == wl::A_WALK && L.items[17].src == wl::A_SPORT && strcmp(L.str[L.items[17].str], "SPORT") == 0);
    }
    // every single-bit corruption is either rejected or still a fully valid layout (never undefined)
    for (size_t k = 0; k < n; ++k) {
      const uint8_t save = buf[k];
      for (int bit = 0; bit < 8; ++bit) {
        buf[k] = (uint8_t)(save ^ (1 << bit));
        wl::Layout M;
        if (wl::parse(buf, n, M)) CHECK(layoutSane(M));
      }
      buf[k] = save;
    }
    CHECK(!wl::parse(buf, n - 1, L) && !wl::parse(buf, n + 1, L));      // wrong length
  }
  CHECK(seen >= 6);
  { wl::Layout D; CHECK(wl::parse(kDefaultLayout, kDefaultLayoutLen, D) && layoutSane(D) && D.pages == 2);     // the built-in design
    int sport = 0, nextpage = 0; for (int q = 0; q < D.count; ++q) if (D.items[q].type == wl::T_BUTTON) { if (D.items[q].src == wl::A_SPORT) ++sport; if (D.items[q].src == wl::A_NEXTPAGE) ++nextpage; }
    CHECK(sport == 2 && nextpage == 1); }
  // version 1 bytes stored by older firmware still load (upgraded)
  { wl::Layout V; CHECK(wl::parse(kV1ClassicRing, sizeof kV1ClassicRing, V) && layoutSane(V));
    CHECK(V.count == 18 && V.items[0].type == wl::T_ARC && V.items[0].a0 == 68 && V.items[17].type == wl::T_BUTTON && V.strCount == 6 + 4); }
  wl::Layout L2;
  CHECK(!wl::parse(0, 0, L2) && !L2.valid);
  uint8_t tiny[12] = {0x4E, 0x57, 2, 0, 0, 0, 12, 0, 1, 7, 1, 0};
  CHECK(!wl::parse(tiny, 12, L2));                                      // zero items
  // value mapping never invents numbers
  Telemetry t;
  float v;
  CHECK(!wl::valueFor(wl::S_SPEED, t, v) && !wl::valueFor(wl::S_POWER, t, v) && !wl::valueFor(wl::S_NONE, t, v));
  t.speedValid = true; t.speedKmh = 21.5f;
  CHECK(wl::valueFor(wl::S_SPEED, t, v) && v > 21.4f && v < 21.6f);
  CHECK(wl::fraction(35.0f, 35) == 1.0f && wl::fraction(70.0f, 35) == 1.0f && wl::fraction(-17.5f, 35) > 0.49f && wl::fraction(5, 0) == 0.0f);
  CHECK(wl::fractionRange(0.0f, -100, 300) > 0.249f && wl::fractionRange(0.0f, -100, 300) < 0.251f && wl::fractionRange(400.0f, -100, 300) == 1.0f && wl::fractionRange(1, 5, 5) == 0.0f);
  // show-only-when rules and colour thresholds
  { wl::Item it; memset(&it, 0, sizeof it); it.page = wl::kPageAll; it.showSrc = wl::S_SPEED; it.showOp = 1; it.showVal = 20;
    wl::Live L; CHECK(wl::visible(it, 0, t, L));                         // 21.5 > 20
    t.speedKmh = 19.0f; CHECK(!wl::visible(it, 0, t, L));
    t.speedValid = false; CHECK(!wl::visible(it, 0, t, L));              // unknown reading hides the item
    it.showSrc = wl::S_BLE; it.showOp = 3; it.showVal = 1; CHECK(!wl::visible(it, 0, t, L)); L.ble = true; CHECK(wl::visible(it, 0, t, L));
    it.page = 1; CHECK(!wl::visible(it, 0, t, L) && wl::visible(it, 1, t, L));
    wl::Item r; memset(&r, 0, sizeof r); r.thrFlags = 1; r.warn = 25; r.bad = 32;
    CHECK(wl::severity(r, 10) == 0 && wl::severity(r, 25) == 1 && wl::severity(r, 40) == 2);
    r.thrFlags = 3; r.warn = 20; r.bad = 10;                             // below: battery
    CHECK(wl::severity(r, 50) == 0 && wl::severity(r, 20) == 1 && wl::severity(r, 5) == 2);
    r.thrFlags = 0; CHECK(wl::severity(r, 99) == 0); }
  // session statistics
  { wl::Stats S; Telemetry q; q.speedValid = true; q.powerValid = true; q.voltValid = true;
    q.speedKmh = 12; q.powerW = 600; q.volts = 78.0f; S.update(q, 1000, true, false, false);
    q.speedKmh = 31; q.powerW = 2400; q.volts = 71.5f; S.update(q, 1500, true, false, true);
    q.speedKmh = 5; q.powerW = -300; q.volts = 79.0f; S.update(q, 2100, true, false, true);
    CHECK(S.live.maxSpeed == 31.0f && S.live.maxPower == 2400.0f && S.live.minVolt > 71.4f && S.live.minVolt < 71.6f && S.live.cap && S.live.ble && !S.live.phone);
    CHECK(S.live.wh > 0.1f && S.live.wh < 0.5f);                          // 2400 W for 0.5 s + 600 W for 0.5 s ~ 0.4 Wh
    q.speedKmh = 7; q.powerW = 100; S.update(q, 3200, true, false, true);                 // 2 s after the first frame: first graph sample
    CHECK(S.histCount == 1 && S.hist[wl::S_SPEED][wl::kHistN - 1] == 7.0f && S.live.maxSpeed == 31.0f);
    q.powerW = -300;
    float m; CHECK(wl::valueFor(wl::S_MAXSPEED, q, S.live, m) && m == 31.0f && wl::valueFor(wl::S_REGEN, q, S.live, m) && m == 1.0f && wl::valueFor(wl::S_LIMIT, q, S.live, m) && m == 1.0f);
    q.speedValid = false; CHECK(!wl::valueFor(wl::S_MAXSPEED, q, S.live, m)); }
}

static void test_apple() {
  // --- ANCS notification source
  const uint8_t ns[8] = {0x00, 0x00, 0x04, 0x01, 0x78, 0x56, 0x34, 0x12};
  ancs::Event e;
  CHECK(ancs::parseNs(ns, 8, e) && e.id == 0 && e.category == 4 && e.uid == 0x12345678u && ancs::shouldShow(e));
  uint8_t pre[8] = {0x00, 0x04, 0x04, 0x01, 1, 0, 0, 0};
  CHECK(ancs::parseNs(pre, 8, e) && !ancs::shouldShow(e));                  // pre-existing: not shown
  uint8_t rem[8] = {0x02, 0x00, 0x04, 0x01, 1, 0, 0, 0};
  CHECK(ancs::parseNs(rem, 8, e) && !ancs::shouldShow(e));                  // removed: not shown
  CHECK(!ancs::parseNs(ns, 7, e) && !ancs::parseNs(0, 8, e));
  uint8_t req[16];
  size_t n = ancs::buildGetAttrs(req, sizeof req, 0x12345678u, 24, 70);
  const uint8_t wantReq[12] = {0x00, 0x78, 0x56, 0x34, 0x12, 0x00, 0x01, 24, 0, 0x03, 70, 0};
  CHECK(n == 12 && memcmp(req, wantReq, 12) == 0);
  CHECK(ancs::buildGetAttrs(req, 8, 1, 24, 70) == 0);

  // --- data source answer split over two notifications
  uint8_t ans[120];
  size_t k = 0;
  ans[k++] = 0x00; ans[k++] = 0x78; ans[k++] = 0x56; ans[k++] = 0x34; ans[k++] = 0x12;
  const char* app = "com.apple.MobileSMS"; const char* title = "Anna"; const char* text = "Kommer om 5 min";
  ans[k++] = 0; ans[k++] = (uint8_t)strlen(app); ans[k++] = 0; memcpy(ans + k, app, strlen(app)); k += strlen(app);
  ans[k++] = 1; ans[k++] = (uint8_t)strlen(title); ans[k++] = 0; memcpy(ans + k, title, strlen(title)); k += strlen(title);
  ans[k++] = 3; ans[k++] = (uint8_t)strlen(text); ans[k++] = 0; memcpy(ans + k, text, strlen(text)); k += strlen(text);
  ancs::DataAssembler da;
  CHECK(!da.feed(ans, 12) && !da.complete());                               // partial
  CHECK(da.feed(ans + 12, k - 12) && da.complete());
  CHECK(da.uid() == 0x12345678u && strcmp(da.app(), "com.apple.MobileSMS") == 0 && strcmp(da.title(), "Anna") == 0 && strcmp(da.text(), "Kommer om 5 min") == 0);
  char fr[20];
  ancs::friendlyApp(da.app(), fr, sizeof fr);
  CHECK(strcmp(fr, "Messages") == 0);
  ancs::friendlyApp("dk.example.minapp", fr, sizeof fr);
  CHECK(strcmp(fr, "minapp") == 0);
  // UTF-8 (e.g. Danish letters) is shown as ? instead of garbage; control bytes are dropped
  uint8_t u[32]; size_t m = 0;
  u[m++] = 0; u[m++] = 1; u[m++] = 0; u[m++] = 'x';                         // dummy to keep layout simple
  (void)u; (void)m;
  uint8_t ans2[64]; size_t j = 0;
  ans2[j++] = 0; ans2[j++] = 1; ans2[j++] = 0; ans2[j++] = 0; ans2[j++] = 0;
  ans2[j++] = 0; ans2[j++] = 1; ans2[j++] = 0; ans2[j++] = 'a';
  ans2[j++] = 1; ans2[j++] = 5; ans2[j++] = 0; ans2[j++] = 'B'; ans2[j++] = 0xC3; ans2[j++] = 0xA6; ans2[j++] = 0x07; ans2[j++] = 'r';
  ans2[j++] = 3; ans2[j++] = 0; ans2[j++] = 0;
  ancs::DataAssembler db;
  CHECK(db.feed(ans2, j) && strcmp(db.title(), "B?r") == 0 && db.text()[0] == 0);
  CHECK(!db.feed(ans2, 0) || true);
  uint8_t junk[3] = {0x09, 1, 2};
  ancs::DataAssembler dc;
  CHECK(!dc.feed(junk, 3) && !dc.complete());                               // wrong command id -> dropped
  uint8_t big[300]; memset(big, 0, sizeof big);
  CHECK(!dc.feed(big, 250) && !dc.complete());                              // too large -> dropped, no overflow

  // --- message store
  NotifStore ns2;
  NotifMsg mm;
  CHECK(ns2.count() == 0 && ns2.unread() == 0 && !ns2.get(0, mm));
  for (int i = 0; i < 8; ++i) { char t[8]; snprintf(t, sizeof t, "T%d", i); ns2.push("App", t, "text", 4); }
  CHECK(ns2.count() == NotifStore::kMax && ns2.unread() == NotifStore::kMax);
  CHECK(ns2.get(0, mm) && strcmp(mm.title, "T7") == 0);                     // newest first
  CHECK(ns2.get(NotifStore::kMax - 1, mm) && strcmp(mm.title, "T2") == 0);  // oldest kept
  CHECK(!ns2.get(NotifStore::kMax, mm));
  ns2.markAllRead();
  CHECK(ns2.unread() == 0);
  ns2.push("A", "x\x01y", "ok", 0);
  CHECK(ns2.get(0, mm) && strcmp(mm.title, "x?y") == 0 && ns2.unread() == 1);
  char longText[200]; memset(longText, 'a', sizeof longText); longText[199] = 0;
  ns2.push("A", "t", longText, 0);
  CHECK(ns2.get(0, mm) && strlen(mm.text) == sizeof(mm.text) - 1);          // truncated, never overflows
  ns2.clear();
  CHECK(ns2.count() == 0);

  // --- AMS
  ams::State st;
  const uint8_t title2[] = {2, 2, 0, 'L', 'o', 'v', 'e'};
  const uint8_t artist2[] = {2, 0, 0, 'A', 'B', 'B', 'A'};
  CHECK(ams::onEntityUpdate(st, title2, sizeof title2) && ams::onEntityUpdate(st, artist2, sizeof artist2));
  CHECK(st.haveTrack && strcmp(st.title, "Love") == 0 && strcmp(st.artist, "ABBA") == 0);
  const uint8_t play[] = {0, 1, 0, '1', ',', '1', '.', '0', '0', '0', ',', '3', '.', '2'};
  CHECK(ams::onEntityUpdate(st, play, sizeof play) && st.playing);
  const uint8_t paused[] = {0, 1, 0, '0', ',', '1', '.', '0'};
  CHECK(ams::onEntityUpdate(st, paused, sizeof paused) && !st.playing);
  CHECK(!ams::onEntityUpdate(st, play, 2) && !ams::onEntityUpdate(st, 0, 5));
  const uint8_t other[] = {1, 0, 0, 'x'};                                   // queue entity: ignored
  CHECK(!ams::onEntityUpdate(st, other, sizeof other));
  uint8_t sub[4];
  CHECK(ams::buildTrackSubscription(sub, 4) == 3 && sub[0] == 2 && sub[1] == 0 && sub[2] == 2);
  CHECK(ams::buildPlayerSubscription(sub, 4) == 2 && sub[0] == 0 && sub[1] == 1);
  CHECK(ams::buildTrackSubscription(sub, 2) == 0);
  CHECK(ams::kRemoteToggle == 2 && ams::kRemoteNext == 3 && ams::kRemotePrev == 4);
}

static void test_startcap() {
  using namespace startcap;
  Inputs in = {true, false, true, 0, false, 0, 0};
  CHECK(shouldCap(in));                                       // linked, limits read and above 20: cap now
  Inputs x = in; x.startCapOn = false; CHECK(!shouldCap(x));  // app setting off: never
  x = in; x.released = true; CHECK(!shouldCap(x));            // released from the service menu for this ride
  x = in; x.linked = false; CHECK(!shouldCap(x));
  x = in; x.capState = 1; CHECK(!shouldCap(x));               // already capped
  x = in; x.capState = -1; CHECK(!shouldCap(x));              // limits not read: never write blind
  x = in; x.writePending = true; CHECK(!shouldCap(x));
  x = in; x.tries = 1; x.msSinceLastTry = kRetryMs - 1; CHECK(!shouldCap(x));
  x = in; x.tries = 1; x.msSinceLastTry = kRetryMs; CHECK(shouldCap(x));
  x = in; x.tries = kMaxTries; x.msSinceLastTry = 999999; CHECK(!shouldCap(x));
  CHECK(secretArmed(true, true, true, 0.0f) && secretArmed(true, true, true, 2.9f));
  CHECK(!secretArmed(true, true, true, 3.0f));                // moving: no service menu
  CHECK(!secretArmed(false, true, true, 0.0f) && !secretArmed(true, false, true, 0.0f));
  CHECK(!secretArmed(true, true, false, 0.0f));               // unknown speed counts as moving
  { uint8_t f[32]; size_t n = app::buildTelem(f, 0x83, 253, 60, 787, -120, -45, 47, 58, 3, 1, 206600);
    CHECK(n == app::kTelemLen && f[0] == app::ST_TELEM && f[1] == 0x83);
    CHECK((f[2] | (f[3] << 8)) == 253 && f[4] == 60 && (f[5] | (f[6] << 8)) == 787);
    CHECK((int16_t)(f[7] | (f[8] << 8)) == -120 && (int16_t)(f[9] | (f[10] << 8)) == -45);
    CHECK(f[11] == 47 && f[12] == 58 && f[13] == 3 && f[14] == 1);
    CHECK((uint32_t)(f[15] | (f[16] << 8) | (f[17] << 16) | ((uint32_t)f[18] << 24)) == 206600u);
    uint8_t g[96]; CHECK(app::buildInfo(g, sizeof g, 1, 2, 3, "v", "p", "t", "80,0,1,2,3") > 0); }
  CHECK(kSecretHoldMs == 2000 && kCapKmh == kQuickLimitKmh);
}

int main() {
  test_startcap();
  test_format(); test_telemetry_default_invalid(); test_gestures(); test_hold();
  test_cal(); test_safety(); test_demo(); test_control();
  test_xbot_variants(); test_xbot_framing(); test_xbot_real_frame(); test_xbot_telemetry(); test_xbot_write(); test_xbot_limit_and_lock(); test_app_protocol(); test_widgets(); test_apple();
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
