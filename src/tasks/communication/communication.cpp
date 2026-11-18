#include <Arduino_FreeRTOS.h>
#include "tasks/communication/communication.h"
#include "tasks/batterie/batterie.h"
#include "config.h"
#include "tasks/traction/traction.h"
#include "tasks/ISR/ISR.h"
#include "tasks/securite/securite.h"
#include "tasks/tourelle/tourelle.h"
#include "tasks/admin/admin.h"
#include "tasks/systeme/systeme.h"
#include "tasks/temperature/temperature.h"
#include "software_handlers/tankos_math/tankos_math.h"
#include "hardware_drivers/tankos_leds/tankos_leds.h"

#if HW_HAS_DHT11
#include "hardware_drivers/tankos_DHT_Sensor/tankos_DHT_Sensor.h"
#endif

#define BT_BAUD 9600

static bool setupIsComplete = false;
static TickType_t lastTempSendTick = 0;

unsigned long telemetryIntervalMs = 1000;
bool isTelemetryActive = true;
float btSpeedLimit = 1.0f;

#if HW_HAS_FAN_PWM
int manualFanSpeedPercent = 0;
const char* modeVentilation = "OFF";
int vitesseActuellePourcent = 0;
int dynTempMin = 25;
int dynTempMax = 45;
#endif

#if HW_HAS_TEMPERATURE
float lastKnownTempC = NAN;
#endif

#if HW_HAS_BT_HM10
static char btBuffer[32];
static uint8_t btIndex = 0;
enum BtBreakState { BT_NORMAL, BT_WAIT_PRINT, BT_BREAK_LOW };
static BtBreakState breakState = BT_NORMAL;
static TickType_t breakTickTimer = 0;
#endif

static char serialBuffer[32];
static uint8_t serialIndex = 0;

void markSetupComplete() {
  setupIsComplete = true;
}

void triggerBtBreak() {
#if HW_HAS_BT_HM10
  if (breakState == BT_NORMAL) {
    breakState = BT_WAIT_PRINT;
    breakTickTimer = xTaskGetTickCount();
  }
#endif
}

float getBluetoothSpeedLimit() {
  return btSpeedLimit;
}

// =============================================================================================
// TABLE DE ROUTAGE DES COMMANDES
// =============================================================================================

typedef void (*CmdHandler)(char* arg, Print& out);
struct CmdTable;

struct Command {
  const char* name;
  CmdHandler handler;
  const CmdTable* sub;
  bool needsArg;
};

struct CmdTable {
  const Command* cmds;
  uint8_t count;
  const char* unknownMsg;
  CmdHandler fallback;
};

#define PSTR_DEF(id, txt) static const char id[] PROGMEM = txt
#define TABLE_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void dispatch(const CmdTable* tablePtr, char* buffer, Print& out) {
  CmdTable table;
  memcpy_P(&table, tablePtr, sizeof(CmdTable));

  char* arg = NULL;
  char* sp = strchr(buffer, ' ');
  if (sp != NULL) {
    *sp = '\0';
    arg = sp + 1;
    while (*arg == ' ') arg++;
    if (*arg == '\0') arg = NULL;
  }

  for (uint8_t i = 0; i < table.count; i++) {
    Command cmd;
    memcpy_P(&cmd, &table.cmds[i], sizeof(Command));
    if (strcmp_P(buffer, cmd.name) != 0) continue;
    if (cmd.needsArg != (arg != NULL)) break;
    if (cmd.sub != NULL) dispatch(cmd.sub, arg, out);
    else cmd.handler(arg, out);
    return;
  }

  if (sp != NULL) *sp = ' ';
  if (table.fallback != NULL) {
    table.fallback(buffer, out);
  } else if (table.unknownMsg != NULL) {
    out.println((const __FlashStringHelper*)table.unknownMsg);
  }
}

