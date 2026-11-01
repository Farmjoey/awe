#include "WidgetDraw.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "Format.h"

namespace {
const uint8_t kFontNo[6] = {1, 2, 4, 6, 7, 8};
const char* const kModeName[4] = {"WALK", "ECO", "DRIVE", "SPORT"};
const float kDeg = 0.0174533f;

uint16_t mix565(uint16_t a, uint16_t b, uint8_t t) {              // t = 0..255 share of b
  const uint32_t ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31, br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  const uint32_t r = (ar * (255 - t) + br * t) / 255, g = (ag * (255 - t) + bg * t) / 255, bl = (ab * (255 - t) + bb * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
uint8_t datumFor(uint8_t align) { return align == 1 ? TC_DATUM : (align == 2 ? TR_DATUM : TL_DATUM); }
int imin(int a, int b) { return a < b ? a : b; }
int imax(int a, int b) { return a > b ? a : b; }
int32_t hashStr(const char* s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return (int32_t)(h & 0x3FFFFFFF); }
}  // namespace

void WidgetRenderer::setLayout(const wl::Layout* L) {
  L_ = (L && L->valid) ? L : nullptr;
  page_ = 0;
  first_ = true;
  memset(st_, 0, sizeof(st_));
}

uint16_t WidgetRenderer::color(uint8_t idx) const {
  switch (idx) {
    case wl::P_TEXT: return pal_.text;
    case wl::P_NEON: return pal_.neon;
    case wl::P_OK: return pal_.ok;
    case wl::P_WARN: return pal_.warn;
    case wl::P_BAD: return pal_.bad;
    case wl::P_DIM: return pal_.dim;
    case wl::P_PANEL: return pal_.panel;
    case wl::P_BG: return pal_.bg;
    case wl::P_GLOW: return pal_.glow;
    case wl::P_WHITE: return 0xFFFF;
    case wl::P_BLACK: return 0x0000;
    default: break;
  }
  if (L_ && idx >= wl::P_CUSTOM && idx < wl::P_CUSTOM + L_->colorCount) return L_->colors[idx - wl::P_CUSTOM];
  return pal_.text;
}

// which: 1 = main colour, 2 = second colour. A threshold hit replaces the main colour.
uint16_t WidgetRenderer::itemColor(const wl::Item& it, uint8_t which, uint8_t sev) const {
  if (which == 2) return color(it.color2);
  if (sev == 1) return pal_.warn;
  if (sev == 2) return pal_.bad;
  return color(it.color);
}

// ---------------------------------------------------------------------------------------------- evaluation
void WidgetRenderer::eval(int i, const Telemetry& t, const wl::Stats& stt, uint8_t mode, bool animate) {
  const wl::Item& it = L_->items[i];
  State& s = st_[i];
  s.vis = wl::visible(it, page_, t, stt.live);
  float v = 0.0f;
  s.ok = false;
  s.sev = 0;
  if (it.type == wl::T_BUTTON || it.type == wl::T_RECT || it.type == wl::T_CIRCLE || it.type == wl::T_LINE || it.type == wl::T_TEXT) {
    // no value
  } else if (it.type == wl::T_ICON && it.src == wl::S_NONE) {
  } else {
    s.ok = wl::valueFor(it.src, t, stt.live, v);
    if (s.ok && !wl::isStatus(it.src) && it.src != wl::S_MODE) s.sev = wl::severity(it, v);
  }
  s.val = v;
  int32_t key = 0;
  switch (it.type) {
    case wl::T_TEXT:
      key = 1;
      break;
    case wl::T_NUM: {
      char n[16];
      if (it.src == wl::S_MODE) snprintf(s.txt, sizeof(s.txt), "%s", (mode < 4) ? kModeName[mode] : "--");
      else if (!s.ok) snprintf(s.txt, sizeof(s.txt), "--");
      else if (wl::isStatus(it.src)) snprintf(s.txt, sizeof(s.txt), "%s", v > 0.5f ? "ON" : "OFF");
      else {
        fmtFixed(n, sizeof(n), true, v, it.dp);
        const bool small = it.font <= 2;                                  // digit-only fonts cannot draw letters
        snprintf(s.txt, sizeof(s.txt), "%s%s%s", (small && it.str2 != wl::kNoStr) ? L_->str[it.str2] : "", n,
                 (small && it.str != wl::kNoStr) ? L_->str[it.str] : "");
      }
      key = hashStr(s.txt) ^ ((int32_t)s.sev << 28) ^ (s.ok ? 0 : 0x10000000);
      break;
    }
    case wl::T_BAR:
    case wl::T_ARC:
    case wl::T_SEGBAR: {
      float target = 0.0f;
      if (s.ok) target = wl::fractionRange(v, it.minv, it.maxv);
      if (animate && it.type != wl::T_SEGBAR) {
        s.disp += (target - s.disp) * 0.35f;
        if (fabsf(target - s.disp) < 0.004f) s.disp = target;
      } else s.disp = target;
      int q;
      if (it.type == wl::T_BAR) q = (int)(s.disp * (float)imax(it.w, it.h));
      else if (it.type == wl::T_ARC) q = (int)(s.disp * (float)it.sweep * 2.0f + 0.5f);
      else q = (int)(s.disp * (float)it.thick + 0.5f);
      key = q | ((int32_t)s.sev << 20) | (s.ok ? 0 : (1 << 24));
      break;
    }
    case wl::T_SPARK:
      key = (int32_t)(stt.lastHistMs & 0x3FFFFF) | ((int32_t)s.sev << 28);
      break;
    case wl::T_DOT:
      key = (s.ok && v > 0.5f ? 1 : 0) | ((int32_t)s.sev << 4);
      break;
    case wl::T_ICON:
      key = (int32_t)s.sev | ((int32_t)(s.ok ? (int)(wl::fractionRange(v, it.minv, it.maxv) * 8.0f) : 8) << 4);
      break;
    case wl::T_BUTTON: {
      const uint8_t a = it.src;
      key = (a >= wl::A_WALK && a <= wl::A_SPORT && mode == (uint8_t)(a - wl::A_WALK)) ? 1 : 0;
      if (a == wl::A_NEXTPAGE || a == wl::A_PREVPAGE) key = 0;
      break;
    }
    default:
      key = 0;
      break;
  }
  s.key = key | (s.vis ? 0x40000000 : 0);
}

// ---------------------------------------------------------------------------------------------- geometry
WidgetRenderer::Box WidgetRenderer::bbox(int i) const {
  const wl::Item& it = L_->items[i];
  const State& s = st_[i];
  Box b = {it.x, it.y, (int16_t)it.w, (int16_t)it.h};
  switch (it.type) {
    case wl::T_ARC: {
      const int r = it.w + (it.flags & wl::F_TICKS ? 10 : 2);
      b.x = (int16_t)(it.x - r); b.y = (int16_t)(it.y - r); b.w = (int16_t)(r * 2); b.h = (int16_t)(r * 2);
      break;
    }
    case wl::T_CIRCLE: case wl::T_DOT: {
      const int r = it.w + 2;
      b.x = (int16_t)(it.x - r); b.y = (int16_t)(it.y - r); b.w = (int16_t)(r * 2); b.h = (int16_t)(r * 2);
      break;
    }
    case wl::T_ICON: b.w = (int16_t)(it.w + 2); b.h = (int16_t)(it.w + 2); break;
    case wl::T_LINE: b.x = (int16_t)(it.x - it.thick); b.y = (int16_t)(it.y - it.thick); b.w = (int16_t)(it.w + it.thick * 2 + 1); b.h = (int16_t)(it.h + it.thick * 2 + 1); break;
    case wl::T_TEXT: case wl::T_NUM: {
      const uint8_t f = kFontNo[it.font];
      int w = s.boxW;
      if (w < 8) w = 8;
      const int h = (f == 1) ? 8 : (f == 2 ? 16 : (f == 4 ? 26 : (f == 6 ? 48 : (f == 7 ? 48 : 75))));
      b.y = it.y; b.h = (int16_t)(h + 2); b.w = (int16_t)(w + 2);
      b.x = (int16_t)(it.align == 1 ? it.x - w / 2 - 1 : (it.align == 2 ? it.x - w - 1 : it.x - 1));
      break;
    }
    default: break;
  }
  if (it.type == wl::T_BAR || it.type == wl::T_RECT || it.type == wl::T_BUTTON || it.type == wl::T_SEGBAR || it.type == wl::T_SPARK) { b.w = (int16_t)(b.w + 1); b.h = (int16_t)(b.h + 1); }
  return b;
}

static bool overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// ---------------------------------------------------------------------------------------------- drawing
void WidgetRenderer::arcSeg(int cx, int cy, int ro, int ri, float aStart, float aLen, uint16_t fg, uint16_t bg, bool caps) {
  if (aLen < 1.0f) return;
  if (aLen > 360.0f) aLen = 360.0f;
  float t0 = fmodf(aStart - 90.0f + 720.0f, 360.0f);          // TFT_eSPI counts from 6 o'clock, clockwise
  const float t1 = t0 + aLen;
  if (t1 <= 360.0f) tft_->drawSmoothArc(cx, cy, ro, ri, (uint32_t)(t0 + 0.5f), (uint32_t)(t1 + 0.5f), fg, bg, caps);
  else {
    tft_->drawSmoothArc(cx, cy, ro, ri, (uint32_t)(t0 + 0.5f), 360, fg, bg, false);
    tft_->drawSmoothArc(cx, cy, ro, ri, 0, (uint32_t)(t1 - 360.0f + 0.5f), fg, bg, false);
  }
}

void WidgetRenderer::icon(int id, int x, int y, int s, uint16_t col, float frac, uint16_t bg) {
  const float u = (float)s / 24.0f;
  auto P = [&](float v) { return (int)lroundf(v * u); };
  switch (id) {
    case 0:                                                    // bolt
      tft_->fillTriangle(x + P(14), y + P(1), x + P(5), y + P(14), x + P(12), y + P(14), col);
      tft_->fillTriangle(x + P(12), y + P(14), x + P(10), y + P(23), x + P(19), y + P(9), col);
      tft_->fillTriangle(x + P(10), y + P(10), x + P(19), y + P(9), x + P(12), y + P(14), col);
      break;
    case 1:                                                    // battery, level from the source
      tft_->fillSmoothRoundRect(x + P(1), y + P(6), P(19), P(12), P(3), col, bg);
      tft_->fillSmoothRoundRect(x + P(2.2f), y + P(7.2f), P(16.6f), P(9.6f), P(2), bg, col);
      tft_->fillRect(x + P(21), y + P(10), P(2.5f) + 1, P(4), col);
      if (frac > 0.02f) tft_->fillRect(x + P(4), y + P(9), imax(1, (int)lroundf((float)P(13.5f) * frac)), P(6), col);
      break;
    case 2:                                                    // thermometer
      tft_->fillSmoothRoundRect(x + P(9), y + P(1), P(6), P(16), P(3), col, bg);
      tft_->fillSmoothRoundRect(x + P(10.5f), y + P(2.5f), P(3), P(13), P(1.5f), bg, col);
      tft_->fillSmoothCircle(x + P(12), y + P(19), P(4.5f), col, bg);
      tft_->fillRect(x + P(11), y + P(8), P(2) + 1, P(11), col);
      break;
    case 3:                                                    // gauge
      tft_->drawSmoothArc(x + P(12), y + P(14), P(11), P(9), 45, 315, col, bg, true);
      tft_->drawWideLine((float)(x + P(12)), (float)(y + P(14)), (float)(x + P(18)), (float)(y + P(7)), 2.0f, col, bg);
      break;
    case 4:                                                    // warning triangle
      tft_->drawWideLine((float)(x + P(12)), (float)(y + P(2)), (float)(x + P(23)), (float)(y + P(21)), 2.0f, col, bg);
      tft_->drawWideLine((float)(x + P(23)), (float)(y + P(21)), (float)(x + P(1)), (float)(y + P(21)), 2.0f, col, bg);
      tft_->drawWideLine((float)(x + P(1)), (float)(y + P(21)), (float)(x + P(12)), (float)(y + P(2)), 2.0f, col, bg);
      tft_->fillRect(x + P(11), y + P(8), P(2) + 1, P(7), col);
      tft_->fillRect(x + P(11), y + P(17), P(2) + 1, P(2) + 1, col);
      break;
    case 5:                                                    // lock
      tft_->fillSmoothRoundRect(x + P(4), y + P(11), P(16), P(11), P(2), col, bg);
      tft_->drawSmoothArc(x + P(12), y + P(10), P(7), P(5), 180, 360, col, bg, false);
      break;
    case 6:                                                    // phone
      tft_->fillSmoothRoundRect(x + P(6), y + P(1), P(12), P(22), P(3), col, bg);
      tft_->fillSmoothRoundRect(x + P(7.6f), y + P(3.5f), P(8.8f), P(15), P(1), bg, col);
      break;
    default:                                                   // light
      tft_->fillSmoothCircle(x + P(12), y + P(12), P(5), col, bg);
      for (int k = 0; k < 8; ++k) {
        const float a = (float)k * 0.785398f;
        tft_->drawWideLine((float)(x + P(12)) + cosf(a) * (float)P(8), (float)(y + P(12)) + sinf(a) * (float)P(8),
                           (float)(x + P(12)) + cosf(a) * (float)P(11), (float)(y + P(12)) + sinf(a) * (float)P(11), 2.0f, col, bg);
      }
      break;
  }
}

void WidgetRenderer::drawItem(int i, uint8_t mode, const wl::Stats& stt) {
  (void)mode;
  const wl::Item& it = L_->items[i];
  State& s = st_[i];
  if (!s.vis) return;
  const uint16_t bg = color(L_->bg);
  const uint16_t c1 = itemColor(it, 1, s.sev), c2 = itemColor(it, 2, 0);
  const int x = it.x, y = it.y, w = it.w, h = it.h;
  switch (it.type) {
    case wl::T_TEXT: {
      const uint8_t f = kFontNo[it.font];
      tft_->setTextPadding(0);
      tft_->setTextColor(c1);                                   // transparent: layers show through
      tft_->setTextDatum(datumFor(it.align));
      tft_->drawString(L_->str[it.str], x, y, f);
      break;
    }
    case wl::T_NUM: {
      uint8_t font = it.font;
      uint16_t col = c1;
      if (!s.ok && it.src != wl::S_MODE) { col = pal_.dim; if (font > 2) font = 2; }
      tft_->setTextPadding(0);
      tft_->setTextColor(col);
      tft_->setTextDatum(datumFor(it.align));
      tft_->drawString(s.txt, x, y, kFontNo[font]);
      break;
    }
    case wl::T_BAR: {
      const bool vert = h > w;
      int r = it.thick;
      if (r > imin(w, h) / 2) r = imin(w, h) / 2;
      const bool border = (it.flags & wl::F_BORDER) != 0;
      const uint16_t edge = mix565(bg, pal_.text, 128);
      int inset = 0;
      if (border) {
        tft_->fillSmoothRoundRect(x, y, w, h, r, edge, bg);
        tft_->fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, imax(0, r - 1), c2, edge);
        inset = 2;
      } else if (r > 0) tft_->fillSmoothRoundRect(x, y, w, h, r, c2, bg);
      else tft_->fillRect(x, y, w, h, c2);
      const int len = (vert ? h : w) - inset * 2;
      float a = 0.0f, b = s.disp;
      if (it.flags & wl::F_BIDIR) {
        const float z = wl::fractionRange(0.0f, it.minv, it.maxv);
        a = z < s.disp ? z : s.disp; b = z < s.disp ? s.disp : z;
        if (!s.ok) { a = b = z; }
      } else if (it.flags & wl::F_REV) { a = 1.0f - s.disp; b = 1.0f; }
      const int p0 = (int)lroundf(a * (float)len), p1 = (int)lroundf(b * (float)len);
      if (p1 - p0 >= 1) {
        const int thickDim = (vert ? w : h) - inset * 2;
        int rr = imin(imax(0, r - inset), imin((p1 - p0) / 2, thickDim / 2));
        const uint16_t under = border ? c2 : c2;
        if (vert) {
          if (rr > 0) tft_->fillSmoothRoundRect(x + inset, y + inset + len - p1, thickDim, p1 - p0, rr, c1, under);
          else tft_->fillRect(x + inset, y + inset + len - p1, thickDim, p1 - p0, c1);
        } else {
          if (rr > 0) tft_->fillSmoothRoundRect(x + inset + p0, y + inset, p1 - p0, thickDim, rr, c1, under);
          else tft_->fillRect(x + inset + p0, y + inset, p1 - p0, thickDim, c1);
        }
      }
      break;
    }
    case wl::T_ARC: {
      const int ro = w, ri = w - h;
      const float a0 = (float)it.a0 * 2.0f, sw = (float)it.sweep * 2.0f;
      const bool caps = (it.flags & wl::F_CAPS) != 0, rev = (it.flags & wl::F_REV) != 0;
      arcSeg(x, y, ro, ri, a0, sw, c2, bg, caps);
      const float fl = s.disp * sw;
      if (fl >= 1.0f) {
        if (rev) arcSeg(x, y, ro, ri, a0 + sw - fl, fl, c1, c2, caps);
        else arcSeg(x, y, ro, ri, a0, fl, c1, c2, caps);
      }
      if (it.flags & wl::F_TICKS) {
        for (int k = 0; k <= 10; ++k) {
          const float a = (a0 + sw * (float)k / 10.0f) * kDeg;
          tft_->drawWideLine((float)x + cosf(a) * (float)(ro + 3), (float)y + sinf(a) * (float)(ro + 3),
                             (float)x + cosf(a) * (float)(ro + 8), (float)y + sinf(a) * (float)(ro + 8), 2.0f, mix565(bg, pal_.text, 140), bg);
        }
      }
      if (it.flags & wl::F_KNOB) {
        const float a = (rev ? a0 + sw - fl : a0 + fl) * kDeg, rm = (float)(ro + ri) * 0.5f;
        tft_->fillSmoothCircle(x + (int)lroundf(cosf(a) * rm), y + (int)lroundf(sinf(a) * rm), imax(4, (h * 4) / 5), pal_.text, c1);
      }
      break;
    }
    case wl::T_RECT: {
      int r = it.thick;
      if (r > imin(w, h) / 2) r = imin(w, h) / 2;
      const bool border = (it.flags & wl::F_BORDER) != 0, fill = (it.flags & wl::F_FILL) != 0;
      if (border) {
        tft_->fillSmoothRoundRect(x, y, w, h, r, c1, bg);
        tft_->fillSmoothRoundRect(x + 2, y + 2, w - 4, h - 4, imax(0, r - 2), fill ? c2 : bg, c1);
      } else tft_->fillSmoothRoundRect(x, y, w, h, r, fill ? c1 : c2, bg);
      break;
    }
    case wl::T_CIRCLE: {
      const bool border = (it.flags & wl::F_BORDER) != 0;
      tft_->fillSmoothCircle(x, y, w, c1, bg);
      if (border && it.thick > 0 && it.thick < w) tft_->fillSmoothCircle(x, y, w - it.thick, (it.flags & wl::F_FILL) ? c2 : bg, c1);
      break;
    }
    case wl::T_LINE:
      if (it.flags & wl::F_REV) tft_->drawWideLine((float)x, (float)(y + h), (float)(x + w), (float)y, (float)it.thick, c1, bg);
      else tft_->drawWideLine((float)x, (float)y, (float)(x + w), (float)(y + h), (float)it.thick, c1, bg);
      break;
    case wl::T_BUTTON: {
      const bool sel = s.key & 1;
      int r = it.thick ? it.thick : 9;
      if (r > imin(w, h) / 2) r = imin(w, h) / 2;
      const uint16_t fill = sel ? c2 : ((it.flags & wl::F_FILL) ? mix565(bg, c2, 90) : bg);
      tft_->fillSmoothRoundRect(x, y, w, h, r, c1, bg);
      tft_->fillSmoothRoundRect(x + 2, y + 2, w - 4, h - 4, imax(0, r - 2), fill, c1);
      tft_->setTextPadding(0);
      tft_->setTextColor(sel ? bg : c1);
      tft_->setTextDatum(MC_DATUM);
      tft_->drawString(L_->str[it.str], x + w / 2, y + h / 2 + 1, kFontNo[it.font]);
      break;
    }
    case wl::T_SEGBAR: {
      const int n = it.thick;
      int gap = (int)lroundf((float)w / (float)n * 0.18f);
      if (gap < 1) gap = 1;
      const int sw = (w - gap * (n - 1)) / n;
      if (sw < 1) break;
      const int lit = (int)(s.disp * (float)n + 0.5f);
      const bool rnd = (it.flags & wl::F_ROUND) != 0;
      const int rr = rnd ? imin(3, sw / 2) : 0;
      for (int k = 0; k < n; ++k) {
        const uint16_t c = k < lit ? c1 : c2;
        if (rr > 0) tft_->fillSmoothRoundRect(x + k * (sw + gap), y, sw, h, rr, c, bg);
        else tft_->fillRect(x + k * (sw + gap), y, sw, h, c);
      }
      break;
    }
    case wl::T_SPARK: {
      tft_->drawRect(x, y, w, h, c2);
      const int N = wl::kHistN, have = stt.histCount;
      if (have < 2 || it.src >= wl::kHistSrc) break;
      const float* hs = stt.hist[it.src];
      int px = 0, py = 0;
      for (int k = N - have; k < N; ++k) {
        const int cx = x + (int)((float)(w - 1) * (float)k / (float)(N - 1));
        const int cy = y + h - 1 - (int)((float)(h - 2) * wl::fractionRange(hs[k], it.minv, it.maxv));
        if (k > N - have) {
          if (it.flags & wl::F_FILL) {
            const uint16_t fc = mix565(bg, c1, 90);
            for (int xx = px; xx <= cx; ++xx) {
              const int yy = (cx == px) ? cy : py + (cy - py) * (xx - px) / (cx - px);
              tft_->drawFastVLine(xx, yy, y + h - 1 - yy, fc);
            }
          }
        }
        px = cx; py = cy;
      }
      px = 0; py = 0;
      for (int k = N - have; k < N; ++k) {
        const int cx = x + (int)((float)(w - 1) * (float)k / (float)(N - 1));
        const int cy = y + h - 1 - (int)((float)(h - 2) * wl::fractionRange(hs[k], it.minv, it.maxv));
        if (k > N - have) {
          if (it.thick > 1) tft_->drawWideLine((float)px, (float)py, (float)cx, (float)cy, (float)it.thick, c1, bg);
          else tft_->drawLine(px, py, cx, cy, c1);
        }
        px = cx; py = cy;
      }
      break;
    }
    case wl::T_DOT:
      tft_->fillSmoothCircle(x, y, w, (s.ok && s.val > 0.5f) ? c1 : c2, bg);
      break;
    case wl::T_ICON:
      icon(it.thick, x, y, w, c1, s.ok ? wl::fractionRange(s.val, it.minv, it.maxv) : 1.0f, bg);
      break;
    default:
      break;
  }
}

void WidgetRenderer::redrawRegion(const Box& b0, uint8_t mode, const wl::Stats& stt) {
  int x0 = b0.x, y0 = b0.y, x1 = b0.x + b0.w, y1 = b0.y + b0.h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > 320) x1 = 320;
  if (y1 > 240) y1 = 240;
  if (x1 <= x0 || y1 <= y0) return;
  tft_->setViewport(x0, y0, x1 - x0, y1 - y0, false);
  tft_->fillRect(x0, y0, x1 - x0, y1 - y0, color(L_->bg));
  for (int j = 0; j < L_->count; ++j) {
    if (!st_[j].vis) continue;
    const Box bj = bbox(j);
    if (overlap(x0, y0, x1 - x0, y1 - y0, bj.x, bj.y, bj.w, bj.h)) drawItem(j, mode, stt);
  }
  tft_->resetViewport();
}

