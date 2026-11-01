#include "AppleServices.h"
#include <Arduino.h>
#include "BoardConfig.h"
#include "Diagnostics.h"
#include "PhoneMedia.h"

#if NOVA_APPLE_SERVICES

#include <NimBLEDevice.h>
#if __has_include("host/ble_gatt.h")
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#else
#include "nimble/nimble/host/include/host/ble_gap.h"
#include "nimble/nimble/host/include/host/ble_gatt.h"
#include "nimble/nimble/host/include/host/ble_uuid.h"
#include "nimble/porting/nimble/include/os/os_mbuf.h"
#endif
#include "AppleParsers.h"
#include "Notifications.h"

namespace applesvc {
namespace {

enum Step : uint8_t {
  ST_IDLE, ST_ANCS_SVC, ST_ANCS_CHR, ST_ANCS_NS, ST_ANCS_DS, ST_AMS_SVC, ST_AMS_CHR, ST_AMS_RC, ST_AMS_EU,
  ST_AMS_TRACK, ST_AMS_PLAYER, ST_READY
};

struct Handles { uint16_t start, end, ns, cp, ds, rc, eu; };

PhoneMedia* s_media = nullptr;
uint16_t s_conn = 0xFFFF;
volatile Step s_step = ST_IDLE;
Handles s_h = {0, 0, 0, 0, 0, 0, 0};
bool s_found = false, s_ancs = false, s_ams = false, s_listening = false;
ble_uuid128_t kAncs, kNs, kCp, kDs, kAms, kRc, kEu;
struct ble_gap_event_listener s_listener;

ancs::DataAssembler s_asm;
ams::State s_state;
uint32_t s_q[4];
int s_qn = 0;
bool s_inflight = false;
uint32_t s_inflightMs = 0;
int s_remote = -1;
char s_status[24] = "no phone";

void makeUuid(ble_uuid128_t& u, const uint8_t be[16]) {
  u.u.type = BLE_UUID_TYPE_128;
  for (int i = 0; i < 16; ++i) u.value[i] = be[15 - i];  // NimBLE stores 128-bit UUIDs little endian
}

void initUuids() {
  static const uint8_t ancs[16] = {0x79, 0x05, 0xF4, 0x31, 0xB5, 0xCE, 0x4E, 0x99, 0xA4, 0x0F, 0x4B, 0x1E, 0x12, 0x2D, 0x00, 0xD0};
  static const uint8_t ns[16] = {0x9F, 0xBF, 0x12, 0x0D, 0x63, 0x01, 0x42, 0xD9, 0x8C, 0x58, 0x25, 0xE6, 0x99, 0xA2, 0x1D, 0xBD};
  static const uint8_t cp[16] = {0x69, 0xD1, 0xD8, 0xF3, 0x45, 0xE1, 0x49, 0xA8, 0x98, 0x21, 0x9B, 0xBD, 0xFD, 0xAA, 0xD9, 0xD9};
  static const uint8_t ds[16] = {0x22, 0xEA, 0xC6, 0xE9, 0x24, 0xD6, 0x4B, 0xB5, 0xBE, 0x44, 0xB3, 0x6A, 0xCE, 0x7C, 0x7B, 0xFB};
  static const uint8_t ams[16] = {0x89, 0xD3, 0x50, 0x2B, 0x0F, 0x36, 0x43, 0x3A, 0x8E, 0xF4, 0xC5, 0x02, 0xAD, 0x55, 0xF8, 0xDC};
  static const uint8_t rc[16] = {0x9B, 0x3C, 0x81, 0xD8, 0x57, 0xB1, 0x4A, 0x8A, 0xB8, 0xDF, 0x0E, 0x56, 0xF7, 0xCA, 0x51, 0xC2};
  static const uint8_t eu[16] = {0x2F, 0x7C, 0xAB, 0xCE, 0x80, 0x8D, 0x41, 0x1F, 0x9A, 0x0C, 0xBB, 0x92, 0xBA, 0x96, 0xC1, 0x02};
  makeUuid(kAncs, ancs); makeUuid(kNs, ns); makeUuid(kCp, cp); makeUuid(kDs, ds);
  makeUuid(kAms, ams); makeUuid(kRc, rc); makeUuid(kEu, eu);
}

void setStatus(const char* s) { strlcpy(s_status, s, sizeof(s_status)); }

void advance(uint8_t from);
void startAms();

void pump(uint32_t now) {
  if (s_inflight || s_qn == 0 || !s_ancs || s_h.cp == 0) return;
  uint8_t req[16];
  const size_t n = ancs::buildGetAttrs(req, sizeof(req), s_q[0], 24, 70);
  if (ble_gattc_write_flat(s_conn, s_h.cp, req, (uint16_t)n, nullptr, nullptr) == 0) {
    s_inflight = true;
    s_inflightMs = now;
    s_asm.reset();
    for (int i = 1; i < s_qn; ++i) s_q[i - 1] = s_q[i];
    --s_qn;
  }
}

// ---- GATT procedures (all callbacks run in the NimBLE host task) ----
int onWrite(uint16_t, const struct ble_gatt_error* err, struct ble_gatt_attr*, void* arg) {
  const uint8_t step = (uint8_t)(uintptr_t)arg;
  if (err->status != 0) Diag::log("apple: write step %u err %d", (unsigned)step, (int)err->status);
  advance(step);
  return 0;
}

bool writeFlat(uint16_t handle, const uint8_t* d, uint16_t n, uint8_t step) {
  return ble_gattc_write_flat(s_conn, handle, d, n, onWrite, (void*)(uintptr_t)step) == 0;
}

int onChr(uint16_t, const struct ble_gatt_error* err, const struct ble_gatt_chr* chr, void* arg) {
  const bool isAncs = (arg == (void*)1);
  if (err->status == 0 && chr) {
    if (isAncs) {
      if (ble_uuid_cmp(&chr->uuid.u, &kNs.u) == 0) s_h.ns = chr->val_handle;
      else if (ble_uuid_cmp(&chr->uuid.u, &kCp.u) == 0) s_h.cp = chr->val_handle;
      else if (ble_uuid_cmp(&chr->uuid.u, &kDs.u) == 0) s_h.ds = chr->val_handle;
    } else {
      if (ble_uuid_cmp(&chr->uuid.u, &kRc.u) == 0) s_h.rc = chr->val_handle;
      else if (ble_uuid_cmp(&chr->uuid.u, &kEu.u) == 0) s_h.eu = chr->val_handle;
    }
    return 0;
  }
  if (err->status == BLE_HS_EDONE) advance(isAncs ? ST_ANCS_CHR : ST_AMS_CHR);
  return 0;
}

int onSvc(uint16_t, const struct ble_gatt_error* err, const struct ble_gatt_svc* svc, void* arg) {
  const bool isAncs = (arg == (void*)1);
  if (err->status == 0 && svc) { s_h.start = svc->start_handle; s_h.end = svc->end_handle; s_found = true; return 0; }
  if (err->status == BLE_HS_EDONE) advance(isAncs ? ST_ANCS_SVC : ST_AMS_SVC);
  return 0;
}

void startAms() {
  s_step = ST_AMS_SVC;
  s_found = false;
  if (ble_gattc_disc_svc_by_uuid(s_conn, &kAms.u, onSvc, (void*)2) != 0) { setStatus("AMS busy"); s_step = ST_READY; }
}

// One step done -> start the next one.
void advance(uint8_t from) {
  static const uint8_t on[2] = {0x01, 0x00};
  switch (from) {
    case ST_ANCS_SVC:                                   // service search finished
      if (!s_found) { Diag::log("apple: no ANCS"); startAms(); return; }
      s_step = ST_ANCS_CHR;
      if (ble_gattc_disc_all_chrs(s_conn, s_h.start, s_h.end, onChr, (void*)1) != 0) startAms();
      return;
    case ST_ANCS_CHR:
      if (!s_h.ns || !s_h.cp || !s_h.ds) { Diag::log("apple: ANCS chars missing"); startAms(); return; }
      s_step = ST_ANCS_NS;                               // subscribe: Notification Source (CCCD = value handle + 1)
      if (!writeFlat((uint16_t)(s_h.ns + 1), on, 2, ST_ANCS_NS)) startAms();
      return;
    case ST_ANCS_NS:
      s_step = ST_ANCS_DS;
      if (!writeFlat((uint16_t)(s_h.ds + 1), on, 2, ST_ANCS_DS)) startAms();
      return;
    case ST_ANCS_DS:
      s_ancs = true;
      Diag::log("apple: ANCS ready");
      startAms();
      return;
    case ST_AMS_SVC:
      if (!s_found) { Diag::log("apple: no AMS"); s_step = ST_READY; setStatus(s_ancs ? "ANCS ok" : "none found"); return; }
      s_step = ST_AMS_CHR;
      s_h.start = s_h.start;  // handles of the AMS service were just stored by onSvc
      if (ble_gattc_disc_all_chrs(s_conn, s_h.start, s_h.end, onChr, (void*)2) != 0) s_step = ST_READY;
      return;
    case ST_AMS_CHR:
      if (!s_h.rc || !s_h.eu) { Diag::log("apple: AMS chars missing"); s_step = ST_READY; return; }
      s_step = ST_AMS_RC;
      if (!writeFlat((uint16_t)(s_h.rc + 1), on, 2, ST_AMS_RC)) s_step = ST_READY;
      return;
    case ST_AMS_RC:
      s_step = ST_AMS_EU;
      if (!writeFlat((uint16_t)(s_h.eu + 1), on, 2, ST_AMS_EU)) s_step = ST_READY;
      return;
    case ST_AMS_EU: {
      uint8_t sub[4];
      const size_t n = ams::buildTrackSubscription(sub, sizeof(sub));
      s_step = ST_AMS_TRACK;
      if (!writeFlat(s_h.eu, sub, (uint16_t)n, ST_AMS_TRACK)) s_step = ST_READY;
      return;
    }
    case ST_AMS_TRACK: {
      uint8_t sub[4];
      const size_t n = ams::buildPlayerSubscription(sub, sizeof(sub));
      s_step = ST_AMS_PLAYER;
      if (!writeFlat(s_h.eu, sub, (uint16_t)n, ST_AMS_PLAYER)) s_step = ST_READY;
      return;
    }
    case ST_AMS_PLAYER:
      s_ams = true;
      s_step = ST_READY;
      setStatus(s_ancs ? "ANCS + AMS ok" : "AMS ok");
      Diag::log("apple: AMS ready");
      return;
    default:
      return;
  }
}

void startDiscovery() {
  if (s_step != ST_IDLE) return;
  memset(&s_h, 0, sizeof(s_h));
  s_ancs = s_ams = false;
  s_qn = 0;
  s_inflight = false;
  s_asm.reset();
  s_state.reset();
  setStatus("setting up");
  s_step = ST_ANCS_SVC;
  s_found = false;
  if (ble_gattc_disc_svc_by_uuid(s_conn, &kAncs.u, onSvc, (void*)1) != 0) { Diag::log("apple: discovery busy"); s_step = ST_IDLE; }
}

void onNotifyData(uint16_t attr, const uint8_t* d, size_t n) {
  if (attr == s_h.ns && s_ancs) {
    ancs::Event e;
    if (ancs::parseNs(d, n, e) && ancs::shouldShow(e) && s_qn < 4) s_q[s_qn++] = e.uid;
  } else if (attr == s_h.ds && s_ancs) {
    if (s_asm.feed(d, n)) {
      char app[20];
      ancs::friendlyApp(s_asm.app(), app, sizeof(app));
      Notifications::push(app, s_asm.title(), s_asm.text(), 0);
      s_inflight = false;
    }
  } else if (attr == s_h.eu && s_ams) {
    if (ams::onEntityUpdate(s_state, d, n) && s_media) {
      s_media->setTrack(s_state.title, s_state.artist, s_state.haveTrack);
      s_media->setPlaying(s_state.playing);
    }
  }
}

int gapEvent(struct ble_gap_event* ev, void*) {
  switch (ev->type) {
    case BLE_GAP_EVENT_ENC_CHANGE:
      if (ev->enc_change.conn_handle == s_conn && ev->enc_change.status == 0) startDiscovery();
      break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
      if (ev->notify_rx.conn_handle != s_conn || !ev->notify_rx.om) break;
      uint8_t buf[200];
      uint16_t len = OS_MBUF_PKTLEN(ev->notify_rx.om);
      if (len > sizeof(buf)) len = sizeof(buf);
      if (os_mbuf_copydata(ev->notify_rx.om, 0, len, buf) == 0) onNotifyData(ev->notify_rx.attr_handle, buf, len);
      break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
      if (ev->disconnect.conn.conn_handle == s_conn) onDisconnect();
      break;
    default:
      break;
  }
  return 0;
}

}  // namespace

bool compiled() { return true; }
bool ancsReady() { return s_ancs && s_step == ST_READY; }
bool amsReady() { return s_ams && s_step == ST_READY; }
const char* statusText() { return s_status; }
void attach(PhoneMedia* media) { s_media = media; }

bool remote(uint8_t cmd) {
  if (!amsReady()) return false;
  s_remote = cmd;
  return true;
}

void onConnect(uint16_t conn) {
  if (!s_listening) {
    initUuids();
    ble_gap_event_listener_register(&s_listener, gapEvent, nullptr);
    s_listening = true;
  }
  s_conn = conn;
  s_step = ST_IDLE;
  s_ancs = s_ams = false;
  setStatus("pairing");
  NimBLEDevice::startSecurity(conn);  // iOS then encrypts the link; discovery starts when that is done
}

void onDisconnect() {
  s_conn = 0xFFFF;
  s_step = ST_IDLE;
  s_ancs = s_ams = false;
  s_inflight = false;
  s_qn = 0;
  s_remote = -1;
  setStatus("no phone");
  if (s_media) s_media->setTrack("", "", false);
}

void loop(uint32_t now) {
  if (s_step != ST_READY) return;
  if (s_inflight && now - s_inflightMs > 3000) { s_inflight = false; s_asm.reset(); }
  pump(now);
  if (s_remote >= 0 && s_ams) {
    const uint8_t cmd = (uint8_t)s_remote;
    if (ble_gattc_write_flat(s_conn, s_h.rc, &cmd, 1, nullptr, nullptr) == 0) s_remote = -1;
  }
}

}  // namespace applesvc

#else  // ---------------------------------------------------------------- stubs (default builds)

namespace applesvc {
bool compiled() { return false; }
bool ancsReady() { return false; }
bool amsReady() { return false; }
const char* statusText() { return "needs apple build"; }
bool remote(uint8_t) { return false; }
void attach(PhoneMedia*) {}
void onConnect(uint16_t) {}
void onDisconnect() {}
void loop(uint32_t) {}
}  // namespace applesvc

#endif