// ---------------------------------------------------------------------------------------------
// Trame de telemetrie (BT uniquement)
// Format : TM,<temp C>,<batt principale %>,<batt tourelle %>,<courant A>,<limite vitesse %>
// Un champ vide signifie capteur absent ou mesure invalide.
// ---------------------------------------------------------------------------------------------
#if HW_HAS_BT_HM10
static void sendTelemetryFrame(Print& out) {
  out.print(F("TM,"));

#if HW_HAS_TEMPERATURE
  if (!isnan(lastKnownTempC)) {
    out.print(lastKnownTempC, 1);
  }
#endif
  out.print(',');

  out.print(getBatteryPercentage(MAIN_BATTERY_PIN));
  out.print(',');

#if HW_HAS_TURRET_BATTERY
  out.print(getBatteryPercentage(TURRET_BATTERY_PIN));
#endif
  out.print(',');

#if HW_HAS_CURRENT_SENSOR
  out.print(getCurrentAmps(), 1);
#endif
  out.print(',');

  out.print(static_cast<int>(btSpeedLimit * 100.0f + 0.5f));
  out.println();
}
#endif

// ---------------------------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------------------------

// -- reboot --
static void cmdRebootTank(char*, Print& out) {
  out.println(F("rebooting board"));
  rebootTank();
}

#if HW_HAS_TOURELLE
static void cmdRebootEsc(char*, Print& out) {
  out.println(F("resetting ESC"));
  setupTourelle();
  out.println(F("done"));
}
#endif

#if HW_HAS_BT_HM10
static void cmdRebootBt(char*, Print& out) {
  out.println(F("rebooting bluetooth module"));
  BT_SERIAL.print(F("AT+RESET"));
  out.println(F("done"));
}
#endif

// -- ver --
static void cmdVerArduino(char*, Print& out) {
  out.println(F("Arduino Mega 2560 - ATMEGA2560"));
}

static void cmdVerFirmware(char*, Print& out) {
  out.print(F("firmware version: "));
  out.print(F(FIRMWARE_VERSION));
  out.print(F(" | last build: "));
  out.println(F(LAST_BUILD_DATE));
}

static void cmdVerDrivers(char*, Print& out) {
  out.println(F("Drivers versions:"));
#if HW_HAS_RFID_RC522
  out.print(F(" - Admin handler: ")); out.println(F(AUTH_HANDLER_VERSION));
#endif
  out.print(F(" - Battery driver: ")); out.println(F(BATT_DRIVER_VERSION));
  out.print(F(" - Communication driver: ")); out.println(F(COM_DRIVER_VERSION));
  out.print(F(" - ISR handler: ")); out.println(F(ISR_HANDLER_VERSION));
  out.print(F(" - Traction driver: ")); out.println(F(TRACTION_MODULE_VERSION));
#if HW_HAS_TOURELLE
  out.print(F(" - Turret driver: ")); out.println(F(TOURELLE_DRIVER_VERSION));
#endif
  out.print(F(" - System module: ")); out.println(F(SYSTEM_MODULE_VERSION));
  out.print(F(" - Security module: ")); out.println(F(SECURITY_MODULE_VERSION));
  out.print(F(" - Math library: ")); out.println(F(TANKOSMATH_VERSION));
#if HW_HAS_TEMPERATURE
  out.print(F(" - Temperature module: ")); out.println(F(TEMPERATURE_MODULE_VERSION));
#endif
#if HW_HAS_STATUS_LEDS
  out.print(F(" - TankOS LED library: ")); out.println(F(LED_DRIVER_VERSION));
#endif
#if HW_HAS_DHT11
  out.print(F(" - TankOS DHT Sensor library: ")); out.println(F(TANKOS_DHT_SENSOR_HANDLER));
#endif
}

// -- stats --
static void cmdStatsOn(char*, Print& out) {
  isTelemetryActive = true;
  out.print(F("continuous stats on - "));
  out.print(telemetryIntervalMs);
  out.println(F(" ms interval"));
}

static void cmdStatsOff(char*, Print& out) {
  isTelemetryActive = false;
  out.println(F("continuous stats off"));
}

