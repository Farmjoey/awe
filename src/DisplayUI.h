#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Telemetry.h"
#include "Gesture.h"
#include "TouchCal.h"
#include "TouchInput.h"
#include "SettingsStorage.h"
#include "SafetyProfile.h"
#include "ScooterBLE.h"
#include "PhoneMedia.h"
#include "Notifications.h"
#include "ScooterControl.h"
#include "AppLink.h"
#include "Theme.h"
#include "WidgetLayout.h"
#include "StartCap.h"
#include "WidgetDraw.h"

enum class Screen : uint8_t { Cal, Hw, Dash, Music, Msg, Settings, Safety, Diag, Ctrl, Update, Secret };

struct UiContext {
  SettingsStorage* store;
  ScooterBLE* ble;
  PhoneMedia* phone;
  Notifications* notes;
  SafetyProfile* safety;
  ScooterControl* ctrl;
};

class DisplayUI {
 public:
  struct Btn { int16_t x, y, w, h; };
  void begin(const UiContext& ctx);
  void update(const Telemetry& t, const TouchState& ts, uint32_t now);
  void onBootButton(bool longPress);  // short = toggle inversion, long = flip 180 deg
  int capState() const { return capState_; }   // 1 = 20 km/h cap active, 0 = not capped, -1 = unknown

 private:
  // lifecycle / helpers
  void go(Screen s);
  void applyDisplaySettings();
  void setBacklight(uint8_t pct);
  void splash();
  void flipRotation();
  void toggleInvert();
  void startCal(Screen returnTo);
  void txt(uint8_t slot, int16_t x, int16_t y, uint8_t font, const char* s,
           uint16_t fg, uint8_t datum, int16_t padW);
  void drawBtn(const Btn& b, const char* label, bool enabled, uint16_t accent, bool selected = false);
  void drawCard(const Btn& b, const char* title, const char* value, uint16_t valueCol);
  uint16_t mix(uint16_t a, uint16_t b, uint8_t t) const;   // t = 0..255 towards b
  void applyColorMode();
  void toggleColors();
  void drawBottom();
  void drawMediaButtons();
  void drawHeader(const char* title);
  void drawFull();

  // screens
  void drawCal();      void tickCal(const TouchState& ts, uint32_t now);
  void drawHw();       void tickHw(bool touching, int16_t mx, int16_t my, const GestureEvent& ev);
  void drawDash();     void tickDash(const Telemetry& t, bool touching, int16_t mx, int16_t my,
                                const GestureEvent& ev, uint32_t now);
  void drawSeg(int i, bool on);
  void drawSegAt(int cx, int cy, int r0, int r1, int i, uint16_t col);
  uint16_t wcol(uint8_t idx) const;
  bool useCustom() const { return layoutOk_ && c_.store->get().layoutOn; }
  void reloadLayout();
  void tickDashInput(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now, bool showModes);
  void drawModeButtons();
  void modeState(uint32_t now, int& active, bool& enabled);
  void drawMusic();    void tickMusic(const GestureEvent& ev);
  void drawMsg();      void tickMsg(const GestureEvent& ev);
  void drawSettings(); void tickSettings(const GestureEvent& ev);
  void drawSafety();   void tickSafety(bool touching, int16_t mx, int16_t my,
                                       const GestureEvent& ev, uint32_t now);
  void drawSafetyBody(LimitStatus st, bool armed);
  void tickSafetyReal(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now);
  void drawSafetyReal(uint32_t now);
  uint32_t safetyRealKey(uint32_t now);
  void applyTheme(uint8_t idx);
  void drawUpdate();   void tickUpdate(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now);
  void drawCtrl();     void tickCtrl(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now);
  uint32_t ctrlKey(uint32_t now);
  void drawDiag();     void tickDiag(const GestureEvent& ev, uint32_t now);
  void hwButtons();
  // reference-style dashboard (arc gauge, pill bars, outlined buttons), start cap and service menu
  void tickCustom(const Telemetry& t, bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now);
  void drawDashRef();
  void tickDashRef(const Telemetry& t, bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now);
  void tickRowInput(const Telemetry& t, bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now);
  void drawRow();
  void drawRefBar(int y, int h, int fx0, int fx1, uint16_t col);
  void drawRefBtn(const Btn& b, const char* label, uint8_t font, uint16_t edge, uint16_t fill, uint16_t txtc, int r);
  void paintArc(int a0, int a1, int fillA);
  void drawKnob(int fillA);
  void drawCapNotch();
  void drawSecret();   void tickSecret(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now);
  void capTick(uint32_t now);
  void onStartCapChanged();
  void drawCalTarget();

