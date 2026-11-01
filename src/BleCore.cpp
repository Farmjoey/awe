#include "BleCore.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

namespace blecore {
void init() {
  static volatile bool inited = false;
  static volatile bool busy = false;
  if (inited) return;
  if (busy) {
    while (!inited) vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }
  busy = true;
  NimBLEDevice::init("NOVA RIDE");
  // Bonding with "Just Works" pairing: iOS needs an encrypted, bonded link for HID.
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  inited = true;
}
}  // namespace blecore