static void cmdStatsDelay(char* arg, Print& out) {
  int delaySec = atoi(arg);
  if (delaySec > 0) {
    telemetryIntervalMs = delaySec * 1000UL;
    out.print(F("stats delay set to "));
    out.print(delaySec);
    out.println(F(" second(s)"));
  } else {
    out.println(F("E0SD - delay must be > 0"));
  }
}

static void cmdStatsBattery(char*, Print& out) {
  int mainPct = getBatteryPercentage(MAIN_BATTERY_PIN);
  out.print(F("I0ST - battery main: "));
  out.print(mainPct);
  out.print(F("%"));
#if HW_HAS_TURRET_BATTERY
  int turretPct = getBatteryPercentage(TURRET_BATTERY_PIN);
  out.print(F(" | turret: "));
  out.print(turretPct);
  out.print(F("%"));
#endif
#if HW_HAS_CURRENT_SENSOR
  float amps = getCurrentAmps();
  out.print(F(" | current: "));
  out.print(amps, 2);
  out.print(F(" A"));
#endif
  out.println();
}

static void cmdStatsRam(char*, Print& out) {
  out.println(F("--- FreeRTOS RAM Manager (mots/words) ---"));
  out.print(F("Task Safety :   ")); out.println(uxTaskGetStackHighWaterMark(hTaskSafety));
  out.print(F("Task Traction : ")); out.println(uxTaskGetStackHighWaterMark(hTaskTraction));
#if HW_HAS_TOURELLE
  out.print(F("Task Tourelle : ")); out.println(uxTaskGetStackHighWaterMark(hTaskTourelle));
#endif
  out.print(F("Task Comm :     ")); out.println(uxTaskGetStackHighWaterMark(hTaskCommunication));
#if HW_HAS_RFID_RC522
  out.print(F("Task Admin :    ")); out.println(uxTaskGetStackHighWaterMark(hTaskAdmin));
#endif
#if HW_HAS_TEMPERATURE
  out.print(F("Task Temp :     ")); out.println(uxTaskGetStackHighWaterMark(hTaskTemperature));
#endif
  out.println(F("---------------------------------------------------"));
  out.print(F("Available RAM (bytes) : "));
  out.println(uxTaskGetStackHighWaterMark(NULL) * sizeof(configSTACK_DEPTH_TYPE));
}

// -- tare --
#if HW_HAS_CURRENT_SENSOR
static void cmdTareAmp(char*, Print& out) {
  tareCurrentSensor();
  out.println(F("ampSensor tare complete."));
}
#endif

// -- motor --
static void cmdMotorOff(char*, Print& out) {
  disableMotorDiagnostic();
  out.println(F("I0MD - motor diagnostic disabled"));
}

static void cmdMotorSet(char* line, Print& out) {
  char side;
  int spd;
  if (sscanf(line, "%c %d", &side, &spd) == 2) {
    if (side == 'L' || side == 'R') {
      spd = constrain(spd, -100, 100);
      setMotorDiagnostic(side, spd);
      out.print(F("I0MD - motor diagnostic enabled for "));
      out.print(side);
      out.print(F(" a "));
      out.print(spd);
      out.println(F("%"));
    } else {
      out.println(F("E0MD - invalid motor argument"));
    }
  } else {
    out.println(F("E0MD - invalid motor command syntax"));
  }
}

// -- fanrange / fanspd / fanctrl --
#if HW_HAS_FAN_PWM
static void cmdFanRangeWinter(char*, Print& out) {
  dynTempMin = 15;
  dynTempMax = 35;
  out.println(F("I0FR - winter season active (15C - 35C)."));
}

static void cmdFanRangeSummer(char*, Print& out) {
  dynTempMin = 30;
  dynTempMax = 55;
  out.println(F("I0FR - summer season active (30C - 55C)."));
}

static void cmdFanRangeShow(char*, Print& out) {
  out.print(F("I0FR - current thermal range : "));
  out.print(dynTempMin);
  out.print(F("C -> "));
  out.print(dynTempMax);
  out.println(F("C"));
}

