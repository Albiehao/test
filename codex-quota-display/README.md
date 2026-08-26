# Codex Quota Display

ESP8266 + SSD1306 128x64 OLED，用来显示 Codex 5 小时额度、周额度、恢复倒计时，同时显示本地温湿度。

## 接线

当前实物接线：

| 模块 | 引脚 | ESP8266 |
|---|---|---|
| OLED | GND | GND |
| OLED | VCC | 3V3 |
| OLED | SCL | D4 / GPIO2 |
| OLED | SDA | D3 / GPIO0 |
| DHT11/DHT22 | DATA | D6 / GPIO12 |
| DHT11/DHT22 | VCC | 3V3 |
| DHT11/DHT22 | GND | GND |

OLED 使用：

```cpp
Wire.begin(D3, D4); // SDA, SCL
```

温湿度传感器默认按 DHT11：

```cpp
constexpr uint8_t DHT_PIN = D6;
#define DHT_TYPE DHT11
```

如果实际是 DHT22，只需改成：

```cpp
#define DHT_TYPE DHT22
```

如果你用的是裸 DHT11/DHT22（不是带小板模块），DATA 到 3V3 建议加约 10kΩ 上拉电阻。

> D3(GPIO0) 和 D4(GPIO2) 都参与 ESP8266 启动配置。如果出现插着 OLED 无法启动、拔掉后正常启动，再考虑换到 D2/D1。当前能正常启动就无需改线。

## Arduino 依赖

安装：

- Adafruit GFX Library
- Adafruit SSD1306
- ArduinoJson 7.x
- DHT sensor library by Adafruit
- Adafruit Unified Sensor

ESP8266WiFi 与 ESP8266HTTPClient 由 ESP8266 Arduino Core 提供。

## ESP8266 配置

编辑 `esp8266/codex_quota_display.ino` 顶部：

```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* STATUS_URL = "http://192.168.1.100:8080/codex/status";
const char* DEVICE_TOKEN = "CHANGE_ME";
```

OLED 顶部持续显示：

```text
T:25.1C H:63%
```

下面显示 5H、WK、READY/LIMITED 和恢复倒计时。

## Codex 网关服务

服务端代码位于：

```text
server/app.py
```

它在已经登录 Codex 的电脑、Mac、Linux 主机或服务器上启动 `codex app-server`，通过 `account/rateLimits/read` 读取额度，再向 ESP8266 暴露一个很小的 HTTP API。

### 1. 确认 Codex CLI 已登录

```bash
codex
```

能正常使用后退出即可。

### 2. 安装 Python 依赖

```bash
cd codex-quota-display/server
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Windows PowerShell 激活虚拟环境：

```powershell
.venv\Scripts\Activate.ps1
```

### 3. 启动服务

macOS/Linux：

```bash
export DEVICE_TOKEN=CHANGE_ME
uvicorn app:app --host 0.0.0.0 --port 8080
```

Windows PowerShell：

```powershell
$env:DEVICE_TOKEN="CHANGE_ME"
uvicorn app:app --host 0.0.0.0 --port 8080
```

如果 `codex` 不在 PATH，可以指定完整路径：

```bash
export CODEX_BIN=/path/to/codex
```

### 4. 测试

```bash
curl -H "X-Device-Token: CHANGE_ME" http://127.0.0.1:8080/codex/status
```

返回格式：

```json
{
  "codex_available": true,
  "five_hour_remaining": 75,
  "weekly_remaining": 82,
  "five_hour_reset_seconds": 3214,
  "plan_type": "plus"
}
```

ESP8266 每分钟从网关刷新一次额度，本地每秒更新恢复倒计时；DHT 温湿度约每 2.5 秒读取一次。

## 安全

不要把 ChatGPT / Codex 登录凭据或 OpenAI Token 放到 ESP8266。凭据只留在运行 Codex CLI 的主机上，ESP8266 只访问这个最小化网关接口。
