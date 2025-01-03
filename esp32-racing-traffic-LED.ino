#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <mqtt_client.h>  //ESP32 的 MQTT 庫
#include <ArduinoJson.h>

const char* versionInfo = "Version: 0.0.1";  // 編譯版本
char burnDate[] = __DATE__;                 // 編譯日期
char burnTime[] = __TIME__;                 // 編譯時間

// WiFi 設定
const char* ssid = "--replace your wifi name";
const char* password = "";

// MQTT 設定
const char* mqttServer = "";
const char* mqttUser = "";
const char* mqttPassword = "";


// 定義 LED 條參數
#define LED_PIN 5   // 連接 WS2812B 的數據引腳
#define NUM_LEDS 8  // WS2812B 的 LED 燈數量

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// 設定-全滅
void clearAllLEDs() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(0, 0, 0));  // 設為全滅
  }
  strip.show();
}

// 設定指定範圍內的顏色
void setRangeColor(int start, int end, uint32_t color) {
  for (int i = start; i <= end; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

/**
 * 讓 LED 在指定範圍內以指定顏色閃爍特定次數。
 *
 * @param start       起始範圍的 LED 編號（包含）。
 * @param end         結束範圍的 LED 編號（包含）。
 * @param count       閃爍的次數。
 * @param delayTime   每次閃爍的延遲時間（毫秒）。
 * @param color       閃爍時的顏色（使用 `strip.Color(r, g, b)` 格式設定）。
 */
void setColorBlink(int start, int end, int count, int delayTime, uint32_t color) {
  for (int i = 0; i < count; i++) {
    // 設定範圍內的 LED 為指定顏色
    setRangeColor(start, end, color);
    delay(delayTime);  // 保持顏色的時間

    // 將範圍內的 LED 設為全滅
    setRangeColor(start, end, strip.Color(0, 0, 0));
    delay(delayTime);  // 保持熄滅的時間
  }
}


// WiFi 連線
void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}


esp_mqtt_client_handle_t mqttClient;  // MQTT 客戶端
String chipId;
bool mqttIsConnected = false;  // MQTT 的連線狀態

// 儲存訂閱的主題模板
// ioT/TrafficLight/{chipId} 發狀態用
String topicsTemplate[] = {
  "/ioT/TrafficLight/{chipId}",
  "/ioT/TrafficLight/API/{chipId}/Red",
  "/ioT/TrafficLight/API/{chipId}/Yellow",
  "/ioT/TrafficLight/API/{chipId}/Green",
  "/ioT/TrafficLight/API/{chipId}/Status",
  "/ioT/TrafficLight/API/{chipId}/Start",
  "/ioT/TrafficLight/API/{chipId}/Stop"
};

const int numTopics = sizeof(topicsTemplate) / sizeof(topicsTemplate[0]);  // 主題數量
String topics[numTopics];
int subscriptionMsgId[numTopics];  // 儲存每個頻道的 msg_id

// 初始化主題列表
void initTopics() {
  uint64_t s = ESP.getEfuseMac();
  chipId = String((uint16_t)(s >> 32), HEX) + String((uint32_t)s, HEX);
  chipId.toUpperCase();

  Serial.print("ESP32 Chip ID: ");
  Serial.println(chipId);

  for (int i = 0; i < numTopics; i++) {
    topics[i] = topicsTemplate[i];
    topics[i].replace("{chipId}", chipId);
  }

  Serial.println("====== Topics ======");
  for (int i = 0; i < numTopics; i++) {
    Serial.println(topics[i]);
  }
  Serial.println("====================");
}

// 紀錄當前燈的狀態
int redStatus = 0;
int yellowStatus = 0;
int greenStatus = 0;

/**
 * 發送燈的狀態到 MQTT
 */
void sendLightStatus(String status) {
  if (!mqttIsConnected) {
    Serial.println("MQTT not connected, cannot send status.");
    return;
  }

  // 建立 JSON 文檔
  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["red"] = redStatus;
  doc["yellow"] = yellowStatus;
  doc["green"] = greenStatus;

  // 將 JSON 序列化為字串
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

  // 發送到 MQTT 主題
  esp_mqtt_client_publish(mqttClient, topics[0].c_str(), jsonBuffer, 0, 0, false);

  // 輸出到 Serial
  Serial.println("Published Light Status:");
  Serial.println(jsonBuffer);
}

/**
 * 更新燈的狀態
 */
void updateLightStatus(String key, int status) {
  if (key == "red") {
    redStatus = status;
    setRangeColor(0, 1, status ? strip.Color(255, 0, 0) : strip.Color(0, 0, 0));
  } else if (key == "yellow") {
    yellowStatus = status;
    setRangeColor(3, 4, status ? strip.Color(255, 255, 0) : strip.Color(0, 0, 0));
  } else if (key == "green") {
    greenStatus = status;
    setRangeColor(6, 7, status ? strip.Color(0, 255, 0) : strip.Color(0, 0, 0));
  } else if (key == "clear") {
    redStatus = 0;
    yellowStatus = 0;
    greenStatus = 0;
    clearAllLEDs();
  }
}

// MQTT 事件回呼函式
static void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch (event_id) {
    case MQTT_EVENT_CONNECTED:
      {
        Serial.println("MQTT Connected!");
        mqttIsConnected = true;

        for (int i = 1; i < numTopics; i++) {
          //0是自己發布狀態的位置，所以i+1，不訂閱該頻道
          esp_mqtt_client_subscribe(mqttClient, topics[i].c_str(), 0);
          subscriptionMsgId[i] = esp_mqtt_client_subscribe(mqttClient, topics[i].c_str(), 0);
          Serial.printf("Subscribing to topic: %s, msg_id=%d\n", topics[i].c_str(), subscriptionMsgId[i]);
        }
      }
      break;

    case MQTT_EVENT_DISCONNECTED:
      {
        Serial.println("MQTT Disconnected!");
        mqttIsConnected = false;
      }
      break;

    case MQTT_EVENT_DATA:
      {
        Serial.println("Message received:");
        Serial.printf("Topic: %.*s\n", event->topic_len, event->topic);
        Serial.printf("Data: %.*s\n", event->data_len, event->data);

        String receivedTopic = String(event->topic, event->topic_len);  // 提取主題
        String receivedData = String(event->data, event->data_len);     // 提取資料


        if (receivedTopic == topics[1].c_str()) {
          // "/ioT/TrafficLight/API/{chipId}/Red"
          updateLightStatus("red", receivedData.toInt());
          sendLightStatus("Red");

        } else if (receivedTopic == topics[2].c_str()) {
          //"/ioT/TrafficLight/API/{chipId}/Yellow"
          updateLightStatus("yellow", receivedData.toInt());
          sendLightStatus("Yellow");

        } else if (receivedTopic == topics[3].c_str()) {
          // "/ioT/TrafficLight/API/{chipId}/Green"
          updateLightStatus("green", receivedData.toInt());
          sendLightStatus("Green");

        } else if (receivedTopic == topics[4].c_str()) {
          // "/ioT/TrafficLight/API/{chipId}/Status"
          sendLightStatus("Status");

        } else if (receivedTopic == topics[5].c_str()) {
          // "/ioT/TrafficLight/API/{chipId}/Start"
          updateLightStatus("clear", 0);
          delay(500);
          setColorBlink(0, 1, 3, 500, strip.Color(255, 0, 0));    // 紅色
          setColorBlink(3, 4, 1, 500, strip.Color(255, 255, 0));  // 黃色
          updateLightStatus("green", 1);
          sendLightStatus("Start");
        } else if (receivedTopic == topics[6].c_str()) {
          // "/ioT/TrafficLight/API/{chipId}/Stop"
          updateLightStatus("clear", 0);
          delay(500);
          updateLightStatus("red", 1);
          sendLightStatus("Stop");
        }
      }
      break;

    case MQTT_EVENT_SUBSCRIBED:
      {
        // 檢查是哪個主題的訂閱成功
        for (int i = 0; i < numTopics; i++) {
          if (subscriptionMsgId[i] == event->msg_id) {
            Serial.printf("Subscribed to topic: %s, msg_id=%d\n", topics[i].c_str(), subscriptionMsgId[i]);
            if (i == 1) {
              // `topics[1]` 是 "/ioT/TrafficLight/API/{chipId}/Red"
              setColorBlink(0, 1, 2, 200, strip.Color(255, 0, 0));  // 紅色
            }
            if (i == 2) {
              // `topics[2]` 是 "/ioT/TrafficLight/API/{chipId}/Yellow"
              setColorBlink(3, 4, 2, 200, strip.Color(255, 255, 0));  // 黃色
            }
            if (i == 3) {
              // `topics[3]` 是 "/ioT/TrafficLight/API/{chipId}/Green"
              setColorBlink(6, 7, 2, 200, strip.Color(0, 255, 0));  // 綠色
            }
            if (i == 6) {
              // `topics[6]` 是 "/ioT/TrafficLight/API/{chipId}/Stop"
              updateLightStatus("clear", 0);
              delay(100);
              updateLightStatus("red", 1);
              sendLightStatus("connectMQTT");
            }
          }
        }
      }
      break;

    default:
      {
      }
      break;
  }
}