static void cmdFanRangeSet(char* line, Print& out) {
  int tmin = 0, tmax = 0;
  if (sscanf(line, "%d %d", &tmin, &tmax) == 2) {
    if (tmin < tmax) {
      dynTempMin = tmin;
      dynTempMax = tmax;
      out.print(F("I0FR - new thermal range : "));
      out.print(dynTempMin);
      out.print(F("C -> "));
      out.print(dynTempMax);
      out.println(F("C"));
    } else {
      out.println(F("E0FR - T_MIN must be less than T_MAX."));
    }
  } else {
    out.println(F("E0FR - Error: Syntax"));
  }
}

static void cmdFanSpdShow(char*, Print& out) {
  out.print(F("I0FS - current fan speed: "));
  out.print(manualFanSpeedPercent);
  out.println(F("%"));
}

static void cmdFanSpdSet(char* arg, Print& out) {
  int val = atoi(arg);
  if (strcmp(modeVentilation, "AUTO") == 0) {
    out.println(F("E0FS - cannot set fan speed in AUTO mode."));
  } else if (strcmp(modeVentilation, "OFF") == 0) {
    out.println(F("E0FS - cannot set fan speed when fans are OFF."));
  } else if (strcmp(modeVentilation, "MANUAL") == 0) {
    if (val >= 0 && val <= 100) {
      manualFanSpeedPercent = val;
      out.print(F("I0FS - fan speed set to "));
      out.print(val);
      out.println(F("%"));
    } else {
      out.println(F("E0FS - invalid fanspd value"));
    }
  }
}

static void cmdFanCtrlShow(char*, Print& out) {
  out.print(F("I0FC - current fan control mode: "));
  out.println(modeVentilation);
}

static void cmdFanCtrlAuto(char*, Print& out) {
  modeVentilation = "AUTO";
  out.println(F("I0FC - fan control mode set to AUTO"));
}

static void cmdFanCtrlManual(char*, Print& out) {
  modeVentilation = "MANUAL";
  out.println(F("I0FC - fan control mode set to MANUAL"));
}

static void cmdFanCtrlOff(char*, Print& out) {
  modeVentilation = "OFF";
  out.println(F("I0FC - fan control mode set to OFF"));
}
#endif // HW_HAS_FAN_PWM

// -- mspd --
static void cmdMspdReset(char*, Print& out) {
  btSpeedLimit = 1.0f;
  out.println(F("I0MS - motor speed limit reset to 100%"));
}

static void cmdMspdShow(char*, Print& out) {
  int currentLimitPercent = static_cast<int>(btSpeedLimit * 100);
  out.print(F("I0MS - current motor speed limit: "));
  out.print(currentLimitPercent);
  out.println(F("%"));
}

static void cmdMspdSet(char* line, Print& out) {
  int val = atoi(line);
  if (val >= 0 && val <= 100) {
    btSpeedLimit = val / 100.0f;
    out.print(F("I0MS - motor speed limit set to "));
    out.print(val);
    out.println(F("%"));
  } else {
    out.println(F("E0MS - invalid mspd value"));
  }
}

// -- admin --
#if HW_HAS_RFID_RC522
static void cmdAdminUidShow(char*, Print& out) {
  printAuthorizedUids(out);
}

static void cmdAdminNotImplemented(char*, Print& out) {
  out.println(F("E0AUTH - not implemented"));
}
#endif

// -- commandes racine sans argument --
static void cmdMathTest(char*, Print& out) {
  out.print(F("sin30="));
  out.println(sinQ15(angleFromDeg(30)));
  out.print(F("cos60="));
  out.println(cosQ15(angleFromDeg(60)));
  out.print(F("atan2(1,1)="));
  out.println(angleToDeg(atan2BAM(1, 1)));
  out.print(F("hypot(3000,4000)="));
  out.println(hypotI(3000, 4000));
  out.print(F("rc 1551="));
  out.println(rcNormalize(1551, 980, 1500, 2000, 50));
  out.print(F("rc 2000="));
  out.println(rcNormalize(2000, 980, 1500, 2000, 50));
  float th = 0;
  if (ballisticElevation(10.0f, 0.0f, 20.0f, th)) {
    out.print(F("ballistic(10m,20m/s)="));
    out.println(th, 2);
  }
}

