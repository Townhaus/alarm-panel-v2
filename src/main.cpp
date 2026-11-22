#include <Arduino.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include "app_state.h"
#include "secrets.h"

// --- Globals owned by this module ---

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Preferences preferences;
bool timeConfigured = false;

namespace {
bool otaStarted = false;
uint32_t lastMqttReconnectAttemptMs = 0;

constexpr uint32_t WATCHDOG_TIMEOUT_SEC = 30;
constexpr char NVS_NAMESPACE[] = "alarm-panel";
constexpr char NVS_KEY_TIMEOUT[] = "dispTimeout";
constexpr char NVS_KEY_BRIGHTNESS[] = "brightness";

#if defined(WOKWI_SIM)
constexpr char WIFI_SSID_RUNTIME[] = "Wokwi-GUEST";
constexpr char WIFI_PASSWORD_RUNTIME[] = "";
constexpr char MQTT_HOST_RUNTIME[] = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT_RUNTIME = 1883;
#else
constexpr const char *WIFI_SSID_RUNTIME = Secrets::WIFI_SSID;
constexpr const char *WIFI_PASSWORD_RUNTIME = Secrets::WIFI_PASSWORD;
constexpr const char *MQTT_HOST_RUNTIME = Secrets::MQTT_HOST;
constexpr uint16_t MQTT_PORT_RUNTIME = Secrets::MQTT_PORT;
#endif
}  // namespace

// --- Functions ---

void loadSettings() {
  preferences.begin(NVS_NAMESPACE, true);
  displayTimeoutSec = preferences.getUInt(NVS_KEY_TIMEOUT, 30);
  uint8_t savedBrightness = preferences.getUChar(NVS_KEY_BRIGHTNESS, BRIGHTNESS_FULL);
  preferences.end();

  if (savedBrightness != BRIGHTNESS_FULL && savedBrightness != BRIGHTNESS_DIM) {
    savedBrightness = BRIGHTNESS_FULL;
  }

  Serial.printf("[NVS] Loaded: timeout=%lu, brightness=%u\n",
                static_cast<unsigned long>(displayTimeoutSec), savedBrightness);
}

void saveSettings() {
  preferences.begin(NVS_NAMESPACE, false);
  preferences.putUInt(NVS_KEY_TIMEOUT, displayTimeoutSec);
  preferences.putUChar(NVS_KEY_BRIGHTNESS, currentBrightness);
  preferences.end();
  Serial.println("[NVS] Settings saved");
}

void feedWatchdog() {
  const esp_err_t result = esp_task_wdt_reset();
  if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) {
    Serial.printf("[WDT] Feed failed: %d\n", static_cast<int>(result));
  }
}

namespace {
void setupWatchdog() {
  const esp_err_t initResult = esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);
  if (initResult != ESP_OK) {
    Serial.printf("[WDT] Init failed: %d\n", static_cast<int>(initResult));
    return;
  }

  enableLoopWDT();
  feedWatchdog();
  Serial.printf("[WDT] Enabled (%lu s)\n", static_cast<unsigned long>(WATCHDOG_TIMEOUT_SEC));
}

void setupOta() {
  if (otaStarted || WiFi.status() != WL_CONNECTED) {
    return;
  }

  ArduinoOTA.setHostname(Secrets::OTA_HOSTNAME);
  ArduinoOTA.setPassword(Secrets::OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress * 100U) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]\n", error);
  });

  ArduinoOTA.begin();
  otaStarted = true;
  Serial.printf("[OTA] Ready at %s.local\n", Secrets::OTA_HOSTNAME);
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.printf("[WIFI] Connecting to %s\n", WIFI_SSID_RUNTIME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID_RUNTIME, WIFI_PASSWORD_RUNTIME);

  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    feedWatchdog();
    delay(500);
    Serial.print(".");
    ++retries;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] Connected, IP=%s\n", WiFi.localIP().toString().c_str());
    configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
    timeConfigured = true;
    setupOta();
  } else {
    Serial.println("[WIFI] Connection timed out");
  }
}

void ensureConnectivity() {
  if (WiFi.status() != WL_CONNECTED) {
    updateMqttStatusLabel(false);
    connectWifi();
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastMqttReconnectAttemptMs < 3000) {
    return;
  }

  lastMqttReconnectAttemptMs = now;
  connectMqtt();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("[BOOT] ESP32 home keypad proof of concept");

  loadSettings();
  setupDisplay();
  setupTouch();
  setupLvgl();
  setupWatchdog();
  mqttClient.setServer(MQTT_HOST_RUNTIME, MQTT_PORT_RUNTIME);
  mqttClient.setBufferSize(1152); // Room for event JSON including sensor metadata.
  mqttClient.setCallback(mqttCallback);

  lastLvglTickMs = millis();
  lastTouchActivityMs = millis();

  connectWifi();
  connectMqtt();

#if defined(WOKWI_SIM)
  Serial.println("[SIM] u=disarm house o=open garage w=Away n=Night p/i/g/d=individual 0-9=PIN s=submit c=back/cancel x=delete");
#endif
}

void loop() {
  feedWatchdog();
  ensureConnectivity();
  if (otaStarted) {
    ArduinoOTA.handle();
  }
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
#if defined(WOKWI_SIM)
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value >= 0) {
      handleSimInputChar(static_cast<char>(value));
    }
  }
#endif
  loopLvgl();
}
