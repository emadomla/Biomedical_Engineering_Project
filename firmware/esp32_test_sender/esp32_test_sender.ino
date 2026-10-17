
#include <WiFi.h>
#include <HTTPClient.h>


const char* WIFI_SSID = "ًWIFI_NAME";
const char* WIFI_PASSWORD = "WIFI_PASSWORD";


const char* SERVER_URL =
    "http://192.168.1.0:8000/api/v1/measurements";

const unsigned long SEND_INTERVAL_MS = 10000;
unsigned long lastSendTime = 0;

void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void sendTestMeasurement() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, SERVER_URL)) {
    Serial.println("Could not start HTTP request");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  
  String payload =
      "{\"device_id\":\"esp32_001\","
      "\"heart_rate_bpm\":90,"
      "\"spo2_percent\":98.0}";

  int statusCode = http.POST(payload);

  Serial.print("HTTP status: ");
  Serial.println(statusCode);

  if (statusCode > 0) {
    Serial.println("Server response:");
    Serial.println(http.getString());
  } else {
    Serial.println(HTTPClient::errorToString(statusCode));
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  connectToWiFi();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = now;
    sendTestMeasurement();
  }

  delay(10);
}
