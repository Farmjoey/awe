#pragma once
// Message list shown on the board. RAM only: nothing is ever written to flash.
// Pure C++11 (host-tested); thread safety is added by the Notifications wrapper.
#include <stdint.h>
#include <string.h>

struct NotifMsg {
  char app[20];
  char title[26];
  char text[72];
  uint8_t category;
  bool unread;
};

class NotifStore {
 public:
  static const int kMax = 6;
  NotifStore() : n_(0), head_(0), unread_(0) {}
  void clear() { n_ = 0; head_ = 0; unread_ = 0; }
  int count() const { return n_; }
  int unread() const { return unread_; }
  // Newest message first.
  bool get(int i, NotifMsg& out) const {
    if (i < 0 || i >= n_) return false;
    out = buf_[(head_ + kMax - 1 - i) % kMax];
    return true;
  }
  void push(const char* app, const char* title, const char* text, uint8_t category) {
    NotifMsg& m = buf_[head_];
    copy(m.app, sizeof(m.app), app);
    copy(m.title, sizeof(m.title), title);
    copy(m.text, sizeof(m.text), text);
    m.category = category;
    m.unread = true;
    head_ = (head_ + 1) % kMax;
    if (n_ < kMax) ++n_;
    recount();
  }
  void markAllRead() { for (int i = 0; i < kMax; ++i) buf_[i].unread = false; unread_ = 0; }
 private:
  static void copy(char* dst, size_t cap, const char* src) {
    size_t k = 0;
    for (; src && src[k] && k + 1 < cap; ++k) dst[k] = (src[k] >= 0x20 && src[k] < 0x7F) ? src[k] : '?';  // printable ASCII only
    dst[k] = 0;
  }
  void recount() {
    unread_ = 0;
    for (int i = 0; i < n_; ++i) if (buf_[(head_ + kMax - 1 - i) % kMax].unread) ++unread_;
  }
  NotifMsg buf_[kMax];
  int n_, head_, unread_;
};