static void cmdPing(char*, Print& out) {
  out.println(F("pong"));
}

static void cmdWhois(char*, Print& out) {
  out.println(F(FIRMWARE_AUTHOR));
}

static void cmdReset(char*, Print& out) {
  btSpeedLimit = 1.0f;
#if HW_HAS_FAN_PWM
  manualFanSpeedPercent = 0;
#endif
  out.println(F("default values restored"));
}

#if HW_HAS_BT_HM10
static void cmdBtBreak(char*, Print& out) {
  out.println(F("breaking"));
  out.println(F(" -- CONNECTION LOST --"));
  triggerBtBreak();
}
#endif

#if HW_HAS_TEMPERATURE
static void cmdTemp(char*, Print& out) {
  if (isnan(lastKnownTempC)) {
    out.println(F("E0TEMP - no valid temperature reading"));
    return;
  }
  out.print(F("current temperature: "));
  out.print(lastKnownTempC);
  out.println(F("C"));
}
#endif

static void cmdUnknown(char* line, Print& out) {
  out.print(F("E0CMD - unknown command: "));
  out.println(line);
}

// ---------------------------------------------------------------------------------------------
// Noms (PROGMEM)
// ---------------------------------------------------------------------------------------------
PSTR_DEF(N_ON, "on");
PSTR_DEF(N_OFF, "off");
PSTR_DEF(N_SHOW, "show");
PSTR_DEF(N_RESET, "reset");
PSTR_DEF(N_ARDUINO, "arduino");
PSTR_DEF(N_TANK, "tank");
PSTR_DEF(N_FIRMWARE, "firmware");
PSTR_DEF(N_DRIVERS, "drivers");
PSTR_DEF(N_STATUS, "status");
PSTR_DEF(N_DELAY, "delay");
PSTR_DEF(N_BATTERY, "battery");
PSTR_DEF(N_RAM, "ram");
PSTR_DEF(N_REBOOT, "reboot");
PSTR_DEF(N_VER, "ver");
PSTR_DEF(N_STATS, "stats");
PSTR_DEF(N_MOTOR, "motor");
PSTR_DEF(N_MSPD, "mspd");
PSTR_DEF(N_MATHTEST, "mathtest");
PSTR_DEF(N_PING, "ping");
PSTR_DEF(N_WHOIS, "whois");

PSTR_DEF(M_E0RB, "E0RB - unknown reboot argument");
PSTR_DEF(M_E0VR, "E0VR - unknown ver argument");
PSTR_DEF(M_E0ST, "E0ST - unknown stats argument");
PSTR_DEF(M_E0ST_STATUS, "E0ST - unknown status argument");

#if HW_HAS_TOURELLE
PSTR_DEF(N_ESC, "esc");
#endif

#if HW_HAS_BT_HM10
PSTR_DEF(N_BT, "bt");
PSTR_DEF(N_BTBRK, "btbrk");
#endif

#if HW_HAS_CURRENT_SENSOR
PSTR_DEF(N_AMPSENSOR, "ampSensor");
PSTR_DEF(N_TARE, "tare");
PSTR_DEF(M_E0TA, "E0TA - unknown tare argument");
#endif

#if HW_HAS_FAN_PWM
PSTR_DEF(N_SET, "set");
PSTR_DEF(N_AUTO, "auto");
PSTR_DEF(N_MANUAL, "manual");
PSTR_DEF(N_SEASON, "season");
PSTR_DEF(N_WINTER, "winter");
PSTR_DEF(N_SUMMER, "summer");
PSTR_DEF(N_FANRANGE, "fanrange");
PSTR_DEF(N_FANSPD, "fanspd");
PSTR_DEF(N_FANCTRL, "fanctrl");
PSTR_DEF(M_E0FR_SEASON, "E0FR - unknown season");
PSTR_DEF(M_E0FS, "E0FS - unknown fanspd argument.");
PSTR_DEF(M_E0FC, "E0FC - unknown fanctrl argument.");
#endif

