#pragma once
// Client for Apple's ANCS (messages) and AMS (music) on the iPhone link.
// Compiled for real only with -DNOVA_APPLE_SERVICES=1 (env cyd_st7789_apple); otherwise inert stubs.
#include <stdint.h>

class PhoneMedia;

namespace applesvc {
bool compiled();
bool ancsReady();
bool amsReady();
const char* statusText();            // short text for the screens
bool remote(uint8_t amsCommand);     // queue an AMS remote command (false if AMS is not ready)
void attach(PhoneMedia* media);      // where song info goes
void onConnect(uint16_t connHandle); // called when the iPhone connects
void onDisconnect();
void loop(uint32_t nowMs);
}  // namespace applesvc
