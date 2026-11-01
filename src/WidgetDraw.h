#pragma once
// Draws a custom dashboard layout (wl::Layout, "widgets 2.0") with anti-aliased shapes.
// Only what changed is redrawn: the area of a changed item is cleared and every visible item that overlaps it is
// drawn again in layout order, clipped to that area, so layers always look the same as after a full redraw.
#include <TFT_eSPI.h>
#include "WidgetLayout.h"

struct WPalette { uint16_t text, neon, ok, warn, bad, dim, panel, bg, glow; };

class WidgetRenderer {
 public:
  void attach(TFT_eSPI* tft) { tft_ = tft; }
  void setPalette(const WPalette& p) { pal_ = p; }
  void setLayout(const wl::Layout* L);                       // nullptr = none
  bool hasLayout() const { return L_ != nullptr; }
  uint8_t page() const { return page_; }
  uint8_t pages() const { return L_ ? L_->pages : 1; }
  void invalidate() { first_ = true; }                       // next update() redraws everything
  void setPage(uint8_t p) { if (L_ && p < L_->pages) page_ = p; }
  // Full redraw (background + everything visible). mode: 0..3 = walk..sport, 255 = unknown.
  void drawAll(const Telemetry& t, const wl::Stats& st, uint8_t mode);
  // Redraws what changed since the last call.
  void update(const Telemetry& t, const wl::Stats& st, uint8_t mode);
  int buttonAt(int16_t x, int16_t y) const;                  // visible button under the point, or -1
  uint8_t buttonAction(int i) const { return L_->items[i].src; }
  void forceRedraw(int i);                                   // next update() redraws item i (clears a hold bar)
  void drawProgress(int i, float f, uint16_t color);         // hold bar inside a button
  uint16_t color(uint8_t idx) const;

 private:
  struct State { int32_t key; float disp; bool vis, ok; uint8_t sev; int16_t boxW; char txt[28]; float val; };
  struct Box { int16_t x, y, w, h; };
  TFT_eSPI* tft_ = nullptr;
  const wl::Layout* L_ = nullptr;
  WPalette pal_ = {0xFFFF, 0x07FF, 0x07E0, 0xFD20, 0xF800, 0x8410, 0x2104, 0x0000, 0x0210};
  uint8_t page_ = 0;
  State st_[wl::kMaxItems];
  uint8_t lastMode_ = 255;
  bool first_ = true;

  void eval(int i, const Telemetry& t, const wl::Stats& stt, uint8_t mode, bool animate);
  Box bbox(int i) const;
  void drawItem(int i, uint8_t mode, const wl::Stats& stt);
  void redrawRegion(const Box& b, uint8_t mode, const wl::Stats& stt);
  uint16_t itemColor(const wl::Item& it, uint8_t which, uint8_t sev) const;
  void arcSeg(int cx, int cy, int ro, int ri, float aStart, float aLen, uint16_t fg, uint16_t bg, bool caps);
  void icon(int id, int x, int y, int s, uint16_t col, float frac, uint16_t bg);
};