#if HW_HAS_RFID_RC522
PSTR_DEF(N_UID_AUTH, "UID_AUTH");
PSTR_DEF(N_ADD, "add");
PSTR_DEF(N_REMOVE, "remove");
PSTR_DEF(N_ADMIN, "admin");
PSTR_DEF(M_E0AUTH, "E0AUTH - unknown admin argument");
#endif

#if HW_HAS_TEMPERATURE
PSTR_DEF(N_TEMP, "temp");
#endif

// ---------------------------------------------------------------------------------------------
// Tables de routage
// ---------------------------------------------------------------------------------------------

static const Command REBOOT_CMDS[] PROGMEM = {
  { N_ARDUINO, cmdRebootTank, NULL, false },
  { N_TANK,    cmdRebootTank, NULL, false },
#if HW_HAS_TOURELLE
  { N_ESC,     cmdRebootEsc,  NULL, false },
#endif
#if HW_HAS_BT_HM10
  { N_BT,      cmdRebootBt,   NULL, false },
#endif
};
static const CmdTable REBOOT_TABLE PROGMEM = { REBOOT_CMDS, TABLE_SIZE(REBOOT_CMDS), M_E0RB, NULL };

static const Command VER_CMDS[] PROGMEM = {
  { N_ARDUINO,  cmdVerArduino,  NULL, false },
  { N_FIRMWARE, cmdVerFirmware, NULL, false },
  { N_DRIVERS,  cmdVerDrivers,  NULL, false },
};
static const CmdTable VER_TABLE PROGMEM = { VER_CMDS, TABLE_SIZE(VER_CMDS), M_E0VR, NULL };

static const Command STATS_STATUS_CMDS[] PROGMEM = {
  { N_ON,  cmdStatsOn,  NULL, false },
  { N_OFF, cmdStatsOff, NULL, false },
};
static const CmdTable STATS_STATUS_TABLE PROGMEM = { STATS_STATUS_CMDS, TABLE_SIZE(STATS_STATUS_CMDS), M_E0ST_STATUS, NULL };

static const Command STATS_CMDS[] PROGMEM = {
  { N_STATUS,  NULL,            &STATS_STATUS_TABLE, true  },
  { N_DELAY,   cmdStatsDelay,   NULL,                true  },
  { N_BATTERY, cmdStatsBattery, NULL,                false },
  { N_RAM,     cmdStatsRam,     NULL,                false },
};
static const CmdTable STATS_TABLE PROGMEM = { STATS_CMDS, TABLE_SIZE(STATS_CMDS), M_E0ST, NULL };

#if HW_HAS_CURRENT_SENSOR
static const Command TARE_CMDS[] PROGMEM = {
  { N_AMPSENSOR, cmdTareAmp, NULL, false },
};
static const CmdTable TARE_TABLE PROGMEM = { TARE_CMDS, TABLE_SIZE(TARE_CMDS), M_E0TA, NULL };
#endif

static const Command MOTOR_CMDS[] PROGMEM = {
  { N_OFF, cmdMotorOff, NULL, false },
};
static const CmdTable MOTOR_TABLE PROGMEM = { MOTOR_CMDS, TABLE_SIZE(MOTOR_CMDS), NULL, cmdMotorSet };

#if HW_HAS_FAN_PWM
static const Command FANRANGE_SEASON_CMDS[] PROGMEM = {
  { N_WINTER, cmdFanRangeWinter, NULL, false },
  { N_SUMMER, cmdFanRangeSummer, NULL, false },
};
static const CmdTable FANRANGE_SEASON_TABLE PROGMEM = { FANRANGE_SEASON_CMDS, TABLE_SIZE(FANRANGE_SEASON_CMDS), M_E0FR_SEASON, NULL };

static const Command FANRANGE_CMDS[] PROGMEM = {
  { N_SEASON, NULL,            &FANRANGE_SEASON_TABLE, true  },
  { N_SHOW,   cmdFanRangeShow, NULL,                   false },
};
static const CmdTable FANRANGE_TABLE PROGMEM = { FANRANGE_CMDS, TABLE_SIZE(FANRANGE_CMDS), NULL, cmdFanRangeSet };

