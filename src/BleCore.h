#pragma once
// One shared NimBLE initialisation, used by the scooter link (central role) and the phone
// link (peripheral role). Both roles run side by side on the same radio.
namespace blecore {
void init();  // idempotent; safe to call from either module
}
