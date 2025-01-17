#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <mqtt_client.h>  //ESP32 的 MQTT 庫
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>
#include <Preferences.h>

const char *versionInfo = "Version: 0.0.4";  // 編譯版本
char burnDate[] = __DATE__;                  // 編譯日期
char burnTime[] = __TIME__;                  // 編譯時間

#define BUTTON_PIN 0  // 預設GPIO 引腳，用來切換藍牙模式

// BLE UUID 定義

// #define BLE_UUID_SERVICE "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
// #define BLE_UUID_WRITE "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
// #define BLE_UUID_NOTIFY "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

#define BLE_UUID_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_UUID_WRITE "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_UUID_NOTIFY "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *writeCharacteristic;
BLECharacteristic *notifyCharacteristic;

// WiFi 設定
String ssid;  //your wifi name
String password;

// MQTT 設定
String mqttServer;
String mqttUser;
String mqttPassword;


Preferences preferences;

// 儲存資料到 Preferences
void saveToPreferences(String key, String value) {
  preferences.begin("settings", false);
  preferences.putString(key.c_str(), value);
  preferences.end();
  Serial.println("Saved " + key + " : " + value);
}

// 從 Preferences 中讀取資料
String loadFromPreferences(String key, String defaultValue) {
  preferences.begin("settings", true);
  String value = preferences.getString(key.c_str(), defaultValue);
  preferences.end();
  return value;
}

//讀取環境參數
void loadPreferences() {
  ssid = loadFromPreferences("SSID", "--replace your wifi name");
  password = loadFromPreferences("WIFIPW", "");
  mqttServer = loadFromPreferences("MQTTIP", "");
  mqttUser = loadFromPreferences("MQTTUR", "");
  mqttPassword = loadFromPreferences("MQTTPW", "");

  // 顯示設定到 Serial
  Serial.println("Loaded Preferences:");
  Serial.println("SSID: " + ssid);
  Serial.println("Password: " + password);
  Serial.println("MQTT Server: " + mqttServer);
  Serial.println("MQTT User: " + mqttUser);
  Serial.println("MQTT Password: " + mqttPassword);
}

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
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startTime = millis();
  const unsigned long timeout = 30000;  // 設定超時 30 秒

  // 嘗試連線
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > timeout) {
      Serial.println("\nWiFi connection timed out!");
      setRangeColor(0, NUM_LEDS - 1, strip.Color(255, 0, 0));  // 紅色
      return;                                                  // 退出連線
    }
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    // 連線成功
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
}



esp_mqtt_client_handle_t mqttClient;  // MQTT 客戶端
bool mqttIsConnected = false;         // MQTT 的連線狀態
bool isWifiMode = false;
bool isBluetoothMode = false;
bool isBluetoothConnected = false;

String chipId;
//初始化chipId參數
void initChipId() {
  uint64_t s = ESP.getEfuseMac();
  chipId = String((uint16_t)(s >> 32), HEX) + String((uint32_t)s, HEX);
  chipId.toUpperCase();

  Serial.print("ESP32 Chip ID: ");
  Serial.println(chipId);
}


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

// 發送燈的狀態
void sendLightStatus(String status) {

  // 建立 JSON 文檔
  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["red"] = redStatus;
  doc["yellow"] = yellowStatus;
  doc["green"] = greenStatus;

  // 將 JSON 序列化為字串
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

  if (isWifiMode) {
    if (!mqttIsConnected) {
      Serial.println("MQTT not connected, cannot send status.");
      return;
    }
    // 發送到 MQTT 主題
    esp_mqtt_client_publish(mqttClient, topics[0].c_str(), jsonBuffer, 0, 0, false);
    // 輸出到 Serial
    Serial.println("WifiMode Published Light Status:");
    Serial.println(jsonBuffer);
  }
  if (isBluetoothMode) {
    if (!isBluetoothConnected) {
      Serial.println("Bluetooth not connected, cannot send status.");
      return;
    }

    notifyMessage(jsonBuffer);
    // 輸出到 Serial
    Serial.println("BluetoothMode Published Light Status:");
    Serial.println(jsonBuffer);
  }
}

