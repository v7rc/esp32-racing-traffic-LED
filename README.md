這是應用MQTT的發布與訂閱機制，進行控制紅綠燈的機制。主要的控制板是ESP32。

---

## 使用相關
| Library           | 功能描述                    | 版本    | 下載連結 |
|-------------------|---------------------------|---------|----------|
| **Arduino-ESP32**    | ESP32             | 3.0.7   | [GitHub](https://github.com/espressif/arduino-esp32) |
| **ArduinoJson**   | JSON        | 6.21.3  | [官方網站](https://arduinojson.org/?utm_source=meta&utm_medium=library.properties) |
| **Adafruit NeoPixel** | 控制 WS2812 LED 燈條 | 1.12.3  | [GitHub](https://github.com/adafruit/Adafruit_NeoPixel) |

---
## 藍牙相關
不重新燒錄的情況下，修改環境，或測試設備使用。
在開機後三秒內按下 IO0 按鈕，當藍燈閃爍即進入藍牙模式。
以下是藍牙指令相關

| **指令**               | **用途**                           | **參數/說明**                              |
|------------------------|------------------------------------|--------------------------------------------|
| **Wi-Fi 與 MQTT 設定** |                                    |                                            |
| `CMDSSID:<Wi-Fi名稱>###` | 設定 Wi-Fi SSID                   | `<Wi-Fi名稱>` 為目標 Wi-Fi 的 SSID 名稱    |
| `CMDWIFIPW:<Wi-Fi密碼>###` | 設定 Wi-Fi 密碼                  | `<Wi-Fi密碼>` 為 Wi-Fi 的連線密碼          |
| `CMDMQTTIP:<MQTT伺服器位置>###` | 設定 MQTT 伺服器 IP 或位置   | `<MQTT伺服器位置>` 可為 IP 或 URL 地址     |
| `CMDMQTTUR:<MQTT使用者名稱>###` | 設定 MQTT 使用者名稱         | `<MQTT使用者名稱>` 為 MQTT 登錄的帳號      |
| `CMDMQTTPW:<MQTT密碼>###` | 設定 MQTT 密碼                   | `<MQTT密碼>` 為 MQTT 登錄的密碼            |
| **燈號控制**            |                                    |                                            |
| `CMDRED:<狀態>###`      | 控制紅燈                          | `<狀態>`：`0` 關閉，`1` 開啟               |
| `CMDGREEN:<狀態>###`    | 控制綠燈                          | `<狀態>`：`0` 關閉，`1` 開啟               |
| `CMDYELLOW:<狀態>###`   | 控制黃燈                          | `<狀態>`：`0` 關閉，`1` 開啟               |
| **燈號動作切換**        |                                    |                                            |
| `CMDSTART###`           | 啟動燈號動作                     | 啟動指定燈號動作                           |
| `CMDSTOP###`            | 停止燈號動作                     | 停止指定燈號動作                           |
| **系統資訊查詢**        |                                    |                                            |
| `CMDCHIPID###`          | 查詢裝置的 CHIP ID               | 返回裝置的唯一 CHIP ID                     |
| `CMDSTATUS###`          | 查詢狀態               | 返回當前狀態             |

---