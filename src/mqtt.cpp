#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <ctype.h>

#include "app_state.h"
#include "secrets.h"
#include "alarmo_events.h"

// --- Globals owned by this module ---

char perimeterAlarmState[32] = "unknown";
char interiorAlarmState[32] = "unknown";
char garageAlarmState[32] = "unknown";
char garageDoorState[32] = "unknown";
char weatherText[32] = "--";

// --- Functions ---

bool mqttReady() {
  return WiFi.status() == WL_CONNECTED && mqttClient.connected();
}

namespace {
#if defined(WOKWI_SIM)
constexpr const char *MQTT_HOST_RUNTIME = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT_RUNTIME = 1883;
constexpr const char *MQTT_USERNAME_RUNTIME = nullptr;
constexpr const char *MQTT_PASSWORD_RUNTIME = nullptr;
constexpr const char *MQTT_CLIENT_ID_RUNTIME = "alarm-panel-wokwi";
#else
constexpr const char *MQTT_HOST_RUNTIME = Secrets::MQTT_HOST;
constexpr uint16_t MQTT_PORT_RUNTIME = Secrets::MQTT_PORT;
constexpr const char *MQTT_USERNAME_RUNTIME = Secrets::MQTT_USERNAME;
constexpr const char *MQTT_PASSWORD_RUNTIME = Secrets::MQTT_PASSWORD;
constexpr const char *MQTT_CLIENT_ID_RUNTIME = Secrets::MQTT_CLIENT_ID;
#endif

void parseWeatherMessage(const char *message) {
  const char *temperatureKey = strstr(message, "\"temperature\":");
  const char *stateKey = strstr(message, "\"state\":\"");
  if (temperatureKey == nullptr || stateKey == nullptr) {
    return;
  }

  float temperature = 0.0f;
  if (sscanf(temperatureKey, "\"temperature\":%f", &temperature) != 1) {
    return;
  }

  stateKey += strlen("\"state\":\"");
  char condition[16] = "";
  size_t index = 0;
  while (stateKey[index] != '\0' && stateKey[index] != '"' && index < sizeof(condition) - 1) {
    condition[index] = stateKey[index];
    ++index;
  }
  condition[index] = '\0';

  if (condition[0] == '\0') {
    return;
  }

  condition[0] = static_cast<char>(toupper(static_cast<unsigned char>(condition[0])));
  snprintf(weatherText, sizeof(weatherText), "%d° %s", static_cast<int>(temperature + 0.5f),
           condition);
  updateWeatherDisplay();
}

bool publishAlarmoCommand(const char *command, const char *area, const char *code) {
  if (!mqttReady()) {
    Serial.println("[MQTT] Alarmo publish skipped, client not connected");
    return false;
  }

  char payload[160];
  const int written = (code != nullptr && code[0] != '\0')
                          ? snprintf(payload, sizeof(payload),
                                     "{\"command\":\"%s\",\"area\":\"%s\",\"code\":\"%s\"}",
                                     command, area, code)
                          : snprintf(payload, sizeof(payload),
                                     "{\"command\":\"%s\",\"area\":\"%s\"}", command, area);

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[MQTT] Alarmo payload too large");
    return false;
  }

  Serial.printf("[MQTT] Alarm command=%s area=%s\n", command, area);
  return mqttClient.publish(Secrets::ALARMO_COMMAND_TOPIC, payload);
}

}  // namespace