static const Command FANSPD_CMDS[] PROGMEM = {
  { N_SHOW, cmdFanSpdShow, NULL, false },
  { N_SET,  cmdFanSpdSet,  NULL, true  },
};
static const CmdTable FANSPD_TABLE PROGMEM = { FANSPD_CMDS, TABLE_SIZE(FANSPD_CMDS), M_E0FS, NULL };

static const Command FANCTRL_CMDS[] PROGMEM = {
  { N_SHOW,   cmdFanCtrlShow,   NULL, false },
  { N_AUTO,   cmdFanCtrlAuto,   NULL, false },
  { N_MANUAL, cmdFanCtrlManual, NULL, false },
  { N_OFF,    cmdFanCtrlOff,    NULL, false },
};
static const CmdTable FANCTRL_TABLE PROGMEM = { FANCTRL_CMDS, TABLE_SIZE(FANCTRL_CMDS), M_E0FC, NULL };
#endif

static const Command MSPD_CMDS[] PROGMEM = {
  { N_RESET, cmdMspdReset, NULL, false },
  { N_SHOW,  cmdMspdShow,  NULL, false },
};
static const CmdTable MSPD_TABLE PROGMEM = { MSPD_CMDS, TABLE_SIZE(MSPD_CMDS), NULL, cmdMspdSet };

#if HW_HAS_RFID_RC522
static const Command ADMIN_UID_CMDS[] PROGMEM = {
  { N_SHOW,   cmdAdminUidShow,        NULL, false },
  { N_ADD,    cmdAdminNotImplemented, NULL, false },
  { N_REMOVE, cmdAdminNotImplemented, NULL, false },
};
static const CmdTable ADMIN_UID_TABLE PROGMEM = { ADMIN_UID_CMDS, TABLE_SIZE(ADMIN_UID_CMDS), M_E0AUTH, NULL };

static const Command ADMIN_CMDS[] PROGMEM = {
  { N_UID_AUTH, NULL, &ADMIN_UID_TABLE, true },
};
static const CmdTable ADMIN_TABLE PROGMEM = { ADMIN_CMDS, TABLE_SIZE(ADMIN_CMDS), M_E0AUTH, NULL };
#endif

static const Command TOP_CMDS[] PROGMEM = {
  { N_REBOOT,   NULL,        &REBOOT_TABLE,   true  },
  { N_VER,      NULL,        &VER_TABLE,      true  },
  { N_STATS,    NULL,        &STATS_TABLE,    true  },
#if HW_HAS_CURRENT_SENSOR
  { N_TARE,     NULL,        &TARE_TABLE,     true  },
#endif
  { N_MOTOR,    NULL,        &MOTOR_TABLE,    true  },
#if HW_HAS_FAN_PWM
  { N_FANRANGE, NULL,        &FANRANGE_TABLE, true  },
  { N_FANSPD,   NULL,        &FANSPD_TABLE,   true  },
  { N_FANCTRL,  NULL,        &FANCTRL_TABLE,  true  },
#endif
  { N_MSPD,     NULL,        &MSPD_TABLE,     true  },
#if HW_HAS_RFID_RC522
  { N_ADMIN,    NULL,        &ADMIN_TABLE,    true  },
#endif
  { N_MATHTEST, cmdMathTest, NULL,            false },
  { N_PING,     cmdPing,     NULL,            false },
  { N_WHOIS,    cmdWhois,    NULL,            false },
  { N_RESET,    cmdReset,    NULL,            false },
#if HW_HAS_BT_HM10
  { N_BTBRK,    cmdBtBreak,  NULL,            false },
#endif
#if HW_HAS_TEMPERATURE
  { N_TEMP,     cmdTemp,     NULL,            false },
#endif
};
static const CmdTable TOP_TABLE PROGMEM = { TOP_CMDS, TABLE_SIZE(TOP_CMDS), NULL, cmdUnknown };

