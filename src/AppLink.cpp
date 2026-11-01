#include "AppLink.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Update.h>
#include <freertos/stream_buffer.h>
#include <string.h>
#include "AppProtocol.h"
#include "BoardConfig.h"
#include "Diagnostics.h"
#include "PhoneMedia.h"
#include "ScooterBLE.h"
#include "SettingsStorage.h"
#include "Theme.h"
#include "WidgetLayout.h"

namespace applink {
namespace {

const char* kSvc = "4e4f5641-5249-4445-0000-000000000001";
const char* kCtrl = "4e4f5641-5249-4445-0000-000000000002";
const char* kData = "4e4f5641-5249-4445-0000-000000000003";
const char* kStat = "4e4f5641-5249-4445-0000-000000000004";

ScooterBLE* s_ble = nullptr;
PhoneMedia* s_phone = nullptr;
SettingsStorage* s_store = nullptr;
NimBLECharacteristic* s_stat = nullptr;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t s_windowUntil = 0;  // 0 = closed
volatile uint8_t s_otaState = app::OTA_IDLE;
volatile uint8_t s_otaErr = app::E_NONE;
volatile uint32_t s_recv = 0, s_total = 0, s_lastData = 0;
volatile bool s_overrun = false;
uint32_t s_expectCrc = 0, s_crc = 0, s_lastNotified = 0;
uint8_t s_head[16];
size_t s_headLen = 0;
bool s_gotHeader = false;
StreamBufferHandle_t s_sb = nullptr;

// changes requested by the app, applied on the main thread
int s_pTheme = -1, s_pStyle = -1, s_pMusic = -1;
bool s_pText = false, s_pImg = false, s_pPreview = false, s_pLayout = false;
int s_pLayUse = -1;
int s_pStartCap = -1;
int s_pBright = -1, s_pArc = -1, s_pAccent = 0;
uint8_t s_pAcc[3];
uint32_t s_lastTelem = 0;
uint8_t s_lay[wl::kMaxBytes];
size_t s_layGot = 0;
char s_pTextBuf[16];
uint8_t s_img[app::kBootImgBytes];
size_t s_imgGot = 0;

void notify(const uint8_t* d, size_t n) {
  if (!s_stat || n == 0) return;
  s_stat->setValue(d, n);
  s_stat->notify();
}
void notifyOta() {
  uint8_t f[12];
  notify(f, app::buildOtaStat(f, s_otaState, s_otaErr, s_recv, s_total));
}
void notifyAck(uint8_t op, uint8_t res) {
  uint8_t f[4];
  notify(f, app::buildAck(f, op, res));
}

bool windowIsOpen(uint32_t now) { return s_windowUntil != 0 && (int32_t)(s_windowUntil - now) > 0; }

bool scooterMoving() {
  if (!s_ble) return false;
  Telemetry t;
  return s_ble->getTelemetry(t, millis()) && t.speedValid && t.speedKmh > 1.0f;
}

void notifyInfo() {
  uint8_t f[128];
  uint8_t flags = 0;
  if (windowIsOpen(millis())) flags |= 1;
  if (s_store && s_store->get().bootImgValid) flags |= 2;
  if (s_store && s_store->get().layoutOn) flags |= 16;
  if (s_store && s_store->get().layoutValid) flags |= 32;
  if (s_store && s_store->get().startCap) flags |= 64;
  if (s_phone && s_phone->available()) flags |= 4;
  if (s_ble && s_ble->state() == BleState::Connected) flags |= 8;
  const Settings& st = s_store->get();
  char extra[24];
  snprintf(extra, sizeof(extra), "%u,%u,%u,%u,%u", (unsigned)st.backlightPct, (unsigned)st.arcMax, (unsigned)st.accent[0], (unsigned)st.accent[1], (unsigned)st.accent[2]);
  notify(f, app::buildInfo(f, sizeof(f), st.theme, st.bootStyle, flags, NOVA_FW_VERSION, NOVA_PROFILE_NAME, st.bootText, extra));
}

void failOta(uint8_t err) {
  Update.abort();
  s_otaErr = err;
  s_otaState = app::OTA_ERROR;
  Diag::log("OTA error %u", (unsigned)err);
  notifyOta();
}

void otaBegin(const uint8_t* p, size_t n) {
  if (n < 9) { notifyAck(app::OP_OTA_BEGIN, 1); return; }
  const uint32_t size = app::le32(p + 1), crc = app::le32(p + 5);
  if (s_otaState == app::OTA_RECV || s_otaState == app::OTA_VERIFY) { s_otaErr = app::E_BUSY; notifyOta(); return; }
  s_otaErr = app::E_NONE;
  if (!windowIsOpen(millis())) { failOta(app::E_CLOSED); return; }
  if (scooterMoving()) { failOta(app::E_RIDING); return; }
  if (size < app::kMinAppBytes || size > app::kMaxAppBytes) { failOta(app::E_SIZE); return; }
  if (!Update.begin(size, U_FLASH)) { failOta(app::E_BEGIN); return; }
  xStreamBufferReset(s_sb);
  s_expectCrc = crc;
  s_crc = 0;
  s_total = size;
  s_recv = 0;
  s_lastNotified = 0;
  s_headLen = 0;
  s_gotHeader = false;
  s_overrun = false;
  s_lastData = millis();
  s_otaState = app::OTA_RECV;
  Diag::log("OTA begin %lu bytes", (unsigned long)size);
  notifyOta();
}

void handleCtrl(const uint8_t* p, size_t n) {
  if (n < 1) return;
  const uint8_t op = p[0];
  switch (op) {
    case app::OP_INFO: notifyInfo(); break;
    case app::OP_OTA_BEGIN: otaBegin(p, n); break;
    case app::OP_OTA_END: notifyAck(op, 0); break;  // size is known from BEGIN; the board finishes by itself
    case app::OP_OTA_ABORT:
      if (s_otaState == app::OTA_RECV) { Update.abort(); s_otaState = app::OTA_IDLE; s_otaErr = app::E_NONE; notifyOta(); }
      notifyAck(op, 0);
      break;
    case app::OP_THEME:
      if (n >= 2 && p[1] < kThemeCount) { portENTER_CRITICAL(&s_mux); s_pTheme = p[1]; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 1);
      break;
    case app::OP_BOOT_STYLE:
      if (n >= 2 && p[1] < app::kBootStyles) { portENTER_CRITICAL(&s_mux); s_pStyle = p[1]; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 1);
      break;
    case app::OP_BOOT_TEXT: {
      portENTER_CRITICAL(&s_mux);
      app::sanitizeBootText(s_pTextBuf, sizeof(s_pTextBuf), p + 1, n - 1);
      s_pText = true;
      portEXIT_CRITICAL(&s_mux);
      notifyAck(op, 0);
      break;
    }
    case app::OP_IMG_BEGIN: s_imgGot = 0; memset(s_img, 0, sizeof(s_img)); notifyAck(op, 0); break;
    case app::OP_IMG_CHUNK:
      if (n >= 4 && app::le16(p + 1) == s_imgGot && s_imgGot + (n - 3) <= app::kBootImgBytes) {
        memcpy(s_img + s_imgGot, p + 3, n - 3);
        s_imgGot += n - 3;
        notifyAck(op, 0);
      } else notifyAck(op, 1);
      break;
    case app::OP_IMG_END:
      if (s_imgGot == app::kBootImgBytes) { portENTER_CRITICAL(&s_mux); s_pImg = true; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 1);
      break;
    case app::OP_PREVIEW: portENTER_CRITICAL(&s_mux); s_pPreview = true; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); break;
    case app::OP_LAYOUT_BEGIN: s_layGot = 0; notifyAck(op, 0); break;
    case app::OP_LAYOUT_CHUNK:
      if (n >= 4 && app::le16(p + 1) == s_layGot && s_layGot + (n - 3) <= wl::kMaxBytes) {
        memcpy(s_lay + s_layGot, p + 3, n - 3);
        s_layGot += n - 3;
        notifyAck(op, 0);
      } else notifyAck(op, 1);
      break;
    case app::OP_LAYOUT_END: {
      wl::Layout probe;
      if (wl::parse(s_lay, s_layGot, probe)) { portENTER_CRITICAL(&s_mux); s_pLayout = true; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 3);  // not a valid layout: nothing is stored
      break;
    }
    case app::OP_LAYOUT_USE:
      if (n >= 2 && p[1] < 2 && (p[1] == 0 || (s_store && s_store->get().layoutValid))) {
        portENTER_CRITICAL(&s_mux); s_pLayUse = p[1]; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0);
      } else notifyAck(op, 1);
      break;
    case app::OP_BACKLIGHT:
      if (n >= 2 && p[1] >= 10 && p[1] <= 100) { portENTER_CRITICAL(&s_mux); s_pBright = p[1]; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 1);
      break;
    case app::OP_ARCMAX:
      if (n >= 2 && p[1] <= 24) { portENTER_CRITICAL(&s_mux); s_pArc = p[1]; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 1);
      break;
    case app::OP_ACCENT:
      if (n >= 4) { portENTER_CRITICAL(&s_mux); s_pAcc[0] = p[1]; s_pAcc[1] = p[2]; s_pAcc[2] = p[3]; s_pAccent = 1; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 1);
      break;
    case app::OP_STARTCAP:
      if (n >= 2 && p[1] < 2) { portENTER_CRITICAL(&s_mux); s_pStartCap = p[1]; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 1);
      break;
    case app::OP_MUSIC:
      if (n >= 2 && p[1] < 3) { portENTER_CRITICAL(&s_mux); s_pMusic = p[1]; portEXIT_CRITICAL(&s_mux); notifyAck(op, 0); }
      else notifyAck(op, 1);
      break;
    default: notifyAck(op, 2); break;
  }
}

class CtrlCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    const std::string v = c->getValue();
    handleCtrl((const uint8_t*)v.data(), v.size());
  }
};
class DataCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    if (s_otaState != app::OTA_RECV || !s_sb) return;
    const std::string v = c->getValue();
    if (xStreamBufferSend(s_sb, v.data(), v.size(), 0) < v.size()) s_overrun = true;
  }
};
CtrlCb s_ctrlCb;
DataCb s_dataCb;

void otaTask(void*) {
  static uint8_t buf[512];
  for (;;) {
    if (s_otaState != app::OTA_RECV) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    if (s_overrun) { failOta(app::E_OVERRUN); continue; }
    const size_t n = xStreamBufferReceive(s_sb, buf, sizeof(buf), pdMS_TO_TICKS(100));
    const uint32_t now = millis();
    if (n == 0) {
      if (now - s_lastData > 15000) failOta(app::E_TIMEOUT);
      continue;
    }
    s_lastData = now;
    if (s_headLen < sizeof(s_head)) {
      const size_t take = (n < sizeof(s_head) - s_headLen) ? n : sizeof(s_head) - s_headLen;
      memcpy(s_head + s_headLen, buf, take);
      s_headLen += take;
    }
    if (!s_gotHeader && s_headLen >= 14) {
      if (!app::isEspImageHeader(s_head, s_headLen)) { failOta(app::E_HEADER); continue; }
      s_gotHeader = true;
    }
    if (s_recv + n > s_total) { failOta(app::E_OVERRUN); continue; }
    if (Update.write(buf, n) != n) { failOta(app::E_WRITE); continue; }
    s_crc = app::crc32Update(s_crc, buf, n);
    s_recv += n;
    if (s_recv - s_lastNotified >= 4096 || s_recv == s_total) { s_lastNotified = s_recv; notifyOta(); }
    if (s_recv == s_total) {
      s_otaState = app::OTA_VERIFY;
      notifyOta();
      if (s_crc != s_expectCrc) { failOta(app::E_CRC); continue; }
      if (!Update.end(true)) { failOta(app::E_WRITE); continue; }
      s_otaState = app::OTA_DONE;
      notifyOta();
      Diag::log("OTA ok - restarting");
      vTaskDelay(pdMS_TO_TICKS(1500));
      ESP.restart();
    }
  }
}

}  // namespace

