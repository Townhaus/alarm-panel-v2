#pragma once
#if defined(DESKTOP_SIM)
#include <cstdint>
#include <ctime>
uint32_t millis();
void delay(uint32_t ms);
bool getLocalTime(struct tm *info, uint32_t timeout = 0);
#else
#include <Arduino.h>
#endif
