#pragma once
// iPhone link over Bluetooth LE (no app, no wifi).
//  * The board advertises as "NOVA RIDE" and is paired once in iPhone Settings > Bluetooth.
//  * Music: play/pause, next, previous are sent as standard Bluetooth media keys
//    (HID consumer control). They act on whatever app is playing.
//  * NOT implemented yet: song title/artist (Apple Media Service) and message
//    notifications (Apple Notification Center Service).
#include <stdint.h>

enum class PhoneLink : uint8_t { Off, Advertising, Connected };

struct MediaInfo {
  PhoneLink link;
  bool trackValid;  // always false until AMS is implemented
  char title[32];
  char artist[32];
  bool playing;
  MediaInfo() : link(PhoneLink::Off), trackValid(false), playing(false) { title[0] = 0; artist[0] = 0; }
};

class PhoneMedia {
 public:
  void begin();
  void setEnabled(bool on);
  bool enabled() const { return enabled_; }
  void loop(uint32_t now);                 // refreshes the link state
  MediaInfo info() const;                  // copy, safe to call from the UI thread
  bool available() const { return info().link == PhoneLink::Connected; }
  void setTrack(const char* title, const char* artist, bool have);   // from AMS
  void setPlaying(bool playing);
  bool playPause();
  bool next();
  bool prev();
 private:
  bool sendKey(uint16_t usage);
  bool enabled_ = false;
  MediaInfo info_;
};