void configure(ScooterBLE* ble, PhoneMedia* phone, SettingsStorage* store) {
  s_ble = ble;
  s_phone = phone;
  s_store = store;
}

void attach(NimBLEServer* server) {
  s_sb = xStreamBufferCreate(12288, 1);
  NimBLEService* svc = server->createService(kSvc);
  NimBLECharacteristic* ctrl = svc->createCharacteristic(kCtrl, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);
  ctrl->setCallbacks(&s_ctrlCb);
  NimBLECharacteristic* data = svc->createCharacteristic(kData, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);
  data->setCallbacks(&s_dataCb);
  s_stat = svc->createCharacteristic(kStat, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  xTaskCreatePinnedToCore(otaTask, "nova_ota", 6144, nullptr, 1, nullptr, 1);
  Diag::log("app link ready");
}

Status status(uint32_t nowMs) {
  Status s;
  s.otaState = s_otaState;
  s.otaErr = s_otaErr;
  s.received = s_recv;
  s.total = s_total;
  s.windowOpen = windowIsOpen(nowMs);
  s.windowLeftMs = s.windowOpen ? (uint32_t)(s_windowUntil - nowMs) : 0;
  return s;
}

void openWindow(uint32_t ms) {
  uint32_t until = millis() + ms;
  if (until == 0) until = 1;
  s_windowUntil = until;
  if (s_otaState == app::OTA_ERROR || s_otaState == app::OTA_DONE) { s_otaState = app::OTA_IDLE; s_otaErr = app::E_NONE; }
  Diag::log("update window open");
}

void closeWindow() {
  if (s_otaState == app::OTA_RECV || s_otaState == app::OTA_VERIFY) return;
  s_windowUntil = 0;
  if (s_otaState == app::OTA_ERROR) { s_otaState = app::OTA_IDLE; s_otaErr = app::E_NONE; }
}

Applied applyPending() {
  Applied a = {false, false, false, false, false, false};
  int theme, style, music, layUse, startCap, bright, arc, accent;
  uint8_t acc[3];
  bool text, img, prev, lay;
  char tbuf[16];
  portENTER_CRITICAL(&s_mux);
  theme = s_pTheme; style = s_pStyle; music = s_pMusic; text = s_pText; img = s_pImg; prev = s_pPreview;
  lay = s_pLayout; layUse = s_pLayUse; startCap = s_pStartCap;
  bright = s_pBright; arc = s_pArc; accent = s_pAccent; acc[0] = s_pAcc[0]; acc[1] = s_pAcc[1]; acc[2] = s_pAcc[2];
  memcpy(tbuf, s_pTextBuf, sizeof(tbuf));
  s_pTheme = -1; s_pStyle = -1; s_pMusic = -1; s_pText = false; s_pImg = false; s_pPreview = false; s_pLayout = false; s_pLayUse = -1; s_pStartCap = -1; s_pBright = -1; s_pArc = -1; s_pAccent = 0;
  portEXIT_CRITICAL(&s_mux);
  if (!s_store) return a;
  Settings& st = s_store->get();
  if (theme >= 0) { st.theme = (uint8_t)theme; a.theme = true; }
  if (style >= 0) { st.bootStyle = (uint8_t)style; a.boot = true; }
  if (text) { strlcpy(st.bootText, tbuf, sizeof(st.bootText)); a.boot = true; }
  if (theme >= 0 || style >= 0 || text) s_store->save();
  if (img) { s_store->saveBootImg(s_img); a.boot = true; }
  if (prev) a.preview = true;
  if (lay) { s_store->saveLayout(s_lay, s_layGot); a.layout = true; }
  if (layUse >= 0) { st.layoutOn = (layUse == 1); s_store->save(); a.layout = true; }
  if (bright >= 0) { st.backlightPct = (uint8_t)bright; s_store->save(); a.display = true; }
  if (arc >= 0) { st.arcMax = (uint8_t)arc; s_store->save(); a.display = true; }
  if (accent) { st.accent[0] = acc[0]; st.accent[1] = acc[1]; st.accent[2] = acc[2]; s_store->save(); a.display = true; }
  if (startCap >= 0) { st.startCap = (startCap == 1); s_store->save(); a.startCap = true; }
  if (music >= 0 && s_phone) {
    if (music == 0) s_phone->prev();
    else if (music == 1) s_phone->playPause();
    else s_phone->next();
  }
  return a;
}

const char* errName(uint8_t err) {
  static const char* const names[11] = {"NONE", "WINDOW CLOSED", "SCOOTER MOVING", "BAD SIZE", "NOT A FIRMWARE", "CRC MISMATCH",
                                         "FLASH WRITE", "BEGIN FAILED", "DATA OVERRUN", "BUSY", "TIMEOUT"};
  return err < 11 ? names[err] : "?";
}

void pushTelemetry(const Telemetry& t, int capState, uint32_t nowMs) {
  if (!s_stat || nowMs - s_lastTelem < 500) return;
  s_lastTelem = nowMs;
  uint8_t valid = 0, mode = 255;
  if (t.speedValid) valid |= 1;
  if (t.battPctValid) valid |= 2;
  if (t.voltValid) valid |= 4;
  if (t.powerValid) valid |= 8;
  if (t.currentValid) valid |= 16;
  if (t.tempValid) valid |= 32;
  if (t.motorTempValid) valid |= 64;
  if (t.totalValid) valid |= 128;
  if (t.modeValid) {
    if (!strncmp(t.mode, "WALK", 4)) mode = 0; else if (!strncmp(t.mode, "ECO", 3)) mode = 1;
    else if (!strncmp(t.mode, "DRIVE", 5)) mode = 2; else if (!strncmp(t.mode, "SPORT", 5)) mode = 3;
  }
  uint8_t f[app::kTelemLen];
  notify(f, app::buildTelem(f, valid, (uint16_t)(t.speedKmh * 10.0f + 0.5f), t.battPct, (uint16_t)(t.volts * 10.0f + 0.5f),
                            (int16_t)t.powerW, (int16_t)lroundf(t.currentA * 10.0f), (int8_t)lroundf(t.tempC), (int8_t)lroundf(t.motorTempC),
                            mode, capState == 1 ? 1 : (capState == 0 ? 0 : 2), (uint32_t)(t.totalKm * 10.0f + 0.5f)));
}

}  // namespace applink
