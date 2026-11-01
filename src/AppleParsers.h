#pragma once
// Parsers for Apple's two BLE services, kept free of radio code so they can be unit-tested:
//   ANCS = Apple Notification Center Service (messages)
//   AMS  = Apple Media Service (song title / artist / playing state)
// Formats follow Apple's published specifications. Not yet verified against a real iPhone.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ancs {

struct Event { uint8_t id, flags, category, count; uint32_t uid; };
static const uint8_t kEvAdded = 0, kEvModified = 1, kEvRemoved = 2;
static const uint8_t kFlagSilent = 1, kFlagPreExisting = 4;

// Notification Source notification: EventID, EventFlags, CategoryID, CategoryCount, UID (u32 LE).
inline bool parseNs(const uint8_t* d, size_t n, Event& e) {
  if (!d || n != 8) return false;
  e.id = d[0]; e.flags = d[1]; e.category = d[2]; e.count = d[3];
  e.uid = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
  return true;
}

// Only fresh notifications are shown (not the old ones iOS replays when we connect).
inline bool shouldShow(const Event& e) { return e.id == kEvAdded && !(e.flags & kFlagPreExisting); }

// Control Point: GetNotificationAttributes for app id (0), title (1, max len), message (3, max len).
inline size_t buildGetAttrs(uint8_t* out, size_t cap, uint32_t uid, uint16_t titleMax, uint16_t msgMax) {
  if (cap < 12) return 0;
  size_t k = 0;
  out[k++] = 0x00;
  for (int i = 0; i < 4; ++i) out[k++] = (uint8_t)(uid >> (8 * i));
  out[k++] = 0x00;
  out[k++] = 0x01; out[k++] = (uint8_t)(titleMax & 255); out[k++] = (uint8_t)(titleMax >> 8);
  out[k++] = 0x03; out[k++] = (uint8_t)(msgMax & 255); out[k++] = (uint8_t)(msgMax >> 8);
  return k;
}

// Data Source answers can arrive in several notifications; this puts them together.
class DataAssembler {
 public:
  DataAssembler() { reset(); }
  void reset() { len_ = 0; done_ = false; uid_ = 0; app_[0] = title_[0] = text_[0] = 0; }
  bool complete() const { return done_; }
  uint32_t uid() const { return uid_; }
  const char* app() const { return app_; }
  const char* title() const { return title_; }
  const char* text() const { return text_; }

  // Returns true when the answer is complete. Anything malformed is dropped (reset).
  bool feed(const uint8_t* d, size_t n) {
    if (done_) reset();
    if (!d || n == 0 || len_ + n > sizeof(buf_)) { reset(); return false; }
    memcpy(buf_ + len_, d, n);
    len_ += n;
    return parse();
  }

 private:
  static void put(char* dst, size_t cap, const uint8_t* s, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; ++i) {
      const uint8_t c = s[i];
      if (c >= 0x20 && c < 0x7F) dst[k++] = (char)c;
      else if (c >= 0x80) { if (k == 0 || dst[k - 1] != '?') dst[k++] = '?'; }   // UTF-8 (e.g. æøå) shown as ?
    }
    dst[k] = 0;
  }
  bool parse() {
    if (len_ < 5 || buf_[0] != 0x00) { if (len_ >= 1 && buf_[0] != 0x00) reset(); return false; }
    size_t pos = 5;
    bool gotApp = false, gotTitle = false, gotText = false;
    char app[sizeof(app_)] = "", title[sizeof(title_)] = "", text[sizeof(text_)] = "";
    while (pos + 3 <= len_) {
      const uint8_t id = buf_[pos];
      const size_t l = (size_t)buf_[pos + 1] | ((size_t)buf_[pos + 2] << 8);
      if (pos + 3 + l > len_) return false;                       // wait for more bytes
      const uint8_t* v = buf_ + pos + 3;
      if (id == 0) { put(app, sizeof(app), v, l); gotApp = true; }
      else if (id == 1) { put(title, sizeof(title), v, l); gotTitle = true; }
      else if (id == 3) { put(text, sizeof(text), v, l); gotText = true; }
      pos += 3 + l;
    }
    if (!(gotApp && gotTitle && gotText)) return false;
    uid_ = (uint32_t)buf_[1] | ((uint32_t)buf_[2] << 8) | ((uint32_t)buf_[3] << 16) | ((uint32_t)buf_[4] << 24);
    memcpy(app_, app, sizeof(app_)); memcpy(title_, title, sizeof(title_)); memcpy(text_, text, sizeof(text_));
    done_ = true;
    return true;
  }
  uint8_t buf_[220];
  size_t len_;
  bool done_;
  uint32_t uid_;
  char app_[20], title_[26], text_[72];
};

