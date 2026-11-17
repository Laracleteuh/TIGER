#pragma once
#include <Arduino.h>
#include "config.h"

#define AUTH_HANDLER_VERSION "2026.2-RC522-RTOS"

#if HW_HAS_RFID_RC522
void setupAdmin();
void updateAdmin();
bool isAdminUnlocked();
bool isAdminCommand(const char* buffer);
void printAuthorizedUids(Print& out);
#else
inline void setupAdmin() {}
inline void updateAdmin() {}
inline bool isAdminUnlocked() { return true; }
inline bool isAdminCommand(const char*) { return false; }
inline void printAuthorizedUids(Print& out) { out.println(F("RFID module disabled.")); }
#endif