// ---------------------------------------------------------------------------------------------- public
void WidgetRenderer::drawAll(const Telemetry& t, const wl::Stats& stt, uint8_t mode) {
  if (!L_ || !tft_) return;
  tft_->fillScreen(color(L_->bg));
  for (int i = 0; i < L_->count; ++i) {
    st_[i].disp = 0.0f;
    st_[i].boxW = 0;
    eval(i, t, stt, mode, false);
    const wl::Item& it = L_->items[i];
    if ((it.type == wl::T_TEXT || it.type == wl::T_NUM) && st_[i].vis) {
      const char* s = it.type == wl::T_TEXT ? L_->str[it.str] : st_[i].txt;
      st_[i].boxW = (int16_t)tft_->textWidth(s, kFontNo[it.font]);
    }
  }
  for (int i = 0; i < L_->count; ++i) drawItem(i, mode, stt);
  lastMode_ = mode;
  first_ = false;
}

void WidgetRenderer::update(const Telemetry& t, const wl::Stats& stt, uint8_t mode) {
  if (!L_ || !tft_) return;
  if (first_) { drawAll(t, stt, mode); return; }
  int32_t oldKey[wl::kMaxItems];
  bool changed[wl::kMaxItems];
  bool any = false;
  for (int i = 0; i < L_->count; ++i) {
    oldKey[i] = st_[i].key;
    eval(i, t, stt, mode, true);
    const wl::Item& it = L_->items[i];
    if ((it.type == wl::T_NUM) && st_[i].vis) {                       // keep the clear-box as wide as the widest text so far
      const int w = tft_->textWidth(st_[i].txt, kFontNo[it.font]);
      if (w > st_[i].boxW) st_[i].boxW = (int16_t)w;
    } else if (it.type == wl::T_TEXT && st_[i].vis && st_[i].boxW == 0) st_[i].boxW = (int16_t)tft_->textWidth(L_->str[it.str], kFontNo[it.font]);
    changed[i] = st_[i].key != oldKey[i];
    any = any || changed[i];
  }
  lastMode_ = mode;
  if (!any) return;
  for (int i = 0; i < L_->count; ++i) if (changed[i]) redrawRegion(bbox(i), mode, stt);
}

int WidgetRenderer::buttonAt(int16_t x, int16_t y) const {
  if (!L_) return -1;
  for (int i = L_->count - 1; i >= 0; --i) {
    const wl::Item& it = L_->items[i];
    if (it.type != wl::T_BUTTON || !st_[i].vis) continue;
    if (x >= it.x && x < it.x + (int)it.w && y >= it.y && y < it.y + (int)it.h) return i;
  }
  return -1;
}

void WidgetRenderer::forceRedraw(int i) { if (L_ && i >= 0 && i < L_->count) st_[i].key = -1; }

void WidgetRenderer::drawProgress(int i, float f, uint16_t colr) {
  if (!L_ || !tft_ || i < 0 || i >= L_->count) return;
  const wl::Item& it = L_->items[i];
  const int w = (int)(f * (float)(it.w - 16));
  if (w > 0) tft_->fillRect(it.x + 8, it.y + it.h - 8, w, 3, colr);
}