  TFT_eSPI tft_;
  UiContext c_;
  Screen screen_ = Screen::Dash;
  bool full_ = true;
  GestureDetector gest_;
  HoldTracker hold_;
  TouchCal cal_;

  // colours (set in begin)
  uint16_t cBg_ = 0, cPanel_ = 0, cNeon_ = 0, cGlow_ = 0, cDim_ = 0, cWarn_ = 0, cOk_ = 0, cBad_ = 0, cText_ = 0;

  // text cache (redraw only on change)
  char cache_[32][44];
  uint16_t cacheCol_[32];

  // per-screen state
  uint8_t hwPage_ = 0;
  uint8_t calStep_ = 0;
  RawPt calPts_[3];
  bool calTouching_ = false, calWaitRelease_ = false;
  uint32_t calT0_ = 0;
  long calSumX_ = 0, calSumY_ = 0;
  int calN_ = 0;
  char calMsg_[40];
  Screen calReturn_ = Screen::Dash;
  float gauge_ = 0;
  int gaugeW_ = 0;
  uint8_t safetyKey_ = 255;
  int holdDrawn_ = 0;
  uint32_t lastDiagMs_ = 0;
  uint32_t ctrlKey_ = 0;
  int musicKey_ = -1;
  uint8_t bottomPage_ = 0;      // 0 = mode buttons, 1 = music buttons
  uint8_t settingsPage_ = 0;
  int mediaKey_ = -1;
  uint8_t bootImg_[1024];
  bool bootImgOk_ = false;
  int updKey_ = -1, updBarW_ = -1;
  wl::Layout layout_;
  bool layoutOk_ = false;
  uint32_t safetyRealKey_ = 0;
  bool safetyWasReal_ = false;
  HoldTracker holdLock_;
  HoldTracker holdMode_;
  int holdModeIdx_ = -1, holdModeDrawn_ = 0, segOn_ = 0, battW_ = -1, modeKey_ = -2;
  uint16_t battCol_ = 0;
  int pwrW_ = -1;
  uint16_t pwrCol_ = 0;
  int demoLim_[3] = {25, 20, 15};  // demo only: sport, drive, eco
  int lockDrawn_ = 0;
  // custom dashboard (widgets 2.0)
  WidgetRenderer wr_;
  wl::Stats stats_;
  HoldTracker custHold_, custSec_;
  int custBtn_ = -1;
  // reference-style dashboard
  uint16_t cTrk_ = 0, cEdge_ = 0, cArc_ = 0, cArcHi_ = 0, cBtn_ = 0, cTag_ = 0;
  uint8_t rowPage_ = 0;                 // 0 = SPORT/RACE, 1 = four modes, 2 = music
  uint32_t rowKey_ = 0xFFFFFFFFu;
  float arcMax_ = 35.0f;                // full-scale of the arc: grows with the highest limit / speed
  int arcFill_ = -1, barPwKey_ = -99999, barBtKey_ = -99999;
  uint16_t dotCol_ = 0, barBtCol_ = 0;
  HoldTracker holdSec_;                 // SPORT + throttle: service menu
  int secDrawn_ = 0, sportDrawn_ = 0;
  // start cap (20 km/h at every power-on) and service menu
  int capState_ = -1;
  bool capReleased_ = false, capWasLinked_ = false, capDemo_ = false, capLastStart_ = false;
  uint32_t capTryMs_ = 0;
  uint8_t capTries_ = 0;
  uint32_t secretKey_ = 0;
};
