#pragma once

#include "panel_platform.h"
#if !defined(DESKTOP_SIM)
#include <PubSubClient.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#endif
#include <lvgl.h>
#include <cstddef>

#include "panel_types.h"

// --- Shared constants ---

constexpr uint8_t BRIGHTNESS_FULL = 255;
constexpr uint8_t BRIGHTNESS_DIM = 40;
constexpr uint8_t BRIGHTNESS_OFF = 0;

// --- Extern globals: main.cpp ---

#if !defined(DESKTOP_SIM)
extern PubSubClient mqttClient;
extern Preferences preferences;
#endif
extern bool timeConfigured;

// --- Extern globals: display.cpp ---

#if !defined(DESKTOP_SIM)
extern TFT_eSPI tft;
#endif
extern uint8_t currentBrightness;
extern DisplayState displayState;
extern uint32_t displayTimeoutSec;
extern uint32_t lastTouchActivityMs;
extern uint32_t displayWakeIgnoreTouchUntilMs;
extern uint32_t lastLvglTickMs;

// --- Extern globals: mqtt.cpp ---

extern char perimeterAlarmState[32];
extern char interiorAlarmState[32];
extern char garageAlarmState[32];
extern char garageDoorState[32];
extern char weatherText[32];

// --- Extern globals: ui.cpp ---

extern ArmMode selectedArmMode;

// --- Function declarations: main.cpp ---

void feedWatchdog();
void saveSettings();

// --- Function declarations: display.cpp ---

void setDisplayBrightness(uint8_t brightness);
void wakeDisplay();
void resetIdleTimer();
void checkIdleTimeout();
void setupDisplay();
void setupTouch();
void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP);
void touchRead(lv_indev_drv_t *drv, lv_indev_data_t *data);

// --- Function declarations: mqtt.cpp ---

bool mqttReady();
bool connectMqtt();
void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
bool executeAlarmAction(AlarmTarget target, AlarmAction action, const char *code);

// --- Function declarations: ui.cpp ---

void setupLvgl();
void loopLvgl();
void showToast(const char *message);
void showScreensaver();
void hideScreensaver();
void checkTriggeredState();
void updateAlarmLabels();
void updateWeatherDisplay();
void updateClockDisplay();
bool handleAlarmoMessage(const char *topic, const char *payload, size_t length);
void updateMqttStatusLabel(bool connected);
void setStatusLabelText(const char *text, lv_color_t color);
const char *friendlyAlarmState(const char *state);
lv_color_t alarmStateColor(const char *state);
const char *targetState(AlarmTarget target);
const char *targetName(AlarmTarget target);
const char *armModeCommand(ArmMode mode);
bool isDisarmedState(const char *state);
AlarmLifecycleState parseAlarmLifecycleState(const char *state);
bool isTriggeredState(AlarmLifecycleState state);
AlarmAction nextActionForTargetState(AlarmTarget target, AlarmLifecycleState state);

#if defined(WOKWI_SIM) || defined(DESKTOP_SIM)
void handleSimInputChar(char c);
#endif
