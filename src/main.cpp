#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

constexpr const int TASK_DELAY = 1000;
constexpr const int RETRY_DELAY = 2000;
constexpr const int WIFI_RETRY_DELAY = 500;
constexpr const int SAMPLE_PERIOD = 500;

#if defined(ENV_WOKWI)
// WiFi constants
constexpr const char *WIFI_SSID = "Wokwi-GUEST";
constexpr const char *WIFI_PASSWORD = "";
constexpr const int WIFI_CHANNEL = 6;

// MQTT constants
constexpr const char *MQTT_SERVER = "host.wokwi.internal";
constexpr const int MQTT_PORT = 1883;
constexpr const char *MQTT_USER = "";
constexpr const char *MQTT_PASSWORD = "";
#elif defined(ENV_REAL)
// WiFi constants
constexpr const char *WIFI_SSID = "ssid";
constexpr const char *WIFI_PASSWORD = "pssd";
constexpr const int WIFI_CHANNEL = 0;

// MQTT constants
constexpr const char *MQTT_SERVER = "192.168.0.30";
constexpr const int MQTT_PORT = 1883;
constexpr const char *MQTT_USER = "";
constexpr const char *MQTT_PASSWORD = "";
#endif

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

Adafruit_ADXL345_Unified accelerometer = Adafruit_ADXL345_Unified(12345);

QueueHandle_t queue;

void mqttTask(void *parameter);
void sensorTask(void *parameter);

void connectWifi();
void connectMQTT();

enum class Orientation
{
  UP,
  DOWN,
  LEFT,
  RIGHT,
  UNKNOWN
};

Orientation classifyOrientation(float x, float y)
{
  constexpr float MIN_TILT = 6.0f; // m/s²; ignore small deviations

  if (fabsf(x) < MIN_TILT && fabsf(y) < MIN_TILT)
  {
    return Orientation::UNKNOWN;
  }

  if (fabsf(x) > fabsf(y))
  {
    return x < 0 ? Orientation::LEFT : Orientation::RIGHT;
  }

  return y > 0 ? Orientation::UP : Orientation::DOWN;
}

const char *orientationName(Orientation orientation)
{
  switch (orientation)
  {
  case Orientation::UP:
    return "UP";
  case Orientation::DOWN:
    return "DOWN";
  case Orientation::LEFT:
    return "LEFT";
  case Orientation::RIGHT:
    return "RIGHT";
  default:
    return "UNKNOWN";
  }
}

struct SensorData
{
  float x;
  float y;
  float z;
  Orientation orientation;
};

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);

  // Initialize wire connecitons
  // I²C del ESP32: SDA = GPIO 21, SCL = GPIO 22
  Wire.begin(21, 22);

  if (!accelerometer.begin())
  {
    Serial.println("ADXL345 was not found. Trying again...");
    while (true)
    {
      delay(1000);
    }
  }
  accelerometer.setRange(ADXL345_RANGE_2_G);
  Serial.println("ADXL345 ready");

  queue = xQueueCreate(10, sizeof(SensorData));
  if (queue == nullptr)
  {
    Serial.println("There was an error creating the queue");
    return;
  }

  // Initialize external network connections
  connectWifi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  connectMQTT();

  // Task initialization
  xTaskCreate(sensorTask, "SensorTask", 4096, nullptr, 1, nullptr);

  // MQTT/NETWORK task pinnned to core 0 alongside WI-FI stack (Priority 1)
  xTaskCreatePinnedToCore(mqttTask, "MqttTask", 8192, NULL, 1, NULL, 0);
}

void loop()
{
  // put your main code here, to run repeatedly:
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void mqttTask(void *parameter)
{

  SensorData prev{};
  bool hasPrev = false;
  SensorData current{};

  for (;;)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      connectWifi();
    }

    if (!mqttClient.connected())
    {
      connectMQTT();
    }

    mqttClient.loop();

    if (xQueueReceive(queue, &current, pdMS_TO_TICKS(SAMPLE_PERIOD)) == pdTRUE)
    {
      if (hasPrev && prev.orientation != current.orientation)
      {

        char payload[100];
        snprintf(
            payload,
            sizeof(payload),
            "{\"orientation\":\"%s\", \"x\": %.2f, \"y\": %.2f, \"z\": %.2f}",
            orientationName(current.orientation), current.x, current.y, current.z);

        if (mqttClient.publish("ESP32/ADXL345/", payload))
        {
          Serial.println("MQTT message sent!");
        }
      }
      prev = current;
      hasPrev = true;
    }

    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY));
  }
}

void connectWifi()
{
  Serial.print("Connecting to Wi-Fi");
  if (WIFI_CHANNEL == 6)
  {
    // configuration for wokwi simulation
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  }
  else
  {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY));
    Serial.print(".");
  }

  Serial.print("\nWi-Fi connected - IP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT()
{

  while (!mqttClient.connected())
  {
    Serial.println("Connecting to Broker MQTT");

    char *clienntId = "ESP32";

    bool connected;
    if (MQTT_USER[0] != '\0')
    {
      connected = mqttClient.connect(clienntId, MQTT_USER, MQTT_PASSWORD);
    }
    else
    {
      connected = mqttClient.connect(clienntId);
    }

    if (connected)
    {
      Serial.println("ESP-032 Connected to MQTT Broker!");
    }
    else
    {
      Serial.print("Connection failed: ");
      Serial.println(mqttClient.state());
      vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY));
    }
  }
}

void sensorTask(void *parameter)
{

  TickType_t lastWakeTime = xTaskGetTickCount();

  for (;;)
  {
    sensors_event_t event;
    accelerometer.getEvent(&event);

    Orientation orientation = classifyOrientation(
        event.acceleration.x,
        event.acceleration.y);

    SensorData data = {
        event.acceleration.x,
        event.acceleration.y,
        event.acceleration.z,
        orientation};

    xQueueSend(queue, &data, pdMS_TO_TICKS(SAMPLE_PERIOD));

    // Wait untill next sample
    vTaskDelayUntil(&lastWakeTime, SAMPLE_PERIOD);
  }
}
