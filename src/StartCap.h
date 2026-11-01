#pragma once
// Pure decision logic for the "start cap": every time the scooter is switched on (= the board
// gets a link to it), the three mode limits are lowered to 20 km/h, if the phone app has the
// start cap turned on. The cap can be released for ONE ride from the service menu; it is armed
// again as soon as the link is lost (scooter switched off).
#include <stdint.h>

namespace startcap {

static const int kCapKmh = 20;
static const uint8_t kMaxTries = 5;
static const uint32_t kRetryMs = 6000;

// capState: 1 = every limit read back at or below 20, 0 = at least one above 20, -1 = unknown.
struct Inputs {
  bool startCapOn;     // setting made in the phone app
  bool released;       // service menu released the cap for this ride
  bool linked;         // link to the scooter is up
  int capState;
  bool writePending;
  uint8_t tries;
  uint32_t msSinceLastTry;
};

// True when the firmware should now ask the scooter to cap the limits at 20 km/h.
inline bool shouldCap(const Inputs& in) {
  if (!in.startCapOn || in.released || !in.linked) return false;
  if (in.capState != 0) return false;            // already capped, or limits not read yet: never write blind
  if (in.writePending) return false;
  if (in.tries >= kMaxTries) return false;
  if (in.tries > 0 && in.msSinceLastTry < kRetryMs) return false;
  return true;
}

// Entering the service menu: hold SPORT for 2 s while the throttle is pressed and the scooter stands still.
static const uint32_t kSecretHoldMs = 2000;
static const float kStandstillKmh = 3.0f;
static const int kGasPowerW = 120;             // the protocol has no throttle value: power draw is the stand-in

inline bool secretArmed(bool onSportButton, bool throttle, bool speedKnown, float speedKmh) {
  return onSportButton && throttle && speedKnown && speedKmh < kStandstillKmh;
}

}  // namespace startcap
