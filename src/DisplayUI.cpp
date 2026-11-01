#include "DisplayUI.h"
#include "DefaultLayout.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <esp_system.h>
#include "BoardConfig.h"
#include "Format.h"
#include "ScooterProtocol.h"
#include "Diagnostics.h"
#include "AppleServices.h"

namespace {
const int16_t kW = 320;
const int16_t kH = 240;
const uint32_t kHoldMs = 1500;

DisplayUI::Btn mk(int16_t x, int16_t y, int16_t w, int16_t h) {
  DisplayUI::Btn b; b.x = x; b.y = y; b.w = w; b.h = h; return b;
}
DisplayUI::Btn bottomBtn(int i) { return mk((int16_t)(8 + i * 104), 206, 96, 30); }
DisplayUI::Btn dashBtn(int i) { return mk((int16_t)(6 + i * 80), 206, 72, 30); }
DisplayUI::Btn gridBtn(int i) { return mk((int16_t)(6 + (i % 4) * 79), (int16_t)(36 + (i / 4) * 44), 74, 40); }
DisplayUI::Btn holdBtn() { return mk(40, 150, 240, 40); }
DisplayUI::Btn hwBtn(int i) { return mk((int16_t)(6 + i * 79), 206, 74, 30); }
DisplayUI::Btn cardBtn(int i) { return mk((int16_t)(8 + (i % 3) * 104), (int16_t)(40 + (i / 3) * 54), 100, 50); }
bool inBtn(const DisplayUI::Btn& b, int16_t x, int16_t y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}
}  // namespace

// ------------------------------------------------------------------ lifecycle

void DisplayUI::begin(const UiContext& ctx) {
  wr_.attach(&tft_);
  c_ = ctx;
  memset(cache_, 0, sizeof(cache_));
  memset(cacheCol_, 0, sizeof(cacheCol_));
  calMsg_[0] = 0;
  tft_.init();
  applyTheme(c_.store->get().theme);
  bootImgOk_ = c_.store->loadBootImg(bootImg_);
  reloadLayout();
  applyDisplaySettings();
  splash();

  const Settings& s = c_.store->get();
  hwPage_ = 0;
  if (!cal_.valid) startCal(s.hwConfirmed ? Screen::Dash : Screen::Hw);
  else if (!s.hwConfirmed) go(Screen::Hw);
  else go(Screen::Dash);
}

void DisplayUI::go(Screen s) {
  if (screen_ == Screen::Ctrl && s != Screen::Ctrl) c_.ble->setWritesUnlocked(false);  // leaving CTRL relocks
  screen_ = s;
  full_ = true;
  gest_ = GestureDetector();
  hold_ = HoldTracker();
}

void DisplayUI::setBacklight(uint8_t pct) {
  static bool attached = false;
  if (!attached) {
    ledcSetup(0, 5000, 8);
    ledcAttachPin(board::kBacklightPin, 0);
    attached = true;
  }
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  ledcWrite(0, (uint32_t)pct * 255u / 100u);
}

void DisplayUI::applyDisplaySettings() {
  const Settings& s = c_.store->get();
  applyColorMode();               // sets the rotation (and, on ST7789, the colour order)
  tft_.invertDisplay(s.invert);
  setBacklight(s.backlightPct);
  TouchCal tc;
  if (c_.store->loadCal(s.rotation, tc)) cal_ = tc; else cal_ = TouchCal();
}

void DisplayUI::applyTheme(uint8_t idx) {
  if (idx >= kThemeCount) idx = 0;
  const ThemeDef& t = kThemes[idx];
  cBg_ = tft_.color565(t.bg.r, t.bg.g, t.bg.b);
  cPanel_ = tft_.color565(t.panel.r, t.panel.g, t.panel.b);
  cNeon_ = tft_.color565(t.neon.r, t.neon.g, t.neon.b);
  cGlow_ = tft_.color565(t.glow.r, t.glow.g, t.glow.b);
  cDim_ = tft_.color565(t.dim.r, t.dim.g, t.dim.b);
  cWarn_ = tft_.color565(t.warn.r, t.warn.g, t.warn.b);
  cOk_ = tft_.color565(t.ok.r, t.ok.g, t.ok.b);
  cBad_ = tft_.color565(t.bad.r, t.bad.g, t.bad.b);
  cText_ = tft_.color565(t.text.r, t.text.g, t.text.b);
  if (idx == kThemeCount - 1) {                          // CUSTOM: accent colour chosen in the phone app
    const Settings& cs = c_.store->get();
    cNeon_ = tft_.color565(cs.accent[0], cs.accent[1], cs.accent[2]);
    cGlow_ = mix(cBg_, cNeon_, 130);
  }
  cArc_ = cNeon_;
  cArcHi_ = mix(cNeon_, 0xFFFF, 140);
  cTrk_ = mix(cBg_, cText_, 50);
  cEdge_ = mix(cBg_, cText_, 128);
  cBtn_ = mix(cBg_, cText_, 225);
  cTag_ = mix(cBg_, cNeon_, 90);
  WPalette wp = {cText_, cNeon_, cOk_, cWarn_, cBad_, cDim_, cPanel_, cBg_, cGlow_};
  wr_.setPalette(wp);
}

