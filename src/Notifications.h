#pragma once
// Message notifications from the iPhone (Apple ANCS), kept in RAM only.
// Needs the "apple" firmware build (NOVA_APPLE_SERVICES=1) and "Show Notifications" for the
// board in iPhone Settings > Bluetooth. Without that, the list stays empty and says so.
#include <stdint.h>
#include "NotifStore.h"

class Notifications {
 public:
  void begin() {}
  void loop(uint32_t) {}
  bool available() const;        // true once ANCS is set up on the phone link
  int count() const;
  int unread() const;
  bool get(int i, NotifMsg& out) const;   // newest first
  void markAllRead();
  void clear();
  static void push(const char* app, const char* title, const char* text, uint8_t category);  // used by the ANCS client
};