// 更新燈的狀態
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
static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch (event_id) {
    case MQTT_EVENT_CONNECTED:
      {
        Serial.println("MQTT Connected!");
        mqttIsConnected = true;

        for (int i = 1; i < numTopics; i++) {
          //0是自己發布狀態的位置，所以i+1，不訂閱該頻道
          subscriptionMsgId[i] = esp_mqtt_client_subscribe(mqttClient, topics[i].c_str(), 0);
          Serial.printf("Subscribing to topic: %s, msg_id=%d\n", topics[i].c_str(), subscriptionMsgId[i]);
        }
      }
      break;

    case MQTT_EVENT_DISCONNECTED:
      {
        Serial.println("MQTT Disconnected!");
        mqttIsConnected = false;
        setRangeColor(0, NUM_LEDS - 1, strip.Color(255, 0, 0));  // 紅色
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

bool isInitMQTT = false;
// MQTT 連線
void connectToMQTT() {
  isInitMQTT = true;
  esp_mqtt_client_config_t mqttConfig = {};
  mqttConfig.broker.address.uri = mqttServer.c_str();
  mqttConfig.credentials.username = mqttUser.c_str();
  mqttConfig.credentials.authentication.password = mqttPassword.c_str();

  mqttClient = esp_mqtt_client_init(&mqttConfig);
  esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
  Serial.println("Connecting to MQTT...");
  esp_mqtt_client_start(mqttClient);
}

// 重連 MQTT（30 秒檢查一次）
void reconnectMQTT() {
  if (isInitMQTT) {
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
}

// 檢測按鈕狀態
bool isButtonPressed() {
  return digitalRead(BUTTON_PIN) == LOW;  // 按下按鈕時，GPIO 為 LOW
}


/**
 * 從指定格式的字串中提取有效值並回傳
 * 
 * @param rawData         原始輸入字串 (如 "CMDSSID###")
 * @param dataPrefix      資料前綴標記 (如 "CMD")
 * @param dataSuffix      資料後綴標記 (如 "###")
 * @return 提取的有效值，若格式不正確則回傳空字串
 */
String extractCommandString(String rawData, String dataPrefix, String dataSuffix) {
  // 檢查是否以指定前綴和後綴包圍
  if (rawData.startsWith(dataPrefix) && rawData.endsWith(dataSuffix)) {
    int valueStart = dataPrefix.length();                   // 有效值的起始索引
    int valueEnd = rawData.length() - dataSuffix.length();  // 有效值的結束索引
    return rawData.substring(valueStart, valueEnd);         // 提取有效值
  }

  // 格式不正確，回傳空字串
  return "";
}



class writeBLECallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    Serial.println("Received data over BLE:");
    Serial.println(value);

    if (value.length() > 0) {
      String tempString = extractCommandString(value, "CMD", "###");
      if (tempString.length() > 0) {
        //原生沒有字串切割，改用提取

        int separatorIndex = tempString.indexOf(':');
        String command = "";
        String value = "";

        if (separatorIndex > 0) {
          command = tempString.substring(0, separatorIndex);  // 提取命令
          value = tempString.substring(separatorIndex + 1);   // 提取參數
        } else {
          command = tempString;
        }

        if (command == "SSID" || command == "WIFIPW" || command == "MQTTIP" || command == "MQTTUR" || command == "MQTTPW") {
          saveToPreferences(command, value);  // 保存參數到 Preferences
        } else if (command == "RED") {
          // "/ioT/TrafficLight/API/{chipId}/Red"
          updateLightStatus("red", value.toInt());
          sendLightStatus("Red");

        } else if (command == "YELLOW") {
          //"/ioT/TrafficLight/API/{chipId}/Yellow"
          updateLightStatus("yellow", value.toInt());
          sendLightStatus("Yellow");

        } else if (command == "GREEN") {
          // "/ioT/TrafficLight/API/{chipId}/Green"
          updateLightStatus("green", value.toInt());
          sendLightStatus("Green");

        } else if (command == "STATUS") {
          // "/ioT/TrafficLight/API/{chipId}/Status"
          sendLightStatus("Status");

        } else if (command == "START") {
          // "/ioT/TrafficLight/API/{chipId}/Start"
          updateLightStatus("clear", 0);
          delay(500);
          setColorBlink(0, 1, 3, 500, strip.Color(255, 0, 0));    // 紅色
          setColorBlink(3, 4, 1, 500, strip.Color(255, 255, 0));  // 黃色
          updateLightStatus("green", 1);
          sendLightStatus("Start");
        } else if (command == "STOP") {
          // "/ioT/TrafficLight/API/{chipId}/Stop"
          updateLightStatus("clear", 0);
          delay(500);
          updateLightStatus("red", 1);
          sendLightStatus("Stop");
        }
        else if (command == "CHIPID") {
          notifyMessage(chipId);
        }
      }
    }
  }
};


class serverBLECallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    Serial.println("Device connected.");
    isBluetoothConnected = true;
    setRangeColor(0, NUM_LEDS - 1, strip.Color(41, 41, 255));  // 藍色
    notifyMessage("Device connected.");
  }

  void onDisconnect(BLEServer *pServer) override {
    Serial.println("Device disconnected.");
    isBluetoothConnected = false;
    clearAllLEDs();
    pServer->startAdvertising();
  }
};