bool executeAlarmAction(AlarmTarget target, AlarmAction action, const char *code) {
  if (target == AlarmTarget::GarageDoor) {
    if (action == AlarmAction::Arm && !isDisarmedState(garageAlarmState)) {
      Serial.printf("[MQTT] Garage door open blocked, garage alarm state=%s\n", garageAlarmState);
      showToast("Disarm garage first");
      return false;
    }

    const AlarmLifecycleState currentState = parseAlarmLifecycleState(garageDoorState);
    const AlarmAction nextAction = nextActionForTargetState(target, currentState);
    if (nextAction != action) {
      Serial.println("[MQTT] Garage door already in requested state");
      return true;
    }

    if (!mqttReady()) {
      Serial.println("[MQTT] Garage door publish skipped, client not connected");
      showToast("Not connected");
      return false;
    }

    Serial.printf("[MQTT] Publish %s -> TOGGLE\n", Secrets::MQTT_TOPIC_GARAGE_DOOR_COMMAND);
    return mqttClient.publish(Secrets::MQTT_TOPIC_GARAGE_DOOR_COMMAND, "TOGGLE");
  }

  const char *command = nullptr;
  if (action == AlarmAction::Arm) {
    command = armModeCommand(selectedArmMode);
  } else if (action == AlarmAction::Disarm) {
    command = "disarm";
  }

  if (command == nullptr) {
    return false;
  }

  const char *area = nullptr;
  if (target == AlarmTarget::Perimeter) {
    area = "perimeter";
  } else if (target == AlarmTarget::Interior) {
    area = "interior";
  } else if (target == AlarmTarget::Garage) {
    area = "garage";
  }

  if (area == nullptr) {
    return false;
  }

  return publishAlarmoCommand(command, area, code);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  AlarmTarget eventTarget;
  if (alarmoEventTarget(topic, eventTarget)) {
    // Parse the original bounded payload; never truncate JSON or log event bodies.
    handleAlarmoMessage(topic, reinterpret_cast<const char *>(payload), length);
    return;
  }
  char message[96];
  const size_t copyLength = min(length, static_cast<unsigned int>(sizeof(message) - 1));
  memcpy(message, payload, copyLength);
  message[copyLength] = '\0';

  Serial.printf("[MQTT] Received %s -> %s\n", topic, message);

  if (strcmp(topic, Secrets::MQTT_TOPIC_DISPLAY_SET) == 0) {
    if (strcasecmp(message, "ON") == 0 || strcasecmp(message, "WAKE") == 0) {
      wakeDisplay();
      setDisplayBrightness(BRIGHTNESS_FULL);
      displayState = DisplayState::Active;
      return;
    }
    if (strcasecmp(message, "OFF") == 0 || strcasecmp(message, "SLEEP") == 0) {
      setDisplayBrightness(BRIGHTNESS_OFF);
      return;
    }
    if (strcasecmp(message, "TOGGLE") == 0) {
      if (displayState == DisplayState::Sleeping) {
        wakeDisplay();
      } else {
        setDisplayBrightness(BRIGHTNESS_OFF);
      }
      return;
    }
  }

  if (strcmp(topic, Secrets::MQTT_TOPIC_DISPLAY_TIMEOUT) == 0) {
    displayTimeoutSec = strtoul(message, nullptr, 10);
    Serial.printf("[UI] Display timeout set to %lu seconds\n",
                  static_cast<unsigned long>(displayTimeoutSec));
    saveSettings();
    if (displayTimeoutSec > 0) {
      resetIdleTimer();
    }
    return;
  }

  if (strcmp(topic, Secrets::MQTT_TOPIC_WEATHER_STATE) == 0) {
    parseWeatherMessage(message);
    return;
  }

  if (strcmp(topic, Secrets::MQTT_TOPIC_GARAGE_DOOR_STATE) == 0) {
    strlcpy(garageDoorState, message, sizeof(garageDoorState));
    updateAlarmLabels();
    return;
  }

  if (strcmp(topic, Secrets::ALARMO_PERIMETER_STATE_TOPIC) == 0) {
    strlcpy(perimeterAlarmState, message, sizeof(perimeterAlarmState));
    updateAlarmLabels();
    checkTriggeredState();
    return;
  }

  if (strcmp(topic, Secrets::ALARMO_INTERIOR_STATE_TOPIC) == 0) {
    strlcpy(interiorAlarmState, message, sizeof(interiorAlarmState));
    updateAlarmLabels();
    checkTriggeredState();
    return;
  }

  if (strcmp(topic, Secrets::ALARMO_GARAGE_STATE_TOPIC) == 0) {
    strlcpy(garageAlarmState, message, sizeof(garageAlarmState));
    updateAlarmLabels();
    checkTriggeredState();
  }
}

bool connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    updateMqttStatusLabel(false);
    return false;
  }

  if (mqttClient.connected()) {
    return true;
  }

  Serial.printf("[MQTT] Connecting to %s:%u\n", MQTT_HOST_RUNTIME, MQTT_PORT_RUNTIME);
  const bool connected =
      mqttClient.connect(MQTT_CLIENT_ID_RUNTIME, MQTT_USERNAME_RUNTIME, MQTT_PASSWORD_RUNTIME,
                         Secrets::MQTT_TOPIC_STATUS, 0, true, "offline");
  if (!connected) {
    Serial.printf("[MQTT] Connect failed, state=%d\n", mqttClient.state());
    updateMqttStatusLabel(false);
    return false;
  }

  Serial.println("[MQTT] Connected");
  mqttClient.publish(Secrets::MQTT_TOPIC_STATUS, "online", true);
  mqttClient.subscribe(Secrets::MQTT_TOPIC_DISPLAY_SET);
  mqttClient.subscribe(Secrets::MQTT_TOPIC_DISPLAY_TIMEOUT);
  mqttClient.subscribe(Secrets::MQTT_TOPIC_WEATHER_STATE);
  mqttClient.subscribe(Secrets::MQTT_TOPIC_GARAGE_DOOR_STATE);
  mqttClient.subscribe(Secrets::ALARMO_PERIMETER_STATE_TOPIC);
  mqttClient.subscribe(Secrets::ALARMO_INTERIOR_STATE_TOPIC);
  mqttClient.subscribe(Secrets::ALARMO_GARAGE_STATE_TOPIC);
  for (unsigned i = 0; i < 3; ++i)
    mqttClient.subscribe(alarmoEventTopic(static_cast<AlarmTarget>(i)));
  updateMqttStatusLabel(true);
  return true;
}