void DisplayUI::splash() {
  const Settings& s = c_.store->get();
  uint8_t style = s.bootStyle;
  if (style == 2 && !bootImgOk_) style = 0;  // no stored image: classic screen
  tft_.fillScreen(cBg_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(MC_DATUM);
  if (style == 0) {
    tft_.setTextColor(cNeon_, cBg_);
    tft_.setTextSize(2);
    tft_.drawString("NOVA", 160, 80, 4);
    tft_.setTextSize(1);
    tft_.setTextColor(cText_, cBg_);
    tft_.drawString("RIDE OS", 160, 130, 4);
  } else if (style == 1) {
    const char* name = s.bootText[0] ? s.bootText : "NOVA";
    tft_.setTextColor(cNeon_, cBg_);
    tft_.setTextSize(strlen(name) <= 8 ? 2 : 1);
    tft_.drawString(name, 160, 80, 4);
    tft_.setTextSize(1);
    tft_.setTextColor(cDim_, cBg_);
    tft_.drawString("RIDE OS", 160, 130, 2);
  } else if (style == 2) {
    tft_.drawBitmap(96, 30, bootImg_, app::kBootImgW, app::kBootImgH, cNeon_);
    tft_.setTextColor(cText_, cBg_);
    tft_.drawString(s.bootText[0] ? s.bootText : "NOVA RIDE OS", 160, 125, 4);
  }
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(NOVA_FW_VERSION " / " NOVA_PROFILE_NAME, 160, 205, 2);
  for (int x = 40; x <= 280; x += 8) {
    tft_.drawFastHLine(40, 160, x - 40, cNeon_);
    delay(6);
  }
  delay(250);
}

void DisplayUI::toggleInvert() {
  Settings& s = c_.store->get();
  s.invert = !s.invert;
  tft_.invertDisplay(s.invert);
  c_.store->save();
  full_ = true;
}

void DisplayUI::flipRotation() {
  Settings& s = c_.store->get();
  s.rotation = (s.rotation == 1) ? 3 : 1;
  c_.store->save();
  applyDisplaySettings();
  const Screen ret = (screen_ == Screen::Cal) ? calReturn_ : screen_;
  if (cal_.valid) go(ret); else startCal(ret);
}

void DisplayUI::onBootButton(bool longPress) {
  if (longPress) flipRotation(); else toggleInvert();
}

void DisplayUI::startCal(Screen returnTo) {
  calReturn_ = returnTo;
  screen_ = Screen::Cal;
  full_ = true;
  calStep_ = 0;
  calTouching_ = false;
  calWaitRelease_ = true;  // require a release before the first target counts
  gest_ = GestureDetector();
}

// ------------------------------------------------------------------ drawing helpers

void DisplayUI::txt(uint8_t slot, int16_t x, int16_t y, uint8_t font, const char* s,
                    uint16_t fg, uint8_t datum, int16_t padW) {
  if (slot >= 32) return;
  if (strcmp(cache_[slot], s) == 0 && cacheCol_[slot] == fg) return;
  tft_.setTextColor(fg, cBg_);
  tft_.setTextDatum(datum);
  tft_.setTextPadding(padW);
  tft_.drawString(s, x, y, font);
  tft_.setTextPadding(0);
  strlcpy(cache_[slot], s, sizeof(cache_[slot]));
  cacheCol_[slot] = fg;
}

uint16_t DisplayUI::mix(uint16_t a, uint16_t b, uint8_t t) const {
  const int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  const int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  const int r = ar + ((br - ar) * (int)t) / 255, g = ag + ((bg - ag) * (int)t) / 255, bl = ab + ((bb - ab) * (int)t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Rotation first (the library default), then on ST7789 an explicit colour order if one was chosen.
void DisplayUI::applyColorMode() {
  const Settings& s = c_.store->get();
  tft_.setRotation(s.rotation);
#if defined(ST7789_DRIVER)
  if (s.colorMode != 0) {
    const uint8_t base = (s.rotation == 1) ? 0x60 : 0xA0;   // MX|MV and MV|MY: the landscape values TFT_eSPI writes
    tft_.writecommand(0x36);                                 // MADCTL
    tft_.writedata((uint8_t)(base | (s.colorMode == 1 ? 0x08 : 0x00)));
  }
#endif
}

void DisplayUI::toggleColors() {
  Settings& s = c_.store->get();
  s.colorMode = (uint8_t)((s.colorMode + 1) % 3);
  applyColorMode();
  c_.store->save();
  full_ = true;
}

// Rounded button with a soft highlight. "selected" fills it with the accent colour.
void DisplayUI::drawBtn(const Btn& b, const char* label, bool enabled, uint16_t accent, bool selected) {
  // outlined, anti-aliased button (same look as the dashboard): accent colour only tints the edge
  const uint16_t fill = selected ? accent : cBg_;
  const uint16_t edge = enabled ? (selected ? accent : mix(cBtn_, accent, 90)) : mix(cBg_, cDim_, 140);
  const int r = b.h >= 40 ? 13 : 9;
  tft_.fillSmoothRoundRect(b.x, b.y, b.w, b.h, r, edge, cBg_);
  tft_.fillSmoothRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, r - 2, fill, edge);
  tft_.setTextColor(!enabled ? cDim_ : (selected ? cBg_ : cText_), fill);
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextPadding(0);
  tft_.drawString(label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

// Settings card: title on top, current value (coloured) below.
void DisplayUI::drawCard(const Btn& b, const char* title, const char* value, uint16_t valueCol) {
  tft_.fillSmoothRoundRect(b.x, b.y, b.w, b.h, 10, cEdge_, cBg_);
  tft_.fillSmoothRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 9, cPanel_, cEdge_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(cText_, cPanel_);
  tft_.drawString(title, b.x + 10, b.y + 8, 2);
  tft_.setTextColor(valueCol, cPanel_);
  tft_.drawString(value, b.x + 10, b.y + 28, 2);
}

void DisplayUI::drawHeader(const char* title) {
  tft_.fillScreen(cBg_);
  tft_.setTextDatum(TC_DATUM);
  tft_.setTextPadding(0);
  tft_.setTextColor(cNeon_, cBg_);
  tft_.drawString(title, 160, 3, 4);
  tft_.fillSmoothRoundRect(110, 31, 100, 3, 1, cNeon_, cBg_);              // short accent line under the title
}

void DisplayUI::drawFull() {
  memset(cache_, 0, sizeof(cache_));
  memset(cacheCol_, 0, sizeof(cacheCol_));
  switch (screen_) {
    case Screen::Cal: drawCal(); break;
    case Screen::Hw: drawHw(); break;
    case Screen::Dash: drawDash(); break;
    case Screen::Music: drawMusic(); break;
    case Screen::Msg: drawMsg(); break;
    case Screen::Settings: drawSettings(); break;
    case Screen::Safety: drawSafety(); break;
    case Screen::Diag: drawDiag(); break;
    case Screen::Ctrl: drawCtrl(); break;
    case Screen::Update: drawUpdate(); break;
    case Screen::Secret: drawSecret(); break;
  }
}

// ------------------------------------------------------------------ main update

void DisplayUI::update(const Telemetry& t, const TouchState& ts, uint32_t now) {
  const applink::Applied ap = applink::applyPending();
  if (ap.theme) { applyTheme(c_.store->get().theme); full_ = true; }
  if (ap.boot) bootImgOk_ = c_.store->loadBootImg(bootImg_);
  if (ap.layout) { reloadLayout(); full_ = true; }
  if (ap.startCap) onStartCapChanged();
  if (ap.display) { setBacklight(c_.store->get().backlightPct); applyTheme(c_.store->get().theme); full_ = true; }
  if (ap.boot || ap.preview) { splash(); full_ = true; }
  if (screen_ == Screen::Cal) { tickCal(ts, now); return; }
  capTick(now);

  int16_t mx = 0, my = 0;
  const bool touching = ts.down && cal_.valid;
  if (touching) cal_.map(ts.rawX, ts.rawY, kW, kH, mx, my);

  if (full_) { drawFull(); full_ = false; }
  const GestureEvent ev = gest_.update(touching, mx, my, now);

  switch (screen_) {
    case Screen::Hw: tickHw(touching, mx, my, ev); break;
    case Screen::Dash: tickDash(t, touching, mx, my, ev, now); break;
    case Screen::Music: tickMusic(ev); break;
    case Screen::Msg: tickMsg(ev); break;
    case Screen::Settings: tickSettings(ev); break;
    case Screen::Safety: tickSafety(touching, mx, my, ev, now); break;
    case Screen::Diag: tickDiag(ev, now); break;
    case Screen::Ctrl: tickCtrl(touching, mx, my, ev, now); break;
    case Screen::Update: tickUpdate(touching, mx, my, ev, now); break;
    case Screen::Secret: tickSecret(touching, mx, my, ev, now); break;
    default: break;
  }
}

// ------------------------------------------------------------------ calibration

void DisplayUI::drawCalTarget() {
  int16_t tx = 20, ty = 20;
  if (calStep_ == 1) { tx = kW - 20; ty = 20; }
  if (calStep_ == 2) { tx = kW - 20; ty = kH - 20; }
  tft_.drawFastHLine(tx - 14, ty, 29, cNeon_);
  tft_.drawFastVLine(tx, ty - 14, 29, cNeon_);
  tft_.drawCircle(tx, ty, 7, cNeon_);
  tft_.fillCircle(tx, ty, 2, cText_);
}

void DisplayUI::drawCal() {
  tft_.fillScreen(cBg_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(cText_, cBg_);
  char b[32];
  snprintf(b, sizeof(b), "TOUCH CALIBRATION %u/3", (unsigned)(calStep_ + 1));
  tft_.drawString(b, 160, 100, 2);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString("Tap the centre of the crosshair", 160, 122, 2);
  tft_.drawString("BOOT: invert (short) / flip (long)", 160, 142, 2);
  if (calMsg_[0]) {
    tft_.setTextColor(cBad_, cBg_);
    tft_.drawString(calMsg_, 160, 170, 2);
  }
  drawCalTarget();
}

void DisplayUI::tickCal(const TouchState& ts, uint32_t now) {
  if (full_) { drawCal(); full_ = false; }
  if (calWaitRelease_) { if (!ts.down) calWaitRelease_ = false; return; }
  if (!ts.down) { calTouching_ = false; return; }
  if (!calTouching_) { calTouching_ = true; calT0_ = now; calSumX_ = 0; calSumY_ = 0; calN_ = 0; }
  calSumX_ += ts.rawX; calSumY_ += ts.rawY; ++calN_;
  if (now - calT0_ < 150 || calN_ <= 0) return;

  calPts_[calStep_].x = (int16_t)(calSumX_ / calN_);
  calPts_[calStep_].y = (int16_t)(calSumY_ / calN_);
  calTouching_ = false;
  calWaitRelease_ = true;
  if (++calStep_ < 3) { full_ = true; return; }

  TouchCal tc;
  if (TouchCal::compute(calPts_[0], calPts_[1], calPts_[2], kW, kH, 20, tc)) {
    cal_ = tc;
    c_.store->saveCal(c_.store->get().rotation, tc);
    calMsg_[0] = 0;
    Diag::log("cal ok swap=%d", tc.swapXY ? 1 : 0);
    go(calReturn_);
  } else {
    strlcpy(calMsg_, "Calibration failed - try again", sizeof(calMsg_));
    Diag::log("cal failed");
    calStep_ = 0;
    full_ = true;
  }
}

// ------------------------------------------------------------------ hardware test

void DisplayUI::hwButtons() {
  const char* l[4] = {"", "", "", ""};
  switch (hwPage_) {
    case 0: case 1: l[0] = "INVERT"; l[1] = "COLORS"; l[2] = "FLIP"; l[3] = "NEXT"; break;
    case 2: l[0] = "CALIB"; l[1] = "CLEAR"; l[3] = "NEXT"; break;
    default: l[0] = "BACK"; l[3] = "ACCEPT"; break;
  }
  for (int i = 0; i < 4; ++i)
    if (l[i][0]) drawBtn(hwBtn(i), l[i], true, (hwPage_ == 3 && i == 3) ? cOk_ : cNeon_);
}

void DisplayUI::drawHw() {
  tft_.fillScreen(cBg_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(cText_, cBg_);
  char h[48];
  switch (hwPage_) {
    case 0: {
      snprintf(h, sizeof(h), "HW 1/4 COLORS  %s", NOVA_PROFILE_NAME);
      tft_.drawString(h, 4, 4, 2);
      const uint16_t cols[8] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK, TFT_CYAN, TFT_MAGENTA, TFT_YELLOW};
      const char* names[8] = {"RED", "GRN", "BLU", "WHT", "BLK", "CYN", "MAG", "YEL"};
      for (int i = 0; i < 8; ++i) tft_.fillRect(i * 40, 24, 40, 126, cols[i]);
      tft_.drawRect(4 * 40, 24, 40, 126, cDim_);
      tft_.setTextDatum(TC_DATUM);
      for (int i = 0; i < 8; ++i) tft_.drawString(names[i], i * 40 + 20, 154, 2);
      tft_.setTextDatum(TL_DATUM);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Negative colours? tap INVERT", 4, 174, 2);
      tft_.drawString("RED looks blue? tap COLORS", 4, 190, 2);
      break;
    }
    case 1: {
      tft_.drawString("HW 2/4 ORIENTATION", 4, 4, 2);
      tft_.drawRect(0, 0, kW, kH, cNeon_);
      tft_.drawRect(1, 1, kW - 2, kH - 2, cGlow_);
      tft_.drawLine(0, 0, kW - 1, kH - 1, cGlow_);
      tft_.drawLine(kW - 1, 0, 0, kH - 1, cGlow_);
      tft_.setTextColor(cText_, cBg_);
      tft_.drawString("TL", 8, 24, 2);
      tft_.setTextDatum(TR_DATUM); tft_.drawString("TR", kW - 8, 24, 2);
      tft_.setTextDatum(TL_DATUM); tft_.drawString("BL", 8, 180, 2);
      tft_.setTextDatum(TR_DATUM); tft_.drawString("BR", kW - 8, 180, 2);
      tft_.setTextDatum(MC_DATUM);
      tft_.setTextColor(cNeon_, cBg_);
      tft_.drawString("NOVA 123", 160, 100, 4);
      char d[32];
      snprintf(d, sizeof(d), "%dx%d rot %u", (int)tft_.width(), (int)tft_.height(),
               (unsigned)c_.store->get().rotation);
      tft_.setTextColor(cText_, cBg_);
      tft_.drawString(d, 160, 132, 2);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Text must read normally", 160, 152, 2);
      break;
    }
    case 2:
      tft_.drawString("HW 3/4 TOUCH", 4, 4, 2);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Touch the screen: dot must follow finger", 4, 24, 2);
      tft_.drawString("Wrong? CALIBRATE again (BOOT = flip)", 4, 44, 2);
      break;
    default:
      tft_.drawString("HW 4/4 ACCEPT", 4, 4, 2);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Colours, orientation, touch all OK?", 4, 34, 2);
      tft_.drawString("ACCEPT stores this profile on the device.", 4, 54, 2);
      tft_.setTextColor(cWarn_, cBg_);
      tft_.drawString("Scooter link: NOT SUPPORTED", 4, 94, 2);
      tft_.drawString("Dashboard starts in DEMO mode.", 4, 114, 2);
      break;
  }
  hwButtons();
}

void DisplayUI::tickHw(bool touching, int16_t mx, int16_t my, const GestureEvent& ev) {
  Settings& s = c_.store->get();
  if (hwPage_ == 2 && touching && my < 200) {
    tft_.fillCircle(mx, my, 3, cNeon_);
    char b[44];
    snprintf(b, sizeof(b), "map %3d,%3d  rot %u", (int)mx, (int)my, (unsigned)s.rotation);
    txt(0, 8, 176, 2, b, cText_, TL_DATUM, 300);
  }
  if (ev.type != GestureType::Tap) return;
  int idx = -1;
  for (int i = 0; i < 4; ++i) if (inBtn(hwBtn(i), ev.x, ev.y)) idx = i;
  if (idx < 0) return;
  switch (hwPage_) {
    case 0: case 1:
      if (idx == 0) toggleInvert();
      else if (idx == 1) toggleColors();
      else if (idx == 2) flipRotation();
      else { ++hwPage_; full_ = true; }
      break;
    case 2:
      if (idx == 0) startCal(Screen::Hw);
      else if (idx == 1) full_ = true;
      else if (idx == 3) { hwPage_ = 3; full_ = true; }
      break;
    default:
      if (idx == 0) { hwPage_ = 0; full_ = true; }
      else if (idx == 3) { s.hwConfirmed = true; c_.store->save(); go(Screen::Dash); }
      break;
  }
}

// ------------------------------------------------------------------ dashboard

namespace {
const int kGcx = 88, kGcy = 126, kGr0 = 66, kGr1 = 80, kSegs = 54;   // speed ring: 270 degrees in 54 segments of 5 degrees
const uint32_t kModeHoldMs = 800;
const float kPowerBarMaxW = 1000.0f;   // full scale of the power bar
const char* const kModeLabel[4] = {"WALK", "ECO", "DRIVE", "SPORT"};
DisplayUI::Btn setBtn() { return mk(266, 3, 48, 22); }
}  // namespace

void DisplayUI::drawSegAt(int cx, int cy, int r0, int r1, int i, uint16_t col) {
  const float k = 0.0174533f;
  const float a0 = (135.0f + 5.0f * (float)i) * k;
  const float a1 = (135.0f + 5.0f * (float)i + 5.8f) * k;   // slight overlap: no hairline gaps
  const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
  const int x0 = cx + (int)lroundf((float)r0 * c0), y0 = cy + (int)lroundf((float)r0 * s0);
  const int x1 = cx + (int)lroundf((float)r1 * c0), y1 = cy + (int)lroundf((float)r1 * s0);
  const int x2 = cx + (int)lroundf((float)r1 * c1), y2 = cy + (int)lroundf((float)r1 * s1);
  const int x3 = cx + (int)lroundf((float)r0 * c1), y3 = cy + (int)lroundf((float)r0 * s1);
  tft_.fillTriangle(x0, y0, x1, y1, x2, y2, col);
  tft_.fillTriangle(x0, y0, x2, y2, x3, y3, col);
}

void DisplayUI::drawSeg(int i, bool on) {
  drawSegAt(kGcx, kGcy, kGr0, kGr1, i, on ? (i >= 44 ? cWarn_ : cNeon_) : cPanel_);
}

void DisplayUI::modeState(uint32_t now, int& active, bool& enabled) {
  const bool demo = c_.store->get().demo;
  const bool linked = !demo && c_.ble->state() == BleState::Connected;
  xbot::Readback rb;
  const bool have = linked && c_.ble->getReadback(rb, now);
  active = -1;
  if (demo) active = (int)c_.ctrl->mode();
  else if (have && rb.modeValid) active = (int)xbot::internalFromModeIndex(rb.modeIdx);
  enabled = demo || (linked && have && rb.modeValid);
}

void DisplayUI::drawModeButtons() {
  int active;
  bool en;
  modeState(millis(), active, en);
  modeKey_ = active * 2 + (en ? 1 : 0);
  for (int i = 0; i < 4; ++i) {
    const Btn b = dashBtn(i);
    drawBtn(b, kModeLabel[i], en, i == active ? cOk_ : cNeon_, i == active && en);
  }
  holdModeDrawn_ = 0;
}

void DisplayUI::drawMediaButtons() {
  const MediaInfo mi = c_.phone->info();
  const bool en = mi.link == PhoneLink::Connected;
  const int unread = c_.notes->unread();
  mediaKey_ = (en ? 1 : 0) + (mi.playing ? 2 : 0) + unread * 4;
  drawBtn(dashBtn(0), "PREV", en, cNeon_);
  drawBtn(dashBtn(1), mi.playing ? "PAUSE" : "PLAY", en, cNeon_, mi.playing);
  drawBtn(dashBtn(2), "NEXT", en, cNeon_);
  char l[12];
  if (unread > 0) snprintf(l, sizeof(l), "MSG %d", unread); else snprintf(l, sizeof(l), "MSG");
  drawBtn(dashBtn(3), l, true, unread > 0 ? cWarn_ : cNeon_, unread > 0);
}

void DisplayUI::drawBottom() {
  if (bottomPage_ == 0) drawModeButtons(); else drawMediaButtons();
  tft_.fillCircle(152, 238, 1, bottomPage_ == 0 ? cNeon_ : cDim_);   // page dots
  tft_.fillCircle(168, 238, 1, bottomPage_ == 1 ? cNeon_ : cDim_);
  holdModeDrawn_ = 0;
}

void DisplayUI::drawDash() {
  if (useCustom()) { tft_.fillScreen(cBg_); wr_.invalidate(); custBtn_ = -1; return; }   // the layout is drawn by the first tick
  drawDashRef();
}

void DisplayUI::tickDash(const Telemetry& t, bool touching, int16_t mx, int16_t my,
                         const GestureEvent& ev, uint32_t now) {
  if (useCustom()) tickCustom(t, touching, mx, my, ev, now);
  else tickDashRef(t, touching, mx, my, ev, now);
}

void DisplayUI::tickDashInput(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now, bool showModes) {
  if (showModes && bottomPage_ == 0) {
    // mode buttons: a short deliberate hold (0.8 s) changes the mode
    int active;
    bool en;
    modeState(now, active, en);
    if (active * 2 + (en ? 1 : 0) != modeKey_) drawModeButtons();
    int inside = -1;
    if (touching && en) for (int i = 0; i < 4; ++i) if (inBtn(dashBtn(i), mx, my)) inside = i;
    if (inside != holdModeIdx_) { holdModeIdx_ = inside; holdMode_ = HoldTracker(); if (holdModeDrawn_ != 0) drawModeButtons(); }
    if (inside >= 0 && inside != active) {
      if (holdMode_.update(true, now, kModeHoldMs)) {
        const bool demo = c_.store->get().demo;
        if (demo) c_.ctrl->setMode((RideMode)inside, true);
        else c_.ble->requestWriteOnce(WriteKind::Mode, xbot::modeIndexFromInternal((uint8_t)inside));
        drawModeButtons();
      } else {
        const Btn bb = dashBtn(inside);
        const int w = (int)(holdMode_.progress(now, kModeHoldMs) * (float)(bb.w - 8));
        if (w != holdModeDrawn_) {
          if (w > holdModeDrawn_) tft_.fillRect(bb.x + 4 + holdModeDrawn_, bb.y + bb.h - 6, w - holdModeDrawn_, 3, cNeon_);
          holdModeDrawn_ = w;
        }
      }
    }
  } else if (showModes && bottomPage_ == 1) {
    // music buttons: plain taps (harmless)
    const MediaInfo mi = c_.phone->info();
    const int key = (mi.link == PhoneLink::Connected ? 1 : 0) + (mi.playing ? 2 : 0) + c_.notes->unread() * 4;
    if (key != mediaKey_) drawMediaButtons();
    if (ev.type == GestureType::Tap) {
      if (inBtn(dashBtn(0), ev.x, ev.y)) c_.phone->prev();
      else if (inBtn(dashBtn(1), ev.x, ev.y)) c_.phone->playPause();
      else if (inBtn(dashBtn(2), ev.x, ev.y)) c_.phone->next();
      else if (inBtn(dashBtn(3), ev.x, ev.y)) { go(Screen::Msg); return; }
    }
  }
  // gestures that work whatever the layout: swipe up = safety, SET = settings
  if (ev.type == GestureType::SwipeUp) { go(Screen::Safety); return; }
  if (ev.type == GestureType::SwipeLeft || ev.type == GestureType::SwipeRight) {
    if (showModes && ev.y >= 198) {                      // swipe along the button row: mode buttons <-> music buttons
      bottomPage_ = (uint8_t)(bottomPage_ ^ 1);
      holdModeIdx_ = -1;
      holdMode_ = HoldTracker();
      drawBottom();
      return;
    }
    go(ev.type == GestureType::SwipeLeft ? Screen::Music : Screen::Msg);
    return;
  }
  if (ev.type == GestureType::Tap && inBtn(setBtn(), ev.x, ev.y)) go(Screen::Settings);
}


// ------------------------------------------------------------------ reference-style dashboard
// Arc gauge with a knob, pill bars with an edge, outlined buttons. Uses the smooth (anti-aliased)
// shapes of TFT_eSPI. Arc angles: TFT_eSPI counts 0 at 6 o'clock, clockwise, so the 270-degree
// gauge runs from 45 to 315 (gap at the bottom).

namespace {
const int kRCx = 90, kRCy = 104, kRRo = 82, kRRi = 73;   // arc centre, outer and inner radius
const float kRMinMaxKmh = 35.0f;                         // smallest full-scale of the arc
const int kBarX = 176, kBarW = 138, kBarInner = kBarW - 4;
const uint32_t kRowHoldMs = 800;
DisplayUI::Btn sportBtn() { return mk(178, 175, 97, 47); }
DisplayUI::Btn raceBtn() { return mk(281, 175, 33, 47); }
DisplayUI::Btn rowBtn(int i) { return mk((int16_t)(178 + (i % 2) * 69), (int16_t)(175 + (i / 2) * 25), 67, 22); }
DisplayUI::Btn setZone() { return mk(262, 0, 58, 28); }
}  // namespace

void DisplayUI::drawRefBtn(const Btn& b, const char* label, uint8_t font, uint16_t edge, uint16_t fill, uint16_t txtc, int r) {
  tft_.fillSmoothRoundRect(b.x, b.y, b.w, b.h, r, edge, cBg_);
  tft_.fillSmoothRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, r - 2, fill, edge);
  tft_.setTextColor(txtc, fill);
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextPadding(0);
  tft_.drawString(label, b.x + b.w / 2, b.y + b.h / 2 + 1, font);
}

// Pill bar with a lighter edge. The fill covers pixels fx0..fx1 of the inside (0..kBarInner).
void DisplayUI::drawRefBar(int y, int h, int fx0, int fx1, uint16_t col) {
  tft_.fillSmoothRoundRect(kBarX, y, kBarW, h, h / 2, cEdge_, cBg_);
  tft_.fillSmoothRoundRect(kBarX + 1, y + 1, kBarW - 2, h - 2, (h - 2) / 2, cTrk_, cEdge_);
  const int w = fx1 - fx0;
  if (w >= 2) {
    const int fh = h - 4;
    int r = w / 2;
    if (r > fh / 2) r = fh / 2;
    tft_.fillSmoothRoundRect(kBarX + 2 + fx0, y + 2, w, fh, r, col, cTrk_);
  }
}

// Paints the part [a0, a1] of the arc: light up to fillA, track colour after it.
void DisplayUI::paintArc(int a0, int a1, int fillA) {
  if (a0 < 45) a0 = 45;
  if (a1 > 315) a1 = 315;
  if (a1 <= a0) return;
  const int split = fillA < a0 ? a0 : (fillA > a1 ? a1 : fillA);
  if (split > a0) tft_.drawSmoothArc(kRCx, kRCy, kRRo, kRRi, a0, split, cArcHi_, cBg_, false);
  if (a1 > split) tft_.drawSmoothArc(kRCx, kRCy, kRRo, kRRi, split, a1, cArc_, cBg_, false);
}

void DisplayUI::drawKnob(int fillA) {
  const float a = (135.0f + (float)(fillA - 45)) * 0.0174533f;   // screen angle of the knob
  const float rm = (float)(kRRo + kRRi) * 0.5f;
  tft_.fillSmoothCircle(kRCx + (int)lroundf(rm * cosf(a)), kRCy + (int)lroundf(rm * sinf(a)), 7, cText_, cArcHi_);
}

// Small tick on the arc at 20 km/h while the start cap is active.
void DisplayUI::drawCapNotch() {
  const float a = (135.0f + 270.0f * (20.0f / arcMax_)) * 0.0174533f;
  const float c = cosf(a), sn = sinf(a);
  tft_.drawWideLine((float)kRCx + (float)(kRRi - 6) * c, (float)kRCy + (float)(kRRi - 6) * sn,
                    (float)kRCx + (float)(kRRo + 6) * c, (float)kRCy + (float)(kRRo + 6) * sn, 3.0f, cWarn_, cBg_);
}

void DisplayUI::drawDashRef() {
  tft_.fillScreen(cBg_);
  tft_.setTextPadding(0);
  tft_.drawSmoothArc(kRCx, kRCy, kRRo, kRRi, 45, 315, cArc_, cBg_, true);   // track with round ends
  arcFill_ = -1;                                                           // fill + knob are painted by tickDashRef
  tft_.setTextColor(cDim_, cBg_);
  tft_.setTextDatum(MC_DATUM);
  tft_.drawString("km/h", kRCx, 158, 2);
  tft_.setTextDatum(TL_DATUM);
  tft_.drawString("W", 296, 52, 2);
  for (int i = 0; i < 3; ++i) tft_.fillCircle(298 + i * 6, 9, 1, cDim_);     // SET: tap the three dots
  dotCol_ = 0;
  barPwKey_ = -99999;
  barBtKey_ = -99999;
  drawRefBar(87, 6, 0, 0, cBtn_);
  drawRefBar(133, 14, 0, 0, cBtn_);
  rowKey_ = 0xFFFFFFFFu;
  drawRow();
}

// Button area at the bottom right: page 0 SPORT + RACE, page 1 the four modes, page 2 music.
void DisplayUI::drawRow() {
  tft_.fillRect(172, 170, 148, 70, cBg_);
  int active;
  bool en;
  modeState(millis(), active, en);
  const MediaInfo mi = c_.phone->info();
  const bool me = mi.link == PhoneLink::Connected;
  const int unread = c_.notes->unread();
  rowKey_ = (uint32_t)rowPage_ | ((uint32_t)(active + 1) << 2) | (en ? 32u : 0u) | ((uint32_t)(capState_ + 1) << 6) |
            (capReleased_ ? 256u : 0u) | (me ? 512u : 0u) | (mi.playing ? 1024u : 0u) | ((uint32_t)unread << 11);
  if (rowPage_ == 0) {
    const bool sel = en && active == 3;
    drawRefBtn(sportBtn(), "SPORT", 4, cBtn_, sel ? cArc_ : cBg_, sel ? cBg_ : cText_, 13);
    const bool lit = capReleased_ && capState_ != 1;                       // limit released: RACE lights up
    drawRefBtn(raceBtn(), "RACE", 1, lit ? cBad_ : mix(cBg_, cBad_, 130), mix(cBg_, cBad_, lit ? 110 : 55),
               lit ? cText_ : mix(cBad_, cText_, 110), 11);
  } else if (rowPage_ == 1) {
    static const char* const nm[4] = {"WALK", "ECO", "DRIVE", "SPORT"};
    for (int i = 0; i < 4; ++i) {
      const bool sel = en && active == i;
      drawRefBtn(rowBtn(i), nm[i], 2, en ? cBtn_ : mix(cBg_, cDim_, 120), sel ? cArc_ : cBg_, !en ? cDim_ : (sel ? cBg_ : cText_), 9);
    }
  } else {
    char l[12];
    if (unread > 0) snprintf(l, sizeof(l), "MSG %d", unread); else snprintf(l, sizeof(l), "MSG");
    drawRefBtn(rowBtn(0), "PREV", 2, me ? cBtn_ : mix(cBg_, cDim_, 120), cBg_, me ? cText_ : cDim_, 9);
    drawRefBtn(rowBtn(1), mi.playing ? "PAUSE" : "PLAY", 2, me ? cBtn_ : mix(cBg_, cDim_, 120), mi.playing ? cArc_ : cBg_, mi.playing ? cBg_ : (me ? cText_ : cDim_), 9);
    drawRefBtn(rowBtn(2), "NEXT", 2, me ? cBtn_ : mix(cBg_, cDim_, 120), cBg_, me ? cText_ : cDim_, 9);
    drawRefBtn(rowBtn(3), l, 2, unread > 0 ? cWarn_ : cBtn_, cBg_, unread > 0 ? cWarn_ : cText_, 9);
  }
  for (int i = 0; i < 3; ++i) tft_.fillCircle(238 + i * 10, 231, 2, rowPage_ == i ? cText_ : cEdge_);
  holdModeDrawn_ = 0;
  sportDrawn_ = 0;
  secDrawn_ = 0;
}

void DisplayUI::tickDashRef(const Telemetry& t, bool touching, int16_t mx, int16_t my,
                            const GestureEvent& ev, uint32_t now) {
  char n[16], b[44];
  // ---- top row: link dot + source, limit marker, phone
  const BleState bs = c_.ble->state();
  const uint16_t dc = bs == BleState::Connected ? cOk_ : (bs == BleState::Off ? cText_ : cNeon_);
  if (dc != dotCol_) { tft_.fillSmoothCircle(11, 15, 4, dc, cBg_); dotCol_ = dc; }
  if (t.isDemo) txt(1, 20, 7, 2, "DEMO", cText_, TL_DATUM, 52);
  else if (t.speedValid || t.battPctValid || t.voltValid) txt(1, 20, 7, 2, "LIVE", cOk_, TL_DATUM, 52);
  else txt(1, 20, 7, 2, "NO DATA", cBad_, TL_DATUM, 64);
  txt(0, 84, 7, 2, capState_ == 1 ? "LIMIT 20" : " ", cWarn_, TL_DATUM, 70);
  const PhoneLink pl = c_.phone->info().link;
  txt(13, 160, 7, 2, pl == PhoneLink::Connected ? "PHONE" : " ", cOk_, TL_DATUM, 52);

  // ---- name (boot text) or mode, speed, arc
  {
    const char* own = c_.store->get().bootText;
    char mname[12];
    mname[0] = 0;
    if (!own[0] && t.modeValid) {
      size_t k = 0;
      while (t.mode[k] && t.mode[k] != ' ' && k < sizeof(mname) - 1) { mname[k] = t.mode[k]; ++k; }
      mname[k] = 0;
    }
    const char* nm = own[0] ? own : mname;
    if (nm[0]) snprintf(b, sizeof(b), "_%s_", nm); else snprintf(b, sizeof(b), "_--_");
    txt(3, kRCx, 66, 2, b, nm[0] ? cText_ : cDim_, MC_DATUM, 110);
  }
  if (t.speedValid) {
    fmtFixed(n, sizeof(n), true, t.speedKmh, 0);
    txt(4, kRCx, 108, (t.speedKmh >= 99.5f) ? 7 : 8, n, cText_, MC_DATUM, 100);
  } else if (strcmp(cache_[4], "--") != 0) {
    tft_.fillRect(kRCx - 52, 76, 104, 64, cBg_);
    tft_.fillRect(kRCx - 28, 104, 24, 8, cDim_);
    tft_.fillRect(kRCx + 4, 104, 24, 8, cDim_);
    strlcpy(cache_[4], "--", sizeof(cache_[4]));
  }
  {
    // full-scale: 35 km/h, or higher when a mode limit above that is read back or the scooter goes faster
    float need = kRMinMaxKmh;
    {
      xbot::Readback rbm;
      if (!c_.store->get().demo && c_.ble->getReadback(rbm, now))
        for (int i = 0; i < 3; ++i) if (rbm.limValid[i] && (float)rbm.limKmh[i] > need) need = (float)rbm.limKmh[i];
      if (t.speedValid && t.speedKmh > need) need = t.speedKmh;
      need = ceilf(need / 5.0f) * 5.0f;
      if (c_.store->get().arcMax) need = (float)c_.store->get().arcMax * 5.0f;      // fixed scale chosen in the app
      if (t.speedValid && t.speedKmh > need) need = ceilf(t.speedKmh / 5.0f) * 5.0f;
      if (need > 120.0f) need = 120.0f;
    }
    if (need != arcMax_) {                               // rescale: keep the same speed at the right place, repaint the arc
      gauge_ = gauge_ * arcMax_ / need;
      arcMax_ = need;
      arcFill_ = -1;
    }
    const float target = t.speedValid ? fminf(t.speedKmh / arcMax_, 1.0f) : 0.0f;
    gauge_ += (target - gauge_) * 0.3f;
    int fa = 45 + (int)(270.0f * gauge_ + 0.5f);
    if (fa > 315) fa = 315;
    const bool notch = capState_ == 1;
    static bool notchDrawn = false;
    if (fa != arcFill_ || notch != notchDrawn) {
      const int lo = (arcFill_ < 0 ? 45 : (fa < arcFill_ ? fa : arcFill_)) - 10;
      const int hi = (arcFill_ < 0 ? 315 : (fa > arcFill_ ? fa : arcFill_)) + 10;
      if (notch != notchDrawn) paintArc(45, 315, fa); else paintArc(lo, hi, fa);
      if (notch) drawCapNotch();
      drawKnob(fa);
      arcFill_ = fa;
      notchDrawn = notch;
    }
  }

  // ---- rear/front temperatures (or the song while the music buttons are showing)
  {
    const MediaInfo mi = c_.phone->info();
    if (rowPage_ == 2) {
      if (mi.link == PhoneLink::Connected && mi.trackValid) snprintf(b, sizeof(b), "%.8s - %.9s", mi.artist[0] ? mi.artist : "?", mi.title);
      else snprintf(b, sizeof(b), "%s", mi.link == PhoneLink::Connected ? "no song info" : "no phone");
      txt(12, 46, 195, 1, b, mi.playing ? cOk_ : cDim_, TL_DATUM, 118);
      txt(14, 46, 208, 1, " ", cDim_, TL_DATUM, 118);
    } else {
      char t1[8], t2[8];
      fmtFixed(t1, sizeof(t1), t.tempValid, t.tempC, 0);
      fmtFixed(t2, sizeof(t2), t.motorTempValid, t.motorTempC, 0);
      snprintf(b, sizeof(b), "R FET %s%s MOT %s%s", t1, t.tempValid ? "\xF8" : "", t2, t.motorTempValid ? "\xF8" : "");
      txt(12, 46, 195, 1, b, cText_, TL_DATUM, 118);
      txt(14, 46, 208, 1, "F FET -- MOT --", cDim_, TL_DATUM, 118);   // the protocol has one controller/motor sensor pair only
    }
  }

  // ---- right column: odometer, power, power bar
  fmtFixed(n, sizeof(n), t.totalValid, t.totalKm, 0);
  txt(15, 290, 18, 2, n, t.totalValid ? mix(cText_, cBg_, 40) : cDim_, TR_DATUM, 70);
  if (t.powerValid) { fmtFixed(n, sizeof(n), true, (float)t.powerW, 0); txt(5, 290, 33, 6, n, cText_, TR_DATUM, 124); }
  else txt(5, 290, 44, 4, "--", cDim_, TR_DATUM, 124);
  {
    int fx0 = 0, fx1 = 0;
    const int zx = kBarInner / 5;                       // zero point of the bar: regeneration to the left of it
    if (t.powerValid) {
      const float p = (float)t.powerW;
      if (p >= 0) { fx0 = zx; fx1 = zx + (int)(fminf(p / 6000.0f, 1.0f) * (float)(kBarInner - zx)); }
      else { fx1 = zx; fx0 = zx - (int)(fminf(-p / 800.0f, 1.0f) * (float)zx); }
    }
    const int key = fx0 * 1000 + fx1;
    if (key != barPwKey_) { barPwKey_ = key; drawRefBar(87, 6, fx0, fx1, cBtn_); }
  }

  // ---- battery: percent, voltage, bar, currents
  {
    uint16_t col = cText_;
    if (t.battPctValid) col = t.battPct > 20 ? cText_ : (t.battPct > 10 ? cWarn_ : cBad_);
    if (t.battPctValid) snprintf(b, sizeof(b), "%u%%", (unsigned)t.battPct); else snprintf(b, sizeof(b), "--");
    txt(6, 178, 100, 4, b, t.battPctValid ? col : cDim_, TL_DATUM, 70);
    fmtFixed(n, sizeof(n), t.voltValid, t.volts, 1);
    snprintf(b, sizeof(b), t.voltValid ? "%s V" : "%s", n);
    txt(7, 314, 106, 2, b, t.voltValid ? cText_ : cDim_, TR_DATUM, 70);
    const int fx1 = t.battPctValid ? (int)t.battPct * kBarInner / 100 : 0;
    if (fx1 != barBtKey_ || col != barBtCol_) { barBtKey_ = fx1; barBtCol_ = col; drawRefBar(133, 14, 0, fx1, t.battPct > 20 ? cBtn_ : col); }
    txt(16, 178, 158, 1, "MOTOR -- A", cDim_, TL_DATUM, 70);          // the protocol has no motor current
    fmtFixed(n, sizeof(n), t.currentValid, t.currentA, 0);
    snprintf(b, sizeof(b), "BATT %s A", n);
    if (strcmp(cache_[17], b) != 0) {
      tft_.fillSmoothRoundRect(254, 151, 60, 13, 4, cTag_, cBg_);
      tft_.setTextColor(t.currentValid ? cText_ : cDim_, cTag_);
      tft_.setTextDatum(MC_DATUM);
      tft_.setTextPadding(0);
      tft_.drawString(b, 284, 158, 1);
      strlcpy(cache_[17], b, sizeof(cache_[17]));
    }
  }

  tickRowInput(t, touching, mx, my, ev, now);
}

void DisplayUI::tickRowInput(const Telemetry& t, bool touching, int16_t mx, int16_t my,
                             const GestureEvent& ev, uint32_t now) {
  int active;
  bool en;
  modeState(now, active, en);
  {
    const MediaInfo mi = c_.phone->info();
    const uint32_t key = (uint32_t)rowPage_ | ((uint32_t)(active + 1) << 2) | (en ? 32u : 0u) | ((uint32_t)(capState_ + 1) << 6) |
                         (capReleased_ ? 256u : 0u) | (mi.link == PhoneLink::Connected ? 512u : 0u) | (mi.playing ? 1024u : 0u) |
                         ((uint32_t)c_.notes->unread() << 11);
    if (key != rowKey_) drawRow();
  }
  const bool demo = c_.store->get().demo;

  if (rowPage_ == 0) {
    const bool onSport = touching && inBtn(sportBtn(), mx, my);
    const Btn sb = sportBtn();
    // service menu: SPORT held for 2 s while the throttle is pressed and the scooter stands still.
    // (The protocol has no throttle value: power draw is the stand-in. In demo the throttle is assumed.)
    const bool gas = demo || (t.powerValid && t.powerW > startcap::kGasPowerW);
    const bool secret = startcap::secretArmed(onSport, gas, demo || t.speedValid, demo ? 0.0f : t.speedKmh);
    if (holdSec_.update(secret, now, startcap::kSecretHoldMs)) { holdSec_ = HoldTracker(); go(Screen::Secret); return; }
    if (secret) {
      const int w = (int)(holdSec_.progress(now, startcap::kSecretHoldMs) * (float)(sb.w - 16));
      if (w > secDrawn_) { tft_.fillRect(sb.x + 8 + secDrawn_, sb.y + sb.h - 9, w - secDrawn_, 4, cBad_); secDrawn_ = w; }
    } else if (secDrawn_ > 0) { secDrawn_ = 0; drawRow(); return; }
    // plain mode change: hold SPORT 0.8 s (not while the service-menu hold is running on a real scooter)
    const bool modeArm = onSport && en && active != 3 && (demo || !secret);
    if (holdMode_.update(modeArm, now, kRowHoldMs)) {
      holdMode_ = HoldTracker();
      if (demo) c_.ctrl->setMode(RideMode::Sport, true);
      else c_.ble->requestWriteOnce(WriteKind::Mode, xbot::modeIndexFromInternal(3));
      drawRow();
      return;
    }
    if (modeArm && !secret) {
      const int w = (int)(holdMode_.progress(now, kRowHoldMs) * (float)(sb.w - 16));
      if (w > sportDrawn_) { tft_.fillRect(sb.x + 8 + sportDrawn_, sb.y + sb.h - 9, w - sportDrawn_, 4, cArcHi_); sportDrawn_ = w; }
    } else if (sportDrawn_ > 0) { sportDrawn_ = 0; drawRow(); return; }
  } else if (rowPage_ == 1) {
    int inside = -1;
    if (touching && en) for (int i = 0; i < 4; ++i) if (inBtn(rowBtn(i), mx, my)) inside = i;
    if (inside != holdModeIdx_) { holdModeIdx_ = inside; holdMode_ = HoldTracker(); if (holdModeDrawn_ != 0) drawRow(); }
    if (inside >= 0 && inside != active) {
      if (holdMode_.update(true, now, kRowHoldMs)) {
        if (demo) c_.ctrl->setMode((RideMode)inside, true);
        else c_.ble->requestWriteOnce(WriteKind::Mode, xbot::modeIndexFromInternal((uint8_t)inside));
        drawRow();
      } else {
        const Btn bb = rowBtn(inside);
        const int w = (int)(holdMode_.progress(now, kRowHoldMs) * (float)(bb.w - 16));
        if (w > holdModeDrawn_) { tft_.fillRect(bb.x + 8 + holdModeDrawn_, bb.y + bb.h - 6, w - holdModeDrawn_, 2, cArcHi_); holdModeDrawn_ = w; }
      }
    }
  } else if (ev.type == GestureType::Tap) {
    if (inBtn(rowBtn(0), ev.x, ev.y)) c_.phone->prev();
    else if (inBtn(rowBtn(1), ev.x, ev.y)) c_.phone->playPause();
    else if (inBtn(rowBtn(2), ev.x, ev.y)) c_.phone->next();
    else if (inBtn(rowBtn(3), ev.x, ev.y)) { go(Screen::Msg); return; }
  }

  if (ev.type == GestureType::SwipeUp) { go(Screen::Safety); return; }
  if (ev.type == GestureType::SwipeLeft || ev.type == GestureType::SwipeRight) {
    if (ev.y >= 170 && ev.x >= 170) {                   // swipe along the button area: next page
      rowPage_ = (uint8_t)((rowPage_ + (ev.type == GestureType::SwipeLeft ? 1 : 2)) % 3);
      holdModeIdx_ = -1;
      holdMode_ = HoldTracker();
      drawRow();
      return;
    }
    go(ev.type == GestureType::SwipeLeft ? Screen::Music : Screen::Msg);
    return;
  }
  if (ev.type == GestureType::Tap && inBtn(setZone(), ev.x, ev.y)) go(Screen::Settings);
}


// ------------------------------------------------------------------ custom dashboard (widgets 2.0)
void DisplayUI::tickCustom(const Telemetry& t, bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  const bool bleUp = c_.ble->state() == BleState::Connected;
  const bool phoneUp = c_.phone->info().link == PhoneLink::Connected;
  stats_.update(t, now, bleUp, phoneUp, capState_ == 1);
  int active;
  bool en;
  modeState(now, active, en);
  const uint8_t mode = (active >= 0 && active < 4) ? (uint8_t)active : 255;
  wr_.update(t, stats_, mode);
  const bool demo = c_.store->get().demo;

  // mode buttons are held (0.8 s); SPORT held 2 s with the throttle pressed opens the service menu
  const int btn = touching ? wr_.buttonAt(mx, my) : -1;
  if (btn != custBtn_) {
    if (custBtn_ >= 0) wr_.forceRedraw(custBtn_);
    custBtn_ = btn;
    custHold_ = HoldTracker();
    custSec_ = HoldTracker();
  }
  if (btn >= 0) {
    const uint8_t act = wr_.buttonAction(btn);
    if (act >= wl::A_WALK && act <= wl::A_SPORT) {
      const int target = act - wl::A_WALK;
      const bool gas = demo || (t.powerValid && t.powerW > startcap::kGasPowerW);
      const bool secret = act == wl::A_SPORT && startcap::secretArmed(true, gas, demo || t.speedValid, demo ? 0.0f : t.speedKmh);
      if (custSec_.update(secret, now, startcap::kSecretHoldMs)) { custSec_ = HoldTracker(); go(Screen::Secret); return; }
      const bool modeArm = en && target != active && (demo || !secret);
      if (custHold_.update(modeArm, now, kRowHoldMs)) {
        custHold_ = HoldTracker();
        if (demo) c_.ctrl->setMode((RideMode)target, true);
        else c_.ble->requestWriteOnce(WriteKind::Mode, xbot::modeIndexFromInternal((uint8_t)target));
        wr_.forceRedraw(btn);
      }
      const float f = secret ? custSec_.progress(now, startcap::kSecretHoldMs) : (modeArm ? custHold_.progress(now, kRowHoldMs) : 0.0f);
      if (f > 0.0f) wr_.drawProgress(btn, f, secret ? cBad_ : cArcHi_);
    }
  }

  if (ev.type == GestureType::Tap) {
    const int b = wr_.buttonAt(ev.x, ev.y);
    if (b >= 0) {
      switch (wr_.buttonAction(b)) {
        case wl::A_NEXTPAGE: wr_.setPage((uint8_t)((wr_.page() + 1) % wr_.pages())); wr_.invalidate(); return;
        case wl::A_PREVPAGE: wr_.setPage((uint8_t)((wr_.page() + wr_.pages() - 1) % wr_.pages())); wr_.invalidate(); return;
        case wl::A_PREVTRACK: c_.phone->prev(); break;
        case wl::A_PLAYPAUSE: c_.phone->playPause(); break;
        case wl::A_NEXTTRACK: c_.phone->next(); break;
        case wl::A_SETTINGS: go(Screen::Settings); return;
        case wl::A_MESSAGES: go(Screen::Msg); return;
        case wl::A_MUSIC: go(Screen::Music); return;
        case wl::A_SAFETY: go(Screen::Safety); return;
        default: break;
      }
    } else if (ev.x >= 292 && ev.y < 22) {            // the top-right corner always opens Settings
      go(Screen::Settings);
      return;
    }
  }
  if (ev.type == GestureType::SwipeUp) { go(Screen::Safety); return; }
  if (ev.type == GestureType::SwipeLeft || ev.type == GestureType::SwipeRight) {
    if (wr_.pages() > 1) {
      const int d = ev.type == GestureType::SwipeLeft ? 1 : (int)wr_.pages() - 1;
      wr_.setPage((uint8_t)((wr_.page() + d) % wr_.pages()));
      wr_.invalidate();
      return;
    }
    go(ev.type == GestureType::SwipeLeft ? Screen::Music : Screen::Msg);
  }
}

// ------------------------------------------------------------------ start cap + service menu

// Runs every frame. Arms the 20 km/h cap whenever the link to the scooter comes up (= switched on).
void DisplayUI::capTick(uint32_t now) {
  const Settings& s = c_.store->get();
  if (s.demo) {                                          // local simulation only: nothing is sent
    if (s.startCap != capLastStart_) { capLastStart_ = s.startCap; capDemo_ = s.startCap; capReleased_ = false; }
    capState_ = capDemo_ ? 1 : 0;
    return;
  }
  const bool linked = c_.ble->state() == BleState::Connected;
  if (!linked) {
    if (capWasLinked_) { capWasLinked_ = false; capReleased_ = false; capTries_ = 0; }   // scooter off: cap armed again
    capState_ = -1;
    return;
  }
  capWasLinked_ = true;
  xbot::Readback rb;
  if (!c_.ble->getReadback(rb, now)) return;             // keep the last known state
  capState_ = xbot::limitsCapped(rb, startcap::kCapKmh);
  if (capState_ == 1) capTries_ = 0;
  startcap::Inputs in;
  in.startCapOn = s.startCap;
  in.released = capReleased_;
  in.linked = true;
  in.capState = capState_;
  in.writePending = c_.ble->writeStatus().state == WriteState::Pending;
  in.tries = capTries_;
  in.msSinceLastTry = now - capTryMs_;
  if (startcap::shouldCap(in) && c_.ble->requestWriteOnce(WriteKind::LimitCap, 1)) { capTryMs_ = now; ++capTries_; }
}

// The app switched the start cap on or off.
void DisplayUI::onStartCapChanged() {
  const Settings& s = c_.store->get();
  capReleased_ = false;
  capTries_ = 0;
  if (s.demo) { capLastStart_ = s.startCap; capDemo_ = s.startCap; return; }
  if (!s.startCap && capState_ == 1 && c_.ble->limitSave().valid) c_.ble->requestWriteOnce(WriteKind::LimitCap, 0);   // switched off: give the limits back
}

void DisplayUI::drawSecret() {
  drawHeader("SERVICE");
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  secretKey_ = 0xFFFFFFFFu;
  holdDrawn_ = 0;
  hold_ = HoldTracker();
}

void DisplayUI::tickSecret(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  const Settings& s = c_.store->get();
  const bool demo = s.demo;
  const bool canRelease = demo || c_.ble->limitSave().valid;
  const WriteStatus ws = c_.ble->writeStatus();
  const bool showWs = !demo && ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < 8000);
  uint32_t key = (uint32_t)(capState_ + 1) | (s.startCap ? 4u : 0u) | (capReleased_ ? 8u : 0u) | (canRelease ? 16u : 0u);
  if (showWs) key |= 32u | ((uint32_t)ws.state << 6);
  const bool capped = capState_ == 1;
  const bool canAct = capped ? canRelease : (demo || capState_ == 0);
  if (key != secretKey_) {
    secretKey_ = key;
    tft_.fillRect(0, 36, kW, 166, cBg_);
    tft_.setTextPadding(0);
    tft_.setTextDatum(TC_DATUM);
    const char* stat = capState_ == 1 ? "LIMIT 20 KM/H ACTIVE" : (capState_ == 0 ? "LIMIT REMOVED" : "UNKNOWN");
    tft_.setTextColor(capState_ == 1 ? cWarn_ : (capState_ == 0 ? cOk_ : cWarn_), cBg_);
    tft_.drawString(stat, 160, 42, 4);
    tft_.setTextColor(cDim_, cBg_);
    tft_.drawString(s.startCap ? "Start cap is ON (set in the phone app)" : "Start cap is OFF (phone app)", 160, 80, 2);
    tft_.drawString(capped ? "Releasing lasts until next power-on"
                           : (s.startCap ? "20 km/h returns at next power-on" : "Session only - nothing is saved"), 160, 98, 2);
    const char* l3 = demo ? "DEMO: simulated, nothing is sent" : "Scooter limits are read back";
    uint16_t c3 = demo ? cWarn_ : cDim_;
    char wb[40];
    if (showWs && ws.state == WriteState::Pending) { l3 = "SENDING..."; c3 = cWarn_; }
    else if (showWs && ws.state == WriteState::Confirmed) { l3 = "CONFIRMED by scooter"; c3 = cOk_; }
    else if (showWs && ws.state == WriteState::NotConfirmed) { l3 = "NOT CONFIRMED"; c3 = cBad_; }
    else if (showWs && ws.state == WriteState::Refused) { snprintf(wb, sizeof(wb), "REFUSED: %s", ws.note); l3 = wb; c3 = cBad_; }
    tft_.setTextColor(c3, cBg_);
    tft_.drawString(l3, 160, 118, 2);
    const char* lbl = capped ? (canRelease ? "HOLD 1.5s: REMOVE LIMIT" : "NO SAVED LIMITS")
                             : (canAct ? "HOLD 1.5s: LIMIT TO 20" : "UNAVAILABLE");
    drawBtn(holdBtn(), lbl, canAct, capped ? cWarn_ : cOk_);
    holdDrawn_ = 0;
    hold_ = HoldTracker();
  }
  const Btn hb = holdBtn();
  if (canAct) {
    if (hold_.update(touching && inBtn(hb, mx, my), now, kHoldMs)) {
      hold_ = HoldTracker();
      if (capped) {                                      // release for this ride
        capReleased_ = true;
        if (demo) capDemo_ = false; else c_.ble->requestWriteOnce(WriteKind::LimitCap, 0);
      } else {
        capReleased_ = false;
        if (demo) capDemo_ = true; else c_.ble->requestWriteOnce(WriteKind::LimitCap, 1);
      }
    } else {
      const int w = (int)(hold_.progress(now, kHoldMs) * (float)(hb.w - 8));
      if (w != holdDrawn_) {
        if (w > holdDrawn_) tft_.fillRect(hb.x + 4 + holdDrawn_, hb.y + hb.h - 8, w - holdDrawn_, 4, cBad_);
        else tft_.fillRect(hb.x + 4 + w, hb.y + hb.h - 8, holdDrawn_ - w, 4, cPanel_);
        holdDrawn_ = w;
      }
    }
  }
  if (ev.type == GestureType::Tap && inBtn(bottomBtn(0), ev.x, ev.y)) go(Screen::Dash);
}

// ------------------------------------------------------------------ custom dashboard (widgets)

uint16_t DisplayUI::wcol(uint8_t idx) const {
  switch (idx) {
    case 1: return cNeon_;
    case 2: return cOk_;
    case 3: return cWarn_;
    case 4: return cBad_;
    case 5: return cDim_;
    case 6: return cPanel_;
    default: return cText_;
  }
}

void DisplayUI::reloadLayout() {
  uint8_t b[wl::kMaxBytes];
  size_t n = 0;
  layoutOk_ = false;
  if (c_.store->get().layoutValid && c_.store->loadLayout(b, sizeof(b), n)) layoutOk_ = wl::parse(b, n, layout_);
  if (!layoutOk_) layoutOk_ = wl::parse(kDefaultLayout, kDefaultLayoutLen, layout_);   // built-in design: also editable from the app
  wr_.setLayout(layoutOk_ ? &layout_ : nullptr);
  stats_.reset(millis());
}

namespace {
const uint8_t kFontNo[5] = {2, 4, 6, 7, 8};
uint8_t datumFor(uint8_t align) { return align == 1 ? TC_DATUM : (align == 2 ? TR_DATUM : TL_DATUM); }
}  // namespace





// ------------------------------------------------------------------ music / messages

namespace {
DisplayUI::Btn musicBtn(int i) { return mk((int16_t)(8 + i * 104), 136, 96, 52); }
DisplayUI::Btn msgCard(int i) { return mk(8, (int16_t)(38 + i * 54), 304, 48); }

// Cuts a string to what fits on one line of font 2.
void clip(char* out, size_t cap, const char* s, size_t maxChars) {
  size_t n = strlen(s);
  if (n > maxChars) n = maxChars;
  if (n + 1 > cap) n = cap - 1;
  memcpy(out, s, n);
  out[n] = 0;
}
}  // namespace

// ------------------------------------------------------------------ music

void DisplayUI::drawMusic() {
  drawHeader("MUSIC");
  const MediaInfo mi = c_.phone->info();
  musicKey_ = (int)mi.link * 8 + (mi.playing ? 4 : 0) + (mi.trackValid ? 2 : 0) + c_.notes->unread() * 16;
  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  const char* st = "PHONE OFF";
  const char* st2 = "Settings > PHONE";
  uint16_t sc = cDim_;
  if (mi.link == PhoneLink::Connected) { st = "PHONE CONNECTED"; sc = cOk_; st2 = applesvc::compiled() ? applesvc::statusText() : "Pair done - media keys"; }
  else if (mi.link == PhoneLink::Advertising) { st = "WAITING FOR PHONE"; sc = cWarn_; st2 = "Pair NOVA RIDE in iPhone Bluetooth"; }
  tft_.setTextColor(sc, cBg_);
  tft_.drawString(st, 160, 40, 2);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(st2, 160, 58, 2);
  char l[40];
  tft_.setTextColor(cText_, cBg_);
  if (mi.link == PhoneLink::Connected && mi.trackValid) {
    clip(l, sizeof(l), mi.title[0] ? mi.title : "-", 20);
    tft_.drawString(l, 160, 84, 4);
    tft_.setTextColor(cNeon_, cBg_);
    clip(l, sizeof(l), mi.artist[0] ? mi.artist : "-", 34);
    tft_.drawString(l, 160, 116, 2);
  } else {
    tft_.drawString("--", 160, 84, 4);
    tft_.setTextColor(cDim_, cBg_);
    tft_.drawString(applesvc::compiled() ? "Start a song on the iPhone" : "Song names need the apple build", 160, 116, 2);
  }
  const bool en = mi.link == PhoneLink::Connected;
  drawBtn(musicBtn(0), "PREV", en, cNeon_);
  drawBtn(musicBtn(1), mi.playing ? "PAUSE" : "PLAY", en, cNeon_, mi.playing);
  drawBtn(musicBtn(2), "NEXT", en, cNeon_);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  const int u = c_.notes->unread();
  if (u > 0) { snprintf(l, sizeof(l), "MSG %d", u); drawBtn(bottomBtn(2), l, true, cWarn_, true); }
  else drawBtn(bottomBtn(2), "MSG", true, cNeon_);
}

void DisplayUI::tickMusic(const GestureEvent& ev) {
  const MediaInfo mi = c_.phone->info();
  const int key = (int)mi.link * 8 + (mi.playing ? 4 : 0) + (mi.trackValid ? 2 : 0) + c_.notes->unread() * 16;
  static char lastTitle[32] = "";
  const bool titleChanged = strcmp(lastTitle, mi.title) != 0;
  if (key != musicKey_ || titleChanged) { strlcpy(lastTitle, mi.title, sizeof(lastTitle)); drawMusic(); }
  if (ev.type == GestureType::SwipeRight) { go(Screen::Dash); return; }
  if (ev.type != GestureType::Tap) return;
  if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
  if (inBtn(bottomBtn(2), ev.x, ev.y)) { go(Screen::Msg); return; }
  if (!c_.phone->available()) return;  // keys only go out with a live iPhone link
  if (inBtn(musicBtn(0), ev.x, ev.y)) c_.phone->prev();
  else if (inBtn(musicBtn(1), ev.x, ev.y)) c_.phone->playPause();
  else if (inBtn(musicBtn(2), ev.x, ev.y)) c_.phone->next();
}

// ------------------------------------------------------------------ messages

void DisplayUI::drawMsg() {
  drawHeader("MESSAGES");
  c_.notes->markAllRead();
  const int n = c_.notes->count();
  musicKey_ = n * 4 + (c_.notes->available() ? 1 : 0) + (c_.phone->available() ? 2 : 0);
  tft_.setTextPadding(0);
  if (n == 0) {
    tft_.setTextDatum(TC_DATUM);
    tft_.setTextColor(cDim_, cBg_);
    if (!applesvc::compiled()) {
      tft_.setTextColor(cWarn_, cBg_);
      tft_.drawString("NEEDS THE APPLE BUILD", 160, 70, 4);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Flash cyd_st7789_apple (experimental)", 160, 112, 2);
    } else if (!c_.phone->available()) {
      tft_.drawString("NO PHONE", 160, 74, 4);
      tft_.drawString("Connect the iPhone first", 160, 112, 2);
    } else if (!c_.notes->available()) {
      tft_.setTextColor(cWarn_, cBg_);
      tft_.drawString("SETTING UP...", 160, 70, 4);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString(applesvc::statusText(), 160, 104, 2);
      tft_.drawString("iPhone: Settings > Bluetooth > (i)", 160, 128, 2);
      tft_.drawString("> turn on Show Notifications", 160, 146, 2);
    } else {
      tft_.setTextColor(cOk_, cBg_);
      tft_.drawString("NO MESSAGES YET", 160, 84, 4);
    }
  } else {
    char l[48];
    for (int i = 0; i < n && i < 3; ++i) {
      NotifMsg m;
      if (!c_.notes->get(i, m)) break;
      const Btn c = msgCard(i);
      tft_.fillRoundRect(c.x, c.y, c.w, c.h, 9, cPanel_);
      tft_.drawRoundRect(c.x, c.y, c.w, c.h, 9, mix(cPanel_, cDim_, 140));
      tft_.setTextDatum(TL_DATUM);
      tft_.setTextColor(cNeon_, cPanel_);
      clip(l, sizeof(l), m.app, 14);
      tft_.drawString(l, c.x + 10, c.y + 6, 2);
      tft_.setTextColor(cText_, cPanel_);
      clip(l, sizeof(l), m.title, 22);
      tft_.drawString(l, c.x + 100, c.y + 6, 2);
      tft_.setTextColor(cDim_, cPanel_);
      clip(l, sizeof(l), m.text, 36);
      tft_.drawString(l, c.x + 10, c.y + 26, 2);
    }
  }
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  if (n > 0) drawBtn(bottomBtn(2), "CLEAR", true, cNeon_);
}

void DisplayUI::tickMsg(const GestureEvent& ev) {
  const int key = c_.notes->count() * 4 + (c_.notes->available() ? 1 : 0) + (c_.phone->available() ? 2 : 0);
  if (key != musicKey_) drawMsg();
  if (ev.type == GestureType::SwipeLeft) { go(Screen::Dash); return; }
  if (ev.type != GestureType::Tap) return;
  if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
  if (c_.notes->count() > 0 && inBtn(bottomBtn(2), ev.x, ev.y)) { c_.notes->clear(); drawMsg(); }
}

// ------------------------------------------------------------------ settings (two pages of cards)

void DisplayUI::drawSettings() {
  drawHeader(settingsPage_ == 0 ? "SETTINGS" : "TOOLS");
  const Settings& s = c_.store->get();
  char v[24];
  if (settingsPage_ == 0) {
    drawCard(cardBtn(0), "DEMO", s.demo ? "ON" : "OFF", s.demo ? cWarn_ : cDim_);
    drawCard(cardBtn(1), "BLE SCAN", s.bleScan ? "ON" : "OFF", s.bleScan ? cOk_ : cDim_);
    drawCard(cardBtn(2), "PHONE", s.phone ? "ON" : "OFF", s.phone ? cOk_ : cDim_);
    drawCard(cardBtn(3), "THEME", kThemes[s.theme].name, cNeon_);
    static const char* const bootNames[4] = {"Classic", "Name", "Image", "Minimal"};
    drawCard(cardBtn(4), "BOOT", bootNames[s.bootStyle & 3], cNeon_);
    drawCard(cardBtn(5), "LAYOUT", layoutOk_ ? (s.layoutOn ? "Custom" : "Standard") : "none", layoutOk_ ? cNeon_ : cDim_);
    snprintf(v, sizeof(v), "%u%%", (unsigned)s.backlightPct);
    drawCard(cardBtn(6), "BRIGHTNESS", v, cNeon_);
    drawCard(cardBtn(7), "INVERT", s.invert ? "ON" : "OFF", s.invert ? cWarn_ : cDim_);
    static const char* const cm[3] = {"A (default)", "B (BGR)", "C (RGB)"};
    drawCard(cardBtn(8), "COLORS", cm[s.colorMode % 3], cNeon_);
  } else {
    drawCard(cardBtn(0), "FLIP", "180 deg", cNeon_);
    drawCard(cardBtn(1), "HW TEST", "run", cNeon_);
    drawCard(cardBtn(2), "CALIBRATE", "touch", cNeon_);
    drawCard(cardBtn(3), "DIAG", "info", cNeon_);
    drawCard(cardBtn(4), "CONTROLS", "limits", cNeon_);
    drawCard(cardBtn(5), "UPDATE", "firmware", cNeon_);
    const MediaInfo mi = c_.phone->info();
    drawCard(cardBtn(6), "MUSIC", mi.link == PhoneLink::Connected ? (mi.playing ? "playing" : "ready") : "no phone", mi.link == PhoneLink::Connected ? cOk_ : cDim_);
    const int u = c_.notes->unread();
    snprintf(v, sizeof(v), u > 0 ? "%d new" : "none", u);
    drawCard(cardBtn(7), "MESSAGES", v, u > 0 ? cWarn_ : cDim_);
    drawCard(cardBtn(8), "ABOUT", "v" NOVA_FW_VERSION, cDim_);
  }
  drawBtn(bottomBtn(0), "BACK", true, cOk_);
  drawBtn(bottomBtn(2), settingsPage_ == 0 ? "MORE >" : "< SETTINGS", true, cNeon_);
}

void DisplayUI::tickSettings(const GestureEvent& ev) {
  if (ev.type == GestureType::SwipeLeft && settingsPage_ == 0) { settingsPage_ = 1; full_ = true; return; }
  if (ev.type == GestureType::SwipeRight && settingsPage_ == 1) { settingsPage_ = 0; full_ = true; return; }
  if (ev.type != GestureType::Tap) return;
  if (inBtn(bottomBtn(0), ev.x, ev.y)) { settingsPage_ = 0; go(Screen::Dash); return; }
  if (inBtn(bottomBtn(2), ev.x, ev.y)) { settingsPage_ = (uint8_t)(1 - settingsPage_); full_ = true; return; }
  int idx = -1;
  for (int i = 0; i < 9; ++i) if (inBtn(cardBtn(i), ev.x, ev.y)) idx = i;
  if (idx < 0) return;
  Settings& s = c_.store->get();
  if (settingsPage_ == 0) {
    switch (idx) {
      case 0: s.demo = !s.demo; c_.store->save(); full_ = true; break;
      case 1: s.bleScan = !s.bleScan; c_.ble->setEnabled(s.bleScan); c_.store->save(); full_ = true; break;
      case 2: s.phone = !s.phone; c_.phone->setEnabled(s.phone); c_.store->save(); full_ = true; break;
      case 3: s.theme = (uint8_t)((s.theme + 1) % kThemeCount); applyTheme(s.theme); c_.store->save(); full_ = true; break;
      case 4: s.bootStyle = (uint8_t)((s.bootStyle + 1) % app::kBootStyles); c_.store->save(); splash(); full_ = true; break;
      case 5: if (layoutOk_) { s.layoutOn = !s.layoutOn; c_.store->save(); full_ = true; } break;
      case 6: {
        const uint8_t steps[4] = {25, 50, 75, 100};
        uint8_t next = steps[0];
        for (int i = 0; i < 4; ++i) if (steps[i] > s.backlightPct) { next = steps[i]; break; }
        s.backlightPct = next; setBacklight(next); c_.store->save(); full_ = true;
        break;
      }
      case 7: toggleInvert(); break;
      default: toggleColors(); break;
    }
  } else {
    switch (idx) {
      case 0: flipRotation(); break;
      case 1: hwPage_ = 0; go(Screen::Hw); break;
      case 2: startCal(Screen::Settings); break;
      case 3: go(Screen::Diag); break;
      case 4: go(Screen::Ctrl); break;
      case 5: go(Screen::Update); break;
      case 6: go(Screen::Music); break;
      case 7: go(Screen::Msg); break;
      default: break;
    }
  }
}

// ------------------------------------------------------------------ firmware update (phone app)

void DisplayUI::drawUpdate() {
  drawHeader("UPDATE");
  tft_.drawRoundRect(19, 112, 282, 14, 4, cDim_);
  tft_.fillRect(21, 114, 278, 10, cPanel_);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  updKey_ = -1;
  updBarW_ = -1;
  holdDrawn_ = 0;
  hold_ = HoldTracker();
}

void DisplayUI::tickUpdate(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  const applink::Status st = applink::status(now);
  const bool busy = st.otaState == app::OTA_RECV || st.otaState == app::OTA_VERIFY || st.otaState == app::OTA_DONE;
  const bool phoneOn = c_.phone->enabled();
  const Btn hb = holdBtn();

  const int key = (st.windowOpen ? 1 : 0) + (busy ? 2 : 0) + (phoneOn ? 4 : 0);
  if (key != updKey_) {
    updKey_ = key;
    tft_.fillRect(0, 140, kW, 64, cBg_);
    if (!busy) drawBtn(hb, st.windowOpen ? "CLOSE WINDOW" : "HOLD 1.5s: OPEN WINDOW", phoneOn || st.windowOpen, cNeon_);
    holdDrawn_ = 0;
    hold_ = HoldTracker();
  }

  char b[48];
  const char* l1 = "WINDOW CLOSED";
  uint16_t c1 = cDim_;
  if (st.otaState == app::OTA_RECV) { l1 = "RECEIVING UPDATE"; c1 = cWarn_; }
  else if (st.otaState == app::OTA_VERIFY) { l1 = "VERIFYING"; c1 = cWarn_; }
  else if (st.otaState == app::OTA_DONE) { l1 = "UPDATE OK - REBOOTING"; c1 = cOk_; }
  else if (st.otaState == app::OTA_ERROR) { snprintf(b, sizeof(b), "ERROR: %s", applink::errName(st.otaErr)); l1 = b; c1 = cBad_; }
  else if (st.windowOpen) { l1 = "WINDOW OPEN"; c1 = cOk_; }
  txt(0, 160, 42, 4, l1, c1, TC_DATUM, 310);

  char b2[48];
  const char* l2;
  uint16_t c2 = cDim_;
  if (!phoneOn && !st.windowOpen) { l2 = "Turn PHONE ON in Settings first"; c2 = cWarn_; }
  else if (busy && st.total) { snprintf(b2, sizeof(b2), "%lu / %lu KB", (unsigned long)(st.received / 1024), (unsigned long)(st.total / 1024)); l2 = b2; c2 = cText_; }
  else if (st.windowOpen) { snprintf(b2, sizeof(b2), "Open the app > Update  (%us)", (unsigned)(st.windowLeftMs / 1000)); l2 = b2; c2 = cText_; }
  else { l2 = "Hold the button to allow an update"; }
  txt(1, 160, 80, 2, l2, c2, TC_DATUM, 310);

  const int w = st.total ? (int)((uint64_t)st.received * 278u / st.total) : 0;
  if (w != updBarW_) {
    tft_.fillRect(21, 114, 278, 10, cPanel_);
    if (w > 0) tft_.fillRect(21, 114, w, 10, cNeon_);
    updBarW_ = w;
  }

  if (!busy && !st.windowOpen && phoneOn) {
    if (hold_.update(touching && inBtn(hb, mx, my), now, kHoldMs)) {
      applink::openWindow(120000);  // two minutes
    } else {
      const int pw = (int)(hold_.progress(now, kHoldMs) * (float)(hb.w - 8));
      if (pw != holdDrawn_) {
        if (pw > holdDrawn_) tft_.fillRect(hb.x + 4 + holdDrawn_, hb.y + hb.h - 8, pw - holdDrawn_, 4, cNeon_);
        else tft_.fillRect(hb.x + 4 + pw, hb.y + hb.h - 8, holdDrawn_ - pw, 4, cPanel_);
        holdDrawn_ = pw;
      }
    }
  }
  if (ev.type != GestureType::Tap) return;
  if (inBtn(bottomBtn(0), ev.x, ev.y)) { if (!busy) applink::closeWindow(); go(Screen::Dash); return; }
  if (!busy && st.windowOpen && inBtn(hb, ev.x, ev.y)) applink::closeWindow();
}

// ------------------------------------------------------------------ 20 km/h safety screen

void DisplayUI::drawSafety() {
  drawHeader("20 KM/H LIMIT");
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  safetyKey_ = 255;  // body is (re)drawn by tickSafety
}

void DisplayUI::drawSafetyBody(LimitStatus st, bool armed) {
  tft_.fillRect(0, 36, kW, 166, cBg_);
  const char* stat = "";
  const char* l1 = ""; const char* l2 = ""; const char* l3 = "";
  uint16_t col = cDim_;
  switch (st) {
    case LimitStatus::Confirmed:
      col = cOk_; stat = "CONFIRMED 20 KM/H"; l1 = "Controller acknowledged."; break;
    case LimitStatus::DemoSimulated:
      if (armed) { col = cWarn_; stat = "DEMO: 20 SIMULATED"; l1 = "DEMO ONLY - nothing was sent."; l2 = "Scooter limit: NOT CHANGED"; l3 = "Tap below to clear."; }
      else { col = cDim_; stat = "DEMO: NOT ARMED"; l1 = "DEMO ONLY - nothing is sent."; l2 = "Scooter limit: UNKNOWN"; l3 = "Hold button to simulate 20."; }
      break;
    case LimitStatus::NotSupported:
      col = cBad_; stat = "NOT CONFIRMED"; l1 = "Controller link not supported."; l2 = "No command is ever sent."; l3 = "Scooter limit: UNKNOWN"; break;
    default:
      col = cWarn_; stat = "UNKNOWN"; l1 = "No link / no confirmation."; l2 = "Limit state: UNKNOWN"; break;
  }
  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  tft_.setTextColor(col, cBg_);
  tft_.drawString(stat, 160, 42, 4);
  tft_.setTextColor(cText_, cBg_);
  tft_.drawString(l1, 160, 80, 2);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(l2, 160, 100, 2);
  tft_.drawString(l3, 160, 120, 2);
  const bool en = (st == LimitStatus::DemoSimulated);
  const char* lbl = armed ? "CLEAR DEMO LIMIT" : (en ? "HOLD 1.5s TO ARM (DEMO)" : "UNAVAILABLE");
  drawBtn(holdBtn(), lbl, en, armed ? cWarn_ : cNeon_);
  holdDrawn_ = 0;
  hold_ = HoldTracker();
}

void DisplayUI::tickSafety(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  const bool demo = c_.store->get().demo;
  const bool real = !demo && c_.ble->state() == BleState::Connected;
  if (real != safetyWasReal_) {  // switched between the real and the local-demo view
    safetyWasReal_ = real;
    safetyKey_ = 255;
    safetyRealKey_ = 0;
    tft_.fillRect(0, 36, kW, 170, cBg_);
    hold_ = HoldTracker();
    holdLock_ = HoldTracker();
  }
  if (real) { tickSafetyReal(touching, mx, my, ev, now); return; }
  const LimitStatus st = c_.safety->status(demo);
  const bool armed = c_.safety->demoArmed();
  const uint8_t key = (uint8_t)((uint8_t)st * 2 + (armed ? 1 : 0));
  if (key != safetyKey_) { safetyKey_ = key; drawSafetyBody(st, armed); }

  const Btn hb = holdBtn();
  const bool canHold = (st == LimitStatus::DemoSimulated) && !armed;
  if (canHold) {
    const bool inside = touching && inBtn(hb, mx, my);
    if (hold_.update(inside, now, kHoldMs)) {
      c_.safety->requestQuickLimit(demo);  // local demo only, nothing is transmitted
      safetyKey_ = 255;
    } else {
      const int w = (int)(hold_.progress(now, kHoldMs) * (float)(hb.w - 8));
      if (w != holdDrawn_) {
        if (w > holdDrawn_) tft_.fillRect(hb.x + 4 + holdDrawn_, hb.y + hb.h - 8, w - holdDrawn_, 4, cNeon_);
        else tft_.fillRect(hb.x + 4 + w, hb.y + hb.h - 8, holdDrawn_ - w, 4, cPanel_);
        holdDrawn_ = w;
      }
    }
  }
  if (ev.type == GestureType::Tap) {
    if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
    if (armed && inBtn(hb, ev.x, ev.y)) { c_.safety->clearDemo(); safetyKey_ = 255; }
  }
}

// ------------------------------------------------------------------ 20 km/h cap + lock on the real scooter

uint32_t DisplayUI::safetyRealKey(uint32_t now) {
  xbot::Readback rb;
  const bool have = c_.ble->getReadback(rb, now);
  const WriteStatus ws = c_.ble->writeStatus();
  const bool showWs = ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < 8000);
  const LimitSave sv = c_.ble->limitSave();
  uint32_t k = 2166136261u;
  const uint32_t parts[11] = {have ? 1u : 0u, rb.lockValid ? 1u : 0u, rb.locked ? 1u : 0u,
                              (uint32_t)(rb.limValid[0] ? rb.limKmh[0] : 99), (uint32_t)(rb.limValid[1] ? rb.limKmh[1] : 99),
                              (uint32_t)(rb.limValid[2] ? rb.limKmh[2] : 99), sv.valid ? 1u : 0u,
                              showWs ? (uint32_t)ws.state : 0u, (uint32_t)strlen(ws.note), 0u, 1u};
  for (int i = 0; i < 11; ++i) k = (k ^ parts[i]) * 16777619u;
  return k | 1u;
}

void DisplayUI::drawSafetyReal(uint32_t now) {
  safetyRealKey_ = safetyRealKey(now);
  holdDrawn_ = 0;
  lockDrawn_ = 0;
  hold_ = HoldTracker();
  holdLock_ = HoldTracker();
  xbot::Readback rb;
  const bool have = c_.ble->getReadback(rb, now);
  const int cap = have ? xbot::limitsCapped(rb, 20) : -1;
  const LimitSave sv = c_.ble->limitSave();
  const WriteStatus ws = c_.ble->writeStatus();

  tft_.fillRect(0, 36, kW, 170, cBg_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  const char* stat = "UNKNOWN";
  uint16_t col = cWarn_;
  if (cap == 1) { stat = "20 KM/H ACTIVE"; col = cOk_; }
  else if (cap == 0) { stat = "NOT LIMITED"; col = cWarn_; }
  tft_.setTextColor(col, cBg_);
  tft_.drawString(stat, 160, 42, 4);

  char b[44];
  if (rb.limValid[0] && rb.limValid[1] && rb.limValid[2])
    snprintf(b, sizeof(b), "Sport %d Drive %d Eco %d km/h", rb.limKmh[0], rb.limKmh[1], rb.limKmh[2]);
  else if (rb.wfValid && rb.limHas[0] && rb.limHas[1] && rb.limHas[2])
    snprintf(b, sizeof(b), "wf %u raw %u/%u/%u", (unsigned)rb.wheelFactor, (unsigned)rb.limRaw[0], (unsigned)rb.limRaw[1], (unsigned)rb.limRaw[2]);
  else snprintf(b, sizeof(b), "Limits: not read yet");
  tft_.setTextColor(cText_, cBg_);
  tft_.drawString(b, 160, 80, 2);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(rb.lockValid ? (rb.locked ? "Scooter: LOCKED" : "Scooter: UNLOCKED") : "Scooter lock: --", 160, 100, 2);

  const bool show = ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < 8000);
  const char* l3 = "Hold a button to send";
  uint16_t c3 = cDim_;
  char wb[40];
  if (show && ws.state == WriteState::Pending) { l3 = "SENDING..."; c3 = cWarn_; }
  else if (show && ws.state == WriteState::Confirmed) { l3 = "CONFIRMED by scooter"; c3 = cOk_; }
  else if (show && ws.state == WriteState::NotConfirmed) { l3 = "NOT CONFIRMED"; c3 = cBad_; }
  else if (show && ws.state == WriteState::Refused) { snprintf(wb, sizeof(wb), "REFUSED: %s", ws.note); l3 = wb; c3 = cBad_; }
  tft_.setTextColor(c3, cBg_);
  tft_.drawString(l3, 160, 120, 2);

  const bool canLimit = (cap == 1) ? sv.valid : have;
  const char* lbl = (cap == 1) ? (sv.valid ? "HOLD 1.5s: RESTORE LIMITS" : "NO SAVED LIMITS") : "HOLD 1.5s: LIMIT TO 20";
  drawBtn(holdBtn(), lbl, canLimit, cNeon_);
  drawBtn(bottomBtn(2), rb.lockValid && rb.locked ? "UNLOCK" : "LOCK", rb.lockValid, cNeon_);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
}

void DisplayUI::tickSafetyReal(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  if (safetyRealKey(now) != safetyRealKey_) drawSafetyReal(now);

  xbot::Readback rb;
  const bool have = c_.ble->getReadback(rb, now);
  const int cap = have ? xbot::limitsCapped(rb, 20) : -1;
  const LimitSave sv = c_.ble->limitSave();
  const Btn hb = holdBtn();
  const Btn lb = bottomBtn(2);

  const bool canLimit = (cap == 1) ? sv.valid : have;
  if (canLimit) {
    if (hold_.update(touching && inBtn(hb, mx, my), now, kHoldMs)) {
      c_.ble->requestWriteOnce(WriteKind::LimitCap, cap == 1 ? 0 : 1);  // 1 = cap at 20, 0 = restore
    } else {
      const int w = (int)(hold_.progress(now, kHoldMs) * (float)(hb.w - 8));
      if (w != holdDrawn_) {
        if (w > holdDrawn_) tft_.fillRect(hb.x + 4 + holdDrawn_, hb.y + hb.h - 8, w - holdDrawn_, 4, cNeon_);
        else tft_.fillRect(hb.x + 4 + w, hb.y + hb.h - 8, holdDrawn_ - w, 4, cPanel_);
        holdDrawn_ = w;
      }
    }
  }
  if (rb.lockValid) {
    if (holdLock_.update(touching && inBtn(lb, mx, my), now, kHoldMs)) {
      c_.ble->requestWriteOnce(WriteKind::Lock, rb.locked ? 0 : 1);  // 1 = lock, 0 = unlock
    } else {
      const int w = (int)(holdLock_.progress(now, kHoldMs) * (float)(lb.w - 8));
      if (w != lockDrawn_) {
        if (w > lockDrawn_) tft_.fillRect(lb.x + 4 + lockDrawn_, lb.y + lb.h - 6, w - lockDrawn_, 3, cNeon_);
        else tft_.fillRect(lb.x + 4 + w, lb.y + lb.h - 6, lockDrawn_ - w, 3, cPanel_);
        lockDrawn_ = w;
      }
    }
  }
  if (ev.type == GestureType::Tap && inBtn(bottomBtn(0), ev.x, ev.y)) go(Screen::Dash);
}

// ------------------------------------------------------------------ diagnostics

void DisplayUI::drawDiag() {
  drawHeader("DIAGNOSTICS");
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  lastDiagMs_ = 0;
}

void DisplayUI::tickDiag(const GestureEvent& ev, uint32_t now) {
  if (ev.type == GestureType::Tap) {
    if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
    if (ev.y >= 148 && ev.y < 202) {
      Settings& st = c_.store->get();
      BleTarget cur = c_.ble->target();
      BleTarget nt;  // empty = scan mode
      bool change = false;
      if (cur.valid()) {
        if (ev.y < 166) change = true;  // tap the target line to forget it
      } else {
        BleSeen d;
        if (c_.ble->get((uint8_t)((ev.y - 148) / 18), d)) {
          strlcpy(nt.addr, d.addr, sizeof(nt.addr));
          strlcpy(nt.name, d.name, sizeof(nt.name));
          nt.type = d.addrType;
          change = true;
        }
      }
      if (change) {
        c_.ble->setTarget(nt);
        strlcpy(st.targetAddr, nt.addr, sizeof(st.targetAddr));
        strlcpy(st.targetName, nt.name, sizeof(st.targetName));
        st.targetType = nt.type;
        c_.store->save();
        lastDiagMs_ = 0;
      }
    }
  }
  if (lastDiagMs_ != 0 && now - lastDiagMs_ < 500) return;
  lastDiagMs_ = now ? now : 1;
  char b[48];
  snprintf(b, sizeof(b), "NOVA RIDE OS v%s %s", NOVA_FW_VERSION, NOVA_PROFILE_NAME);
  txt(0, 8, 40, 2, b, cText_, TL_DATUM, 304);
  snprintf(b, sizeof(b), "Heap %u/%u  up %lus", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
           (unsigned long)(now / 1000UL));
  txt(1, 8, 58, 2, b, cDim_, TL_DATUM, 304);
  const BleState bs = c_.ble->state();
  const char* bn = bs == BleState::Off ? "off" : bs == BleState::Connected ? "linked" : bs == BleState::Connecting ? "connecting" : "scan";
  snprintf(b, sizeof(b), "BLE %s  seen %u  reset %d", bn, (unsigned)c_.ble->count(), (int)esp_reset_reason());
  txt(2, 8, 76, 2, b, cDim_, TL_DATUM, 304);

  const BleTarget tg = c_.ble->target();
  const BleLinkInfo li = c_.ble->linkInfo();
  if (tg.valid()) snprintf(b, sizeof(b), "Target %.16s", tg.name[0] ? tg.name : tg.addr);
  else snprintf(b, sizeof(b), "No target - tap a device below");
  txt(3, 8, 94, 2, b, tg.valid() ? cText_ : cWarn_, TL_DATUM, 304);
  if (bs == BleState::Connected)
    snprintf(b, sizeof(b), "Link %s  key %02X  var %u", li.profile, (unsigned)li.key, (unsigned)li.variant);
  else if (li.refused) snprintf(b, sizeof(b), "REFUSED: variant %u not decoded", (unsigned)li.variant);
  else if (bs == BleState::Connecting) snprintf(b, sizeof(b), "%s", li.step[0] ? li.step : "Connecting...");
  else snprintf(b, sizeof(b), "Not connected (read-only build)");
  txt(4, 8, 112, 2, b, bs == BleState::Connected ? cOk_ : cDim_, TL_DATUM, 304);
  if (li.triage[0]) snprintf(b, sizeof(b), "%s", li.triage);
  else snprintf(b, sizeof(b), "ntf %lu frm %lu ver %s", (unsigned long)li.notifies, (unsigned long)li.frames,
                li.version[0] ? li.version : "--");
  txt(5, 8, 130, 2, b, li.triage[0] ? cWarn_ : cDim_, TL_DATUM, 304);

  if (tg.valid()) {
    txt(6, 8, 148, 2, "Tap here to forget target", cDim_, TL_DATUM, 304);
    txt(7, 8, 166, 2, " ", cDim_, TL_DATUM, 304);
    txt(8, 8, 184, 2, " ", cDim_, TL_DATUM, 304);
  } else {
    for (uint8_t i = 0; i < 3; ++i) {
      BleSeen d;
      if (c_.ble->get(i, d)) {
        snprintf(b, sizeof(b), "%c%-14.14s %4d", d.candidate ? '*' : ' ', d.name[0] ? d.name : d.addr, (int)d.rssi);
        txt((uint8_t)(6 + i), 8, (int16_t)(148 + i * 18), 2, b, d.candidate ? cWarn_ : cText_, TL_DATUM, 304);
      } else {
        txt((uint8_t)(6 + i), 8, (int16_t)(148 + i * 18), 2, i == 0 ? " no devices seen (BLE on?)" : " ", cDim_, TL_DATUM, 304);
      }
    }
  }
}

// ------------------------------------------------------------------ ride controls

namespace {
DisplayUI::Btn limMinus(int j) { return mk(196, (int16_t)(38 + j * 30), 52, 26); }
DisplayUI::Btn limPlus(int j) { return mk(256, (int16_t)(38 + j * 30), 52, 26); }
DisplayUI::Btn togBtn(int i) { return mk((int16_t)(8 + i * 104), 132, 96, 40); }
const char* const kLimName[3] = {"ECO", "DRIVE", "SPORT"};  // row j shows mode index 2 - j (eco, drive, sport)
const uint32_t kStatusShowMs = 8000;
}  // namespace

// Everything that changes what CTRL shows, folded into one number (redraw only on change).
uint32_t DisplayUI::ctrlKey(uint32_t now) {
  const bool demo = c_.store->get().demo;
  const bool linked = !demo && c_.ble->state() == BleState::Connected;
  xbot::Readback rb;
  const bool have = linked && c_.ble->getReadback(rb, now);
  const WriteStatus ws = c_.ble->writeStatus();
  const bool showWs = ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < kStatusShowMs);
  uint32_t k = 2166136261u;
  const uint32_t parts[16] = {
      demo ? 1u : 0u, linked ? 1u : 0u, (linked && c_.ble->writesUnlocked()) ? 1u : 0u, have ? 1u : 0u,
      (have && rb.lightValid) ? (rb.light ? 2u : 1u) : 0u, (have && rb.zeroValid) ? (rb.zero ? 2u : 1u) : 0u,
      (uint32_t)(have && rb.limValid[0] ? rb.limKmh[0] : 99), (uint32_t)(have && rb.limValid[1] ? rb.limKmh[1] : 99),
      (uint32_t)(have && rb.limValid[2] ? rb.limKmh[2] : 99), showWs ? (uint32_t)ws.state : 0u,
      (uint32_t)demoLim_[0], (uint32_t)demoLim_[1], (uint32_t)demoLim_[2],
      (c_.ctrl->light() ? 1u : 0u) | (c_.ctrl->zeroStart() ? 2u : 0u) | (c_.ctrl->hazard() ? 4u : 0u), (uint32_t)strlen(ws.note), 1u};
  for (int i = 0; i < 16; ++i) k = (k ^ parts[i]) * 16777619u;
  return k | 1u;
}

void DisplayUI::drawCtrl() {
  const uint32_t now = millis();
  ctrlKey_ = ctrlKey(now);
  holdDrawn_ = 0;
  drawHeader("RIDE CONTROLS");
  const bool demo = c_.store->get().demo;
  const bool linked = !demo && c_.ble->state() == BleState::Connected;
  const bool unlocked = linked && c_.ble->writesUnlocked();
  xbot::Readback rb;
  const bool have = linked && c_.ble->getReadback(rb, now);
  const bool active = demo || unlocked;
  const ScooterControl& c = *c_.ctrl;

  // speed limit per mode, like the sliders in the X-BOT app (1 km/h steps)
  char l[24];
  for (int j = 0; j < 3; ++j) {
    const int which = 2 - j;
    const int y = 38 + j * 30;
    tft_.setTextPadding(0);
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(cText_, cBg_);
    tft_.drawString(kLimName[j], 8, y + 6, 2);
    bool known = false;
    int v = 0;
    if (demo) { known = true; v = demoLim_[which]; }
    else if (have && rb.limValid[which]) { known = true; v = rb.limKmh[which]; }
    if (known) snprintf(l, sizeof(l), "%d km/h", v); else snprintf(l, sizeof(l), "-- km/h");
    tft_.setTextColor(known ? cOk_ : cDim_, cBg_);
    tft_.drawString(l, 72, y + 2, 4);
    drawBtn(limMinus(j), "-", active && known, cNeon_);
    drawBtn(limPlus(j), "+", active && known, cNeon_);
  }

  bool on0 = false, on1 = false, on2 = false, k0 = false, k1 = false;
  if (demo) { on0 = c.light(); on1 = c.zeroStart(); on2 = c.hazard(); k0 = k1 = true; }
  else if (have) { on0 = rb.light; k0 = rb.lightValid; on1 = rb.zero; k1 = rb.zeroValid; }
  snprintf(l, sizeof(l), "LIGHT %s", k0 ? (on0 ? "ON" : "OFF") : "--");
  drawBtn(togBtn(0), l, active && k0, on0 ? cOk_ : cNeon_);
  snprintf(l, sizeof(l), "ZERO %s", k1 ? (on1 ? "ON" : "OFF") : "--");
  drawBtn(togBtn(1), l, active && k1, on1 ? cOk_ : cNeon_);
  if (demo) {
    snprintf(l, sizeof(l), "HAZARD %s", on2 ? "ON" : "OFF");
    drawBtn(togBtn(2), l, true, on2 ? cWarn_ : cNeon_);
  } else {
    drawBtn(togBtn(2), "HAZARD N/A", false, cNeon_);  // no register known for it
  }

  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  const char* s1 = "";
  uint16_t c1 = cDim_;
  char buf[40];
  if (demo) { s1 = "DEMO: simulated, nothing is sent"; c1 = cWarn_; }
  else if (!linked) { s1 = "NOT SUPPORTED: no scooter link"; c1 = cBad_; }
  else {
    const WriteStatus ws = c_.ble->writeStatus();
    const bool show = ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < kStatusShowMs);
    if (show && ws.state == WriteState::Pending) { s1 = "SENDING..."; c1 = cWarn_; }
    else if (show && ws.state == WriteState::Confirmed) { s1 = "CONFIRMED by scooter"; c1 = cOk_; }
    else if (show && ws.state == WriteState::NotConfirmed) { s1 = "NOT CONFIRMED"; c1 = cBad_; }
    else if (show && ws.state == WriteState::Refused) { snprintf(buf, sizeof(buf), "REFUSED: %s", ws.note); s1 = buf; c1 = cBad_; }
    else if (unlocked) { s1 = "UNLOCKED: tap to send"; c1 = cWarn_; }
    else { s1 = "LOCKED: hold UNLOCK 1.5 s"; c1 = cDim_; }
  }
  tft_.setTextColor(c1, cBg_);
  tft_.drawString(s1, 160, 182, 2);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  if (linked) drawBtn(bottomBtn(2), unlocked ? "LOCK" : "UNLOCK", true, unlocked ? cWarn_ : cNeon_);
}

