#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <DHT.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Your own trusted gateway. Do not put ChatGPT/OpenAI credentials on ESP8266.
const char* STATUS_URL = "http://192.168.1.100:8080/codex/status";
const char* DEVICE_TOKEN = "CHANGE_ME";

constexpr uint32_t REFRESH_INTERVAL_MS = 60UL * 1000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr uint32_t HTTP_TIMEOUT_MS = 5000UL;
constexpr uint32_t SENSOR_INTERVAL_MS = 2500UL;

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int OLED_RESET = -1;
constexpr uint8_t OLED_ADDRESS = 0x3C;

// OLED wiring:
// SDA -> D3 / GPIO0
// SCL -> D4 / GPIO2
constexpr uint8_t OLED_SDA = D3;
constexpr uint8_t OLED_SCL = D4;

// Temperature/humidity sensor wiring:
// DATA -> D6 / GPIO12
constexpr uint8_t DHT_PIN = D6;
#define DHT_TYPE DHT11
// If your sensor is DHT22, change the line above to: #define DHT_TYPE DHT22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHT dht(DHT_PIN, DHT_TYPE);

struct CodexStatus {
  bool valid = false;
  bool available = false;
  int fiveHourRemaining = 0;
  int weeklyRemaining = 0;
  uint32_t resetSeconds = 0;
};

struct EnvironmentData {
  bool valid = false;
  float temperature = 0.0f;
  float humidity = 0.0f;
};

CodexStatus statusData;
EnvironmentData envData;
uint32_t lastRefreshAt = 0;
uint32_t lastWifiRetryAt = 0;
uint32_t lastSensorAt = 0;

void drawProgressBar(int x, int y, int width, int height, int percent) {
  percent = constrain(percent, 0, 100);
  display.drawRect(x, y, width, height, SSD1306_WHITE);
  int innerWidth = width - 2;
  int fillWidth = (innerWidth * percent) / 100;
  if (fillWidth > 0) {
    display.fillRect(x + 1, y + 1, fillWidth, height - 2, SSD1306_WHITE);
  }
}

String formatDuration(uint32_t seconds) {
  uint32_t hours = seconds / 3600;
  uint32_t minutes = (seconds % 3600) / 60;
  uint32_t secs = seconds % 60;
  char buf[16];
  if (hours > 0) {
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)secs);
  } else {
    snprintf(buf, sizeof(buf), "%02lu:%02lu",
             (unsigned long)minutes,
             (unsigned long)secs);
  }
  return String(buf);
}

void readEnvironment() {
  uint32_t now = millis();
  if (now - lastSensorAt < SENSOR_INTERVAL_MS) return;
  lastSensorAt = now;

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (!isnan(humidity) && !isnan(temperature)) {
    envData.humidity = humidity;
    envData.temperature = temperature;
    envData.valid = true;
  }
}

void render() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Top line: local sensor data always stays visible.
  display.setCursor(0, 0);
  if (envData.valid) {
    display.print("T:");
    display.print(envData.temperature, 1);
    display.print("C H:");
    display.print(envData.humidity, 0);
    display.print("%");
  } else {
    display.print("T:--.-C H:--%");
  }

  if (WiFi.status() != WL_CONNECTED) {
    display.setCursor(0, 18);
    display.print("WiFi disconnected");
    display.setCursor(0, 32);
    display.print("Reconnecting...");
    display.display();
    return;
  }

  if (!statusData.valid) {
    display.setCursor(0, 18);
    display.print("Waiting Codex...");
    display.display();
    return;
  }

  display.setCursor(0, 12);
  display.print("5H ");
  display.print(statusData.fiveHourRemaining);
  display.print("%");
  drawProgressBar(42, 11, 84, 9, statusData.fiveHourRemaining);

  display.setCursor(0, 26);
  display.print("WK ");
  display.print(statusData.weeklyRemaining);
  display.print("%");
  drawProgressBar(42, 25, 84, 9, statusData.weeklyRemaining);

  display.setCursor(0, 41);
  display.print(statusData.available ? "READY" : "LIMITED");

  display.setCursor(0, 53);
  display.print("Reset ");
  display.print(formatDuration(statusData.resetSeconds));

  display.display();
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - lastWifiRetryAt < WIFI_RETRY_INTERVAL_MS) return;
  lastWifiRetryAt = now;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool fetchStatus() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, STATUS_URL)) return false;

  http.addHeader("Accept", "application/json");
  if (strlen(DEVICE_TOKEN) > 0) {
    http.addHeader("X-Device-Token", DEVICE_TOKEN);
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.printf("JSON error: %s\n", error.c_str());
    return false;
  }

  statusData.available = doc["codex_available"] | false;
  statusData.fiveHourRemaining = constrain((int)(doc["five_hour_remaining"] | 0), 0, 100);
  statusData.weeklyRemaining = constrain((int)(doc["weekly_remaining"] | 0), 0, 100);
  statusData.resetSeconds = doc["five_hour_reset_seconds"] | 0UL;
  statusData.valid = true;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Wire.begin(OLED_SDA, OLED_SCL); // Wire.begin(SDA, SCL)
  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("SSD1306 init failed");
    while (true) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("CODEX MONITOR");
  display.println("Booting...");
  display.display();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  connectWiFi();
  readEnvironment();

  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED &&
      (!statusData.valid || now - lastRefreshAt >= REFRESH_INTERVAL_MS)) {
    lastRefreshAt = now;
    fetchStatus();
  }

  static uint32_t lastSecond = millis();
  if (millis() - lastSecond >= 1000) {
    lastSecond += 1000;
    if (statusData.valid && statusData.resetSeconds > 0) {
      statusData.resetSeconds--;
    }
    render();
  }

  delay(10);
}
