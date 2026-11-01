#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>

constexpr const int TASK_DELAY = 10;

constexpr const int WIFI_RETRY_DELAY = 500;
constexpr const char* MQTT_SERVER = "192.168.1.21";
constexpr const int MQTT_PORT = 1883;

constexpr const char* ssid = "WifiSsid";
constexpr const char* password = "WifiPassword";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

QueueHandle_t queue;

void mqttTask(void *parameter);
void sensorTask(void *parameter);

void reconnectMQTT();

void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);


  // Task is pineed to core 1 with priority 2
  xTaskCreatePinnedToCore(mqttTask, "MqttTask", 6144, NULL, 2, NULL, 1);


  // MQTT/NETWORK task pinnned to core 0 alongside WI-FI stack (Priority 3)
  xTaskCreatePinnedToCore(mqttTask, "MqttTask", 6144, NULL, 3, NULL, 0);

}

void loop() {
  // put your main code here, to run repeatedly:
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void mqttTask(void *parameter) {
  Wifi.begin(ssid, password);

  while(Wifi.status() != WL_CONNECTED) {
    vTaskDelay(WIFI_RETRY_DELAY);
  }

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  for(;;) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }

    mqttClient.loop();

    char* payload = "{\"temperature\":30, \"time\": 1793538677}";
    mqttClient.publish("ESP32/sensor/", payload);

    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY));
  }

}

void reconnectMQTT() {

}

void sensorTask(void *parameter) {

}
