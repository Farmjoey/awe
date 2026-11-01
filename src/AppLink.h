#pragma once
// GATT service for the NOVA RIDE phone app (Web Bluetooth): firmware update, theme,
// boot screen and music remote. Updates need (1) an encrypted, bonded link, (2) the
// update window opened on the board by a deliberate hold, and (3) a scooter that is not moving.
#include <stdint.h>

class NimBLEServer;
class ScooterBLE;
class PhoneMedia;
class SettingsStorage;

namespace applink {

struct Status {
  uint8_t otaState;   // app::OtaState
  uint8_t otaErr;     // app::OtaErr
  uint32_t received, total;
  bool windowOpen;
  uint32_t windowLeftMs;
};

struct Applied { bool theme, boot, preview, layout, startCap, display; };

void configure(ScooterBLE* ble, PhoneMedia* phone, SettingsStorage* store);
void attach(NimBLEServer* server);              // creates the service (before advertising starts)
Status status(uint32_t nowMs);
void openWindow(uint32_t ms);
void closeWindow();                              // ignored while an update is being received
Applied applyPending();                          // call from the main loop: stores changes, runs music keys
const char* errName(uint8_t err);
void pushTelemetry(const struct Telemetry& t, int capState, uint32_t nowMs);   // about 2 Hz while a phone is connected

}  // namespace applink