void notifyMessage(String message) {
  if (isBluetoothConnected && notifyCharacteristic != nullptr) {
    notifyCharacteristic->setValue(message.c_str());  // 設置通知的內容
    notifyCharacteristic->notify();                   // 發送通知
    Serial.println("Notification sent over BLE: " + message);
  }
}


void startBluetoothMode() {
  Serial.println("Button detected! Entering Bluetooth mode...");
  isBluetoothMode = true;

  // 初始化 BLE
  String bleDeviceName = "ESP32-BLE-" + chipId;
  BLEDevice::init(bleDeviceName.c_str());


  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new serverBLECallbacks());  // 設置連線監聽回調

  BLEService *pService = pServer->createService(BLE_UUID_SERVICE);

  writeCharacteristic = pService->createCharacteristic(
    BLE_UUID_WRITE,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);

  writeCharacteristic->setCallbacks(new writeBLECallbacks());

  notifyCharacteristic = pService->createCharacteristic(
    BLE_UUID_NOTIFY,
    BLECharacteristic::PROPERTY_NOTIFY);

  // 啟動服務並開始廣播
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_UUID_SERVICE);  // 將服務 UUID 添加到廣播數據
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);  // functions that help with iPhone connections issue
  pAdvertising->start();

  Serial.println("Bluetooth mode is active, waiting for connections...");
}

void startWifiMode() {
  isWifiMode = true;

  loadPreferences();
  if (ssid.length() > 0) {
    connectToWiFi();
    if (WiFi.status() == WL_CONNECTED && mqttServer.length() > 0) {
      initTopics();
      connectToMQTT();
    } else {
      setRangeColor(0, NUM_LEDS - 1, strip.Color(255, 0, 0));  // 紅色
    }
  } else {
    setRangeColor(0, NUM_LEDS - 1, strip.Color(255, 0, 0));  // 紅色
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

  initChipId();
  strip.begin();
  clearAllLEDs();

  // 設置按鈕引腳
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // 使用內部上拉電阻

  delay(3000);  //如果不用GPIO 引腳可以刪除，開機時按著按鈕就行

  // 開機檢測按鈕
  if (isButtonPressed()) {
    setColorBlink(0, NUM_LEDS - 1, 3, 500, strip.Color(41, 41, 255));  // 全藍閃爍 3 次
    startBluetoothMode();
  } else {
    setColorBlink(0, NUM_LEDS - 1, 3, 500, strip.Color(255, 255, 255));  // 全白閃爍 3 次
    startWifiMode();
  }
}

void loop() {
  reconnectMQTT();
}