// ---------------------------------------------------------------------------------------------
// Point d'entree unique
// ---------------------------------------------------------------------------------------------
static void processCommand(char* buffer, Print& out) {
  if (isAdminCommand(buffer) && !isAdminUnlocked()) {
    out.println(F("E0AUTH - commande reservee a l'admin. Badgez le lecteur NFC pour deverrouiller."));
    return;
  }
  dispatch(&TOP_TABLE, buffer, out);
}

void setupCommunication() {
#if HW_HAS_BT_HM10
  BT_SERIAL.begin(BT_BAUD);
  pinMode(BT_BRK_PIN, OUTPUT);
  digitalWrite(BT_BRK_PIN, HIGH);
  Serial.println(F("Module Communication (BT + Serie) pret. Tapez vos commandes."));
#else
  Serial.println(F("Module Communication (Serie USB uniquement) pret. Tapez vos commandes."));
#endif
}

void updateCommunication(int swdRaw) {
  TickType_t currentTick = xTaskGetTickCount();

#if HW_HAS_BT_HM10
  if (breakState == BT_WAIT_PRINT) {
    if ((currentTick - breakTickTimer) * portTICK_PERIOD_MS >= 50) {
      digitalWrite(BT_BRK_PIN, LOW);
      breakState = BT_BREAK_LOW;
      breakTickTimer = currentTick;
    }
  } else if (breakState == BT_BREAK_LOW) {
    if ((currentTick - breakTickTimer) * portTICK_PERIOD_MS >= 200) {
      digitalWrite(BT_BRK_PIN, HIGH);
      breakState = BT_NORMAL;
    }
  }

  while (BT_SERIAL.available() > 0) {
    char c = BT_SERIAL.read();
    if (c == '\n' || c == '\r') {
      if (btIndex > 0) {
        btBuffer[btIndex] = '\0';
        btIndex = 0;
        processCommand(btBuffer, BT_SERIAL);
      }
    } else if (btIndex < sizeof(btBuffer) - 1) {
      btBuffer[btIndex++] = c;
    }
  }
#endif

  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialIndex > 0) {
        serialBuffer[serialIndex] = '\0';
        serialIndex = 0;
        processCommand(serialBuffer, Serial);
      }
    } else if (serialIndex < sizeof(serialBuffer) - 1) {
      serialBuffer[serialIndex++] = c;
    }
  }

#if HW_HAS_TEMPERATURE
  float t = getTemperatureC();
  if (!isnan(t)) {
    lastKnownTempC = t;
  }
#endif

#if HW_HAS_FAN_PWM
  if (swdRaw < 1250) {
    OCR2B = 0;
    modeVentilation = "OFF";
    vitesseActuellePourcent = 0;
  } else if (swdRaw >= 1250 && swdRaw <= 1750) {
    int pwmValue = map(manualFanSpeedPercent, 0, 100, 0, 79);
    OCR2B = constrain(pwmValue, 0, 79);
    modeVentilation = "MANUAL";
    vitesseActuellePourcent = manualFanSpeedPercent;
  } else {
    modeVentilation = "AUTO";
#if HW_HAS_TEMPERATURE
    if (!isnan(lastKnownTempC)) {
      int fanAutoSpeed = map((int)lastKnownTempC, dynTempMin, dynTempMax, 0, 79);
      OCR2B = constrain(fanAutoSpeed, 0, 79);
    } else {
      OCR2B = 40;
    }
#else
    OCR2B = 40;
#endif
    vitesseActuellePourcent = map(OCR2B, 0, 79, 0, 100);
  }
#else
  (void)swdRaw;
#endif

#if HW_HAS_BT_HM10
  if (setupIsComplete && isTelemetryActive &&
      ((currentTick - lastTempSendTick) * portTICK_PERIOD_MS >= telemetryIntervalMs)) {
    sendTelemetryFrame(BT_SERIAL);
    lastTempSendTick = currentTick;
  }
#else
  (void)currentTick;
  (void)lastTempSendTick;
#endif
}