// MQTT 連線
void connectToMQTT() {
  esp_mqtt_client_config_t mqttConfig = {};
  mqttConfig.broker.address.uri = mqttServer;
  mqttConfig.credentials.username = mqttUser;
  mqttConfig.credentials.authentication.password = mqttPassword;

  mqttClient = esp_mqtt_client_init(&mqttConfig);
  esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
  Serial.println("Connecting to MQTT...");
  esp_mqtt_client_start(mqttClient);
}

// 重連 MQTT（30 秒檢查一次）
void reconnectMQTT() {
  static unsigned long lastCheck = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastCheck >= 30000) {
    lastCheck = currentMillis;

    if (!mqttIsConnected) {
      Serial.println("MQTT not connected, attempting to reconnect...");
      esp_err_t err = esp_mqtt_client_reconnect(mqttClient);
      if (err == ESP_OK) {
        Serial.println("MQTT reconnection initiated.");
      } else {
        Serial.printf("MQTT reconnection failed, error code: %d\n", err);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("========");
  Serial.println(versionInfo);
  Serial.print("burnDate:");
  Serial.println(burnDate);
  Serial.print("burnTime:");
  Serial.println(burnTime);
  Serial.println("========");

  initTopics();

  connectToWiFi();

  strip.begin();
  clearAllLEDs();
  setColorBlink(0, NUM_LEDS - 1, 3, 500, strip.Color(255, 255, 255));  // 全白閃爍 3 次
  clearAllLEDs();

  connectToMQTT();
}

void loop() {
  reconnectMQTT();
}
