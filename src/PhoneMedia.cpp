#include "PhoneMedia.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <vector>
#include "BleCore.h"
#include "Diagnostics.h"
#include "AppLink.h"
#include "AppleServices.h"
#include "AppleParsers.h"
#include "BoardConfig.h"

namespace {
// HID report map: consumer control, report id 2, one 16-bit usage code.
const uint8_t kReportMap[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID 2
    0x19, 0x00,        //   Usage Minimum 0
    0x2A, 0x9C, 0x02,  //   Usage Maximum 0x029C
    0x15, 0x00,        //   Logical Minimum 0
    0x26, 0x9C, 0x02,  //   Logical Maximum 0x029C
    0x75, 0x10,        //   Report Size 16
    0x95, 0x01,        //   Report Count 1
    0x81, 0x00,        //   Input (Data, Array)
    0xC0               // End Collection
};
const uint16_t kUsagePlayPause = 0x00CD;
const uint16_t kUsageNext = 0x00B5;
const uint16_t kUsagePrev = 0x00B6;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
NimBLEServer* s_server = nullptr;
NimBLEHIDDevice* s_hid = nullptr;
NimBLECharacteristic* s_input = nullptr;
bool s_created = false;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override {
    Diag::log("phone connected");
    NimBLEDevice::getAdvertising()->start();  // keep advertising so the app can connect as well
  }
#if NOVA_APPLE_SERVICES
  void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override { applesvc::onConnect(desc->conn_handle); }
#endif
  void onDisconnect(NimBLEServer*) override {
    Diag::log("phone disconnected");
    applesvc::onDisconnect();
    NimBLEDevice::getAdvertising()->start();  // be pairable/reconnectable again
  }
};
ServerCb s_cb;

void createOnce() {
  if (s_created) return;
  blecore::init();
  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(&s_cb);
  s_hid = new NimBLEHIDDevice(s_server);
  s_input = s_hid->inputReport(2);
  s_hid->manufacturer()->setValue("NOVA");
  s_hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  s_hid->hidInfo(0x00, 0x01);
  s_hid->reportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));
  s_hid->startServices();
  applink::attach(s_server);
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C0);  // generic HID
  adv->addServiceUUID(s_hid->hidService()->getUUID());
  adv->setScanResponse(true);
  s_created = true;
  Diag::log("phone HID ready");
}
}  // namespace

void PhoneMedia::setEnabled(bool on) {
  if (on == enabled_) return;
  enabled_ = on;
  if (on) {
    createOnce();
    NimBLEDevice::getAdvertising()->start();
    Diag::log("phone: advertising");
  } else if (s_created) {
    NimBLEDevice::getAdvertising()->stop();
    std::vector<uint16_t> peers = s_server->getPeerDevices();
    for (size_t i = 0; i < peers.size(); ++i) s_server->disconnect(peers[i]);
  }
}

void PhoneMedia::begin() { applesvc::attach(this); }

void PhoneMedia::loop(uint32_t now) {
  PhoneLink l = PhoneLink::Off;
  if (enabled_ && s_created) l = s_server->getConnectedCount() > 0 ? PhoneLink::Connected : PhoneLink::Advertising;
  portENTER_CRITICAL(&s_mux);
  info_.link = l;
  portEXIT_CRITICAL(&s_mux);
  applesvc::loop(now);
}

MediaInfo PhoneMedia::info() const {
  MediaInfo m;
  portENTER_CRITICAL(&s_mux);
  m = info_;
  portEXIT_CRITICAL(&s_mux);
  return m;
}

void PhoneMedia::setTrack(const char* title, const char* artist, bool have) {
  portENTER_CRITICAL(&s_mux);
  strlcpy(info_.title, title, sizeof(info_.title));
  strlcpy(info_.artist, artist, sizeof(info_.artist));
  info_.trackValid = have;
  portEXIT_CRITICAL(&s_mux);
}

void PhoneMedia::setPlaying(bool playing) {
  portENTER_CRITICAL(&s_mux);
  info_.playing = playing;
  portEXIT_CRITICAL(&s_mux);
}

bool PhoneMedia::sendKey(uint16_t usage) {
  if (!available() || !s_input) return false;
  uint8_t down[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  uint8_t up[2] = {0, 0};
  s_input->setValue(down, sizeof(down));
  s_input->notify();
  delay(20);
  s_input->setValue(up, sizeof(up));
  s_input->notify();
  return true;
}

// Apple Media Service first (acts on the app that is playing), Bluetooth media keys otherwise.
bool PhoneMedia::playPause() { return applesvc::remote(ams::kRemoteToggle) || sendKey(kUsagePlayPause); }
bool PhoneMedia::next() { return applesvc::remote(ams::kRemoteNext) || sendKey(kUsageNext); }
bool PhoneMedia::prev() { return applesvc::remote(ams::kRemotePrev) || sendKey(kUsagePrev); }
