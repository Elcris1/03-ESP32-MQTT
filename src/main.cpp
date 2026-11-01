#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>

constexpr const int TASK_DELAY = 10;
constexpr const int RETRY_DELAY = 2000;


constexpr const int WIFI_RETRY_DELAY = 500;

#if defined(ENV_WOKWI)
//WiFi constants
constexpr const char* WIFI_SSID = "Wokwi-GUEST";
constexpr const char* WIFI_PASSWORD = "";
constexpr const int WIFI_CHANNEL = 6;

//MQTT constants
constexpr const char* MQTT_SERVER = "host.wokwi.internal";
constexpr const int MQTT_PORT = 1883;
constexpr const char* MQTT_USER = "";
constexpr const char* MQTT_PASSWORD = "";
#elif defined(ENV_REAL)
//WiFi constants
constexpr const char* WIFI_SSID = "WifiSsid";
constexpr const char* WIFI_PASSWORD = "WifiPassword";
constexpr const int WIFI_CHANNEL = 0;

//MQTT constants
constexpr const char* MQTT_SERVER = "192.168.1.21";
constexpr const int MQTT_PORT = 1883;
constexpr const char* MQTT_USER = "";
constexpr const char* MQTT_PASSWORD = "";
#endif

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

QueueHandle_t queue;

void mqttTask(void *parameter);
void sensorTask(void *parameter);

void connectWifi();
void connectMQTT();

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  // Initialize external connections
  connectWifi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  connectMQTT();

  // Task is pineed to core 1 with priority 2
  xTaskCreatePinnedToCore(mqttTask, "SensorTask", 6144, NULL, 2, NULL, 1);

  // MQTT/NETWORK task pinnned to core 0 alongside WI-FI stack (Priority 1)
  xTaskCreatePinnedToCore(mqttTask, "MqttTask", 8192, NULL, 1, NULL, 0);

}

void loop() {
  // put your main code here, to run repeatedly:
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void mqttTask(void *parameter) {

  for(;;) {
    if(WiFi.status() != WL_CONNECTED) {
      connectWifi();
    }

    if (!mqttClient.connected()) {
      connectMQTT();
    }

    mqttClient.loop();

    char* payload = "{\"temperature\":30, \"time\": 1793538677}";
    mqttClient.publish("ESP32/sensor/", payload);

    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY));
  }

}

void connectWifi() {
  Serial.print("Connecting to Wi-Fi");
  if (WIFI_CHANNEL == 6) {
    //configuration for wokwi simulation
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  } else { 
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY));
    Serial.print(".");
  }

  Serial.printf("\nWi-Fi conneted - IP: %s\n", WiFi.localIP());
}

void connectMQTT() {

  while(!mqttClient.connected()) {
    Serial.println("Connecting to Broker MQTT");

    char* clienntId = "ESP32";

    bool connected;
    if (MQTT_USER[0] != '\0') {
      connected = mqttClient.connect(clienntId, MQTT_USER, MQTT_PASSWORD);
    } else {
      connected = mqttClient.connect(clienntId);
    }

    if (connected) {
      Serial.println("ESP-032 Connected to MQTT Broker!");
    } else {
      Serial.printf("Connetion failed: %s\n", mqttClient.state());
      vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY));
    }

  }
}

void sensorTask(void *parameter) {
  vTaskDelay(pdMS_TO_TICKS(1000));

}