// Short, readable name for common bundle identifiers; otherwise the last part of the id.
inline void friendlyApp(const char* bundle, char* out, size_t cap) {
  struct M { const char* id; const char* name; };
  static const M map[] = {
      {"com.apple.MobileSMS", "Messages"}, {"net.whatsapp.WhatsApp", "WhatsApp"}, {"com.facebook.Messenger", "Messenger"},
      {"com.burbn.instagram", "Instagram"}, {"com.toyopagroup.picaboo", "Snapchat"}, {"ph.telegra.Telegraph", "Telegram"},
      {"com.apple.mobilephone", "Phone"}, {"com.apple.mobilemail", "Mail"}, {"com.apple.facetime", "FaceTime"},
      {"com.google.Gmail", "Gmail"}, {"com.tinyspeck.chatlyio", "Slack"}, {"com.hammerandchisel.discord", "Discord"},
      {"org.whispersystems.signal", "Signal"}, {"com.apple.reminders", "Reminders"}, {"com.apple.mobilecal", "Calendar"}};
  for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
    if (strcmp(bundle, map[i].id) == 0) { snprintf(out, cap, "%s", map[i].name); return; }
  const char* dot = strrchr(bundle, '.');
  snprintf(out, cap, "%s", (dot && dot[1]) ? dot + 1 : (bundle[0] ? bundle : "App"));
}

}  // namespace ancs

namespace ams {

static const uint8_t kEntityPlayer = 0, kEntityTrack = 2;
static const uint8_t kRemotePlay = 0, kRemotePause = 1, kRemoteToggle = 2, kRemoteNext = 3, kRemotePrev = 4;

struct State {
  char title[32], artist[32];
  bool haveTrack, playing;
  State() { reset(); }
  void reset() { title[0] = artist[0] = 0; haveTrack = false; playing = false; }
};

// Entity Update write: which attributes we want notifications for.
inline size_t buildTrackSubscription(uint8_t* out, size_t cap) {
  if (cap < 3) return 0;
  out[0] = kEntityTrack; out[1] = 0x00; out[2] = 0x02;   // artist, title
  return 3;
}
inline size_t buildPlayerSubscription(uint8_t* out, size_t cap) {
  if (cap < 2) return 0;
  out[0] = kEntityPlayer; out[1] = 0x01;                 // playback info
  return 2;
}

inline void setText(char* dst, size_t cap, const uint8_t* s, size_t n) {
  size_t k = 0;
  for (size_t i = 0; i < n && k + 1 < cap; ++i) {
    const uint8_t c = s[i];
    if (c >= 0x20 && c < 0x7F) dst[k++] = (char)c;
    else if (c >= 0x80) { if (k == 0 || dst[k - 1] != '?') dst[k++] = '?'; }
  }
  dst[k] = 0;
}

// Entity Update notification: EntityID, AttributeID, Flags, value...
inline bool onEntityUpdate(State& st, const uint8_t* d, size_t n) {
  if (!d || n < 3) return false;
  const uint8_t entity = d[0], attr = d[1];
  const uint8_t* v = d + 3;
  const size_t vl = n - 3;
  if (entity == kEntityTrack) {
    if (attr == 0) { setText(st.artist, sizeof(st.artist), v, vl); st.haveTrack = true; return true; }
    if (attr == 2) { setText(st.title, sizeof(st.title), v, vl); st.haveTrack = st.title[0] != 0 || st.artist[0] != 0; return true; }
  } else if (entity == kEntityPlayer && attr == 1) {
    // "<state>,<rate>,<elapsed>" with state 0 paused, 1 playing, 2 rewinding, 3 fast-forwarding
    char tmp[24];
    setText(tmp, sizeof(tmp), v, vl);
    st.playing = (tmp[0] == '1' || tmp[0] == '2' || tmp[0] == '3');
    return true;
  }
  return false;
}

}  // namespace ams
