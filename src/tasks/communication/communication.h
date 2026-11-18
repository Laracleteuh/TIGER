#pragma once
#include <Arduino.h>


#define BT_DRIVER_VERSION "BT_DRIVER_2026.2-RTOS"
#define BT_HARDWARE "HM-10 Arduino Module, please refer to the module's documentation for more information."
#define COM_DRIVER_VERSION "COM_DRIVER_2026.5-RTOS using BT_DRIVER_2026.2-HM10"

void setupCommunication();

void updateCommunication(int swdRaw);

float getBluetoothSpeedLimit();

void triggerBtBreak();

void markSetupComplete();