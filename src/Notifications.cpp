#include "Notifications.h"
#include <Arduino.h>
#include "AppleServices.h"

namespace {
NotifStore s_store;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

bool Notifications::available() const { return applesvc::ancsReady(); }

int Notifications::count() const {
  portENTER_CRITICAL(&s_mux);
  const int n = s_store.count();
  portEXIT_CRITICAL(&s_mux);
  return n;
}

int Notifications::unread() const {
  portENTER_CRITICAL(&s_mux);
  const int n = s_store.unread();
  portEXIT_CRITICAL(&s_mux);
  return n;
}

bool Notifications::get(int i, NotifMsg& out) const {
  portENTER_CRITICAL(&s_mux);
  const bool ok = s_store.get(i, out);
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

void Notifications::markAllRead() {
  portENTER_CRITICAL(&s_mux);
  s_store.markAllRead();
  portEXIT_CRITICAL(&s_mux);
}

void Notifications::clear() {
  portENTER_CRITICAL(&s_mux);
  s_store.clear();
  portEXIT_CRITICAL(&s_mux);
}

void Notifications::push(const char* app, const char* title, const char* text, uint8_t category) {
  portENTER_CRITICAL(&s_mux);
  s_store.push(app, title, text, category);
  portEXIT_CRITICAL(&s_mux);
}
