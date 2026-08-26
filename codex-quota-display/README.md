# Codex Quota Display

ESP8266 + SSD1306 128x64 OLED，用来显示 Codex 5 小时额度、周额度和恢复倒计时。

## 接线

按当前实物接线：

| OLED | ESP8266 |
|---|---|
| GND | GND |
| VCC | 3V3 |
| SCL | D4 / GPIO2 |
| SDA | D3 / GPIO0 |

代码中使用：

```cpp
Wire.begin(D3, D4); // SDA, SCL
```

> D3(GPIO0) 和 D4(GPIO2) 都参与 ESP8266 启动配置。如果出现插着 OLED 无法启动、拔掉后正常启动，再考虑换到 D2/D1。当前能正常启动就无需改线。

## Arduino 依赖

安装：

- Adafruit GFX Library
- Adafruit SSD1306
- ArduinoJson 7.x

ESP8266WiFi 与 ESP8266HTTPClient 由 ESP8266 Arduino Core 提供。

## 配置

编辑 `esp8266/codex_quota_display.ino` 顶部：

```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* STATUS_URL = "http://192.168.1.100:8080/codex/status";
const char* DEVICE_TOKEN = "CHANGE_ME";
```

## 中间层接口

ESP8266 不直接保存 OpenAI / ChatGPT / Codex 凭据，只访问自己的中间层：

```text
GET /codex/status
```

可选请求头：

```text
X-Device-Token: CHANGE_ME
```

期望返回：

```json
{
  "codex_available": true,
  "five_hour_remaining": 75,
  "weekly_remaining": 82,
  "five_hour_reset_seconds": 3214
}
```

OLED 每分钟从服务端刷新一次额度数据，本地每秒更新恢复倒计时。
