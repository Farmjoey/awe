#include "SettingsStorage.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include "BoardConfig.h"
#include "Diagnostics.h"
#include "AppProtocol.h"
#include "Theme.h"

static Preferences s_prefs;

void SettingsStorage::begin() {
  s_prefs.begin("nova", false);
  s_.demo = s_prefs.getBool("demo", true);
  s_.bleScan = s_prefs.getBool("ble", false);
  s_.phone = s_prefs.getBool("phone", false);
  s_.invert = s_prefs.getBool("inv", NOVA_DEFAULT_INVERT != 0);
  s_.colorMode = s_prefs.getUChar("cmode", NOVA_DEFAULT_COLORMODE);
  if (s_.colorMode > 2) s_.colorMode = 0;
  s_.rotation = s_prefs.getUChar("rot", 1);
  if (s_.rotation != 1 && s_.rotation != 3) s_.rotation = 1;
  s_.backlightPct = s_prefs.getUChar("bl", 80);
  if (s_.backlightPct < 10) s_.backlightPct = 10;
  if (s_.backlightPct > 100) s_.backlightPct = 100;
  s_.hwConfirmed = s_prefs.getBool("hwok", false);
  {
    String a = s_prefs.getString("taddr", "");
    String n = s_prefs.getString("tname", "");
    strlcpy(s_.targetAddr, a.c_str(), sizeof(s_.targetAddr));
    strlcpy(s_.targetName, n.c_str(), sizeof(s_.targetName));
    s_.targetType = s_prefs.getUChar("ttype", 0);
    s_.theme = s_prefs.getUChar("theme", 6);
    if (s_.theme >= kThemeCount) s_.theme = 6;
    s_.bootStyle = s_prefs.getUChar("bstyle", 0);
    if (s_.bootStyle >= app::kBootStyles) s_.bootStyle = 0;
    String bt = s_prefs.getString("btext", "");
    strlcpy(s_.bootText, bt.c_str(), sizeof(s_.bootText));
    s_.bootImgValid = s_prefs.getBool("bimgv", false);
    s_.layoutOn = s_prefs.getBool("lon", true);
    s_.layoutValid = s_prefs.getBool("lval", false);
    s_.startCap = s_prefs.getBool("scap", false);
    s_.arcMax = s_prefs.getUChar("amax", 0);
    if (s_.arcMax > 24) s_.arcMax = 0;
    s_.accent[0] = s_prefs.getUChar("acr", 111);
    s_.accent[1] = s_prefs.getUChar("acg", 163);
    s_.accent[2] = s_prefs.getUChar("acb", 168);
    s_.limValid = s_prefs.getBool("lsv", false);
    s_.limRaw[0] = s_prefs.getUShort("ls0", 0);
    s_.limRaw[1] = s_prefs.getUShort("ls1", 0);
    s_.limRaw[2] = s_prefs.getUShort("ls2", 0);
    s_.limWf = s_prefs.getUShort("lswf", 0);
  }

  // A different hardware profile was flashed: the old hardware test no longer applies.
  String stored = s_prefs.getString("prof", "");
  if (!stored.equals(NOVA_PROFILE_NAME)) {
    Diag::log("profile changed -> HW test required");
    s_.hwConfirmed = false;
    s_.invert = (NOVA_DEFAULT_INVERT != 0);
    s_.colorMode = NOVA_DEFAULT_COLORMODE;
    s_prefs.putString("prof", NOVA_PROFILE_NAME);
    save();
  }
}

void SettingsStorage::save() {
  s_prefs.putBool("demo", s_.demo);
  s_prefs.putBool("ble", s_.bleScan);
  s_prefs.putBool("phone", s_.phone);
  s_prefs.putBool("inv", s_.invert);
  s_prefs.putUChar("cmode", s_.colorMode);
  s_prefs.putUChar("rot", s_.rotation);
  s_prefs.putUChar("bl", s_.backlightPct);
  s_prefs.putBool("hwok", s_.hwConfirmed);
  s_prefs.putString("taddr", s_.targetAddr);
  s_prefs.putString("tname", s_.targetName);
  s_prefs.putUChar("ttype", s_.targetType);
  s_prefs.putUChar("theme", s_.theme);
  s_prefs.putUChar("bstyle", s_.bootStyle);
  s_prefs.putString("btext", s_.bootText);
  s_prefs.putBool("bimgv", s_.bootImgValid);
  s_prefs.putBool("lon", s_.layoutOn);
  s_prefs.putBool("lval", s_.layoutValid);
  s_prefs.putBool("scap", s_.startCap);
  s_prefs.putUChar("amax", s_.arcMax);
  s_prefs.putUChar("acr", s_.accent[0]);
  s_prefs.putUChar("acg", s_.accent[1]);
  s_prefs.putUChar("acb", s_.accent[2]);
  s_prefs.putBool("lsv", s_.limValid);
  s_prefs.putUShort("ls0", s_.limRaw[0]);
  s_prefs.putUShort("ls1", s_.limRaw[1]);
  s_prefs.putUShort("ls2", s_.limRaw[2]);
  s_prefs.putUShort("lswf", s_.limWf);
}

bool SettingsStorage::loadCal(uint8_t rotation, TouchCal& out) {
  char key[8];
  snprintf(key, sizeof(key), "cal%u", (unsigned)rotation);
  if (s_prefs.getBytesLength(key) != sizeof(TouchCal)) return false;
  TouchCal tmp;
  s_prefs.getBytes(key, &tmp, sizeof(TouchCal));
  if (tmp.magic != 0xC1 || !tmp.valid) return false;
  out = tmp;
  return true;
}

void SettingsStorage::saveCal(uint8_t rotation, const TouchCal& cal) {
  char key[8];
  snprintf(key, sizeof(key), "cal%u", (unsigned)rotation);
  s_prefs.putBytes(key, &cal, sizeof(TouchCal));
}

bool SettingsStorage::loadBootImg(uint8_t* out) {
  if (!s_.bootImgValid || s_prefs.getBytesLength("bimg") != app::kBootImgBytes) return false;
  return s_prefs.getBytes("bimg", out, app::kBootImgBytes) == app::kBootImgBytes;
}

void SettingsStorage::saveBootImg(const uint8_t* in) {
  s_prefs.putBytes("bimg", in, app::kBootImgBytes);
  s_.bootImgValid = true;
  save();
}

bool SettingsStorage::loadLayout(uint8_t* out, size_t cap, size_t& n) {
  const size_t len = s_prefs.getBytesLength("wlay");
  if (len == 0 || len > cap) return false;
  n = s_prefs.getBytes("wlay", out, len);
  return n == len;
}

void SettingsStorage::saveLayout(const uint8_t* in, size_t n) {
  s_prefs.putBytes("wlay", in, n);
  s_.layoutValid = true;
  s_.layoutOn = true;
  save();
}