void DisplayUI::tickCtrl(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  const bool demo = c_.store->get().demo;
  const bool linked = !demo && c_.ble->state() == BleState::Connected;
  const bool unlocked = linked && c_.ble->writesUnlocked();

  if (ctrlKey(now) != ctrlKey_) drawCtrl();  // state changed: redraw

  // Unlock needs a deliberate 1.5 s hold.
  if (linked && !unlocked) {
    const Btn ub = bottomBtn(2);
    const bool inside = touching && inBtn(ub, mx, my);
    if (hold_.update(inside, now, kHoldMs)) {
      c_.ble->setWritesUnlocked(true);
      drawCtrl();
      return;
    }
    const int w = (int)(hold_.progress(now, kHoldMs) * (float)(ub.w - 8));
    if (w != holdDrawn_) {
      if (w > holdDrawn_) tft_.fillRect(ub.x + 4 + holdDrawn_, ub.y + ub.h - 6, w - holdDrawn_, 3, cNeon_);
      else tft_.fillRect(ub.x + 4 + w, ub.y + ub.h - 6, holdDrawn_ - w, 3, cPanel_);
      holdDrawn_ = w;
    }
  }

  if (ev.type != GestureType::Tap) return;
  if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
  if (linked && unlocked && inBtn(bottomBtn(2), ev.x, ev.y)) { c_.ble->setWritesUnlocked(false); drawCtrl(); return; }
  if (!demo && !unlocked) return;

  xbot::Readback rb;
  const bool have = !demo && c_.ble->getReadback(rb, now);
  for (int j = 0; j < 3; ++j) {
    const int which = 2 - j;
    int delta = 0;
    if (inBtn(limMinus(j), ev.x, ev.y)) delta = -1;
    else if (inBtn(limPlus(j), ev.x, ev.y)) delta = 1;
    if (delta == 0) continue;
    const int mx2 = xbot::limitMaxKmh(xbot::limitRegForIndex(which));
    if (demo) {
      int v = demoLim_[which] + delta;
      if (v < 6) v = 6;
      if (v > mx2) v = mx2;
      demoLim_[which] = v;
      drawCtrl();
    } else if (have && rb.limValid[which]) {
      const int v = rb.limKmh[which] + delta;
      if (v >= 6 && v <= mx2) c_.ble->requestWrite(WriteKind::ModeLimit, xbot::encodeLimitReq(which, v));
    }
  }
  if (demo) {
    ScooterControl& c = *c_.ctrl;
    bool changed = false;
    if (inBtn(togBtn(0), ev.x, ev.y)) { c.setLight(!c.light(), true); changed = true; }
    if (inBtn(togBtn(1), ev.x, ev.y)) { c.setZeroStart(!c.zeroStart(), true); changed = true; }
    if (inBtn(togBtn(2), ev.x, ev.y)) { c.setHazard(!c.hazard(), true); changed = true; }
    if (changed) drawCtrl();
    return;
  }
  if (have) {
    if (inBtn(togBtn(0), ev.x, ev.y) && rb.lightValid) c_.ble->requestWrite(WriteKind::Light, rb.light ? 0 : 1);
    if (inBtn(togBtn(1), ev.x, ev.y) && rb.zeroValid) c_.ble->requestWrite(WriteKind::ZeroStart, rb.zero ? 0 : 1);
  }
}

