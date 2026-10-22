#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_sleep.h>

// ==============================================================================
// QUICK-ACCESS CONFIGURATION (Easily change your pins, timers & security here)
// ==============================================================================

// 1. Security & Authentication Configuration
// The secret API Key that matches your FastAPI backend (.env / ESP32_API_KEY)
#define ESP32_API_KEY           "bme_secret_esp32_key_2026"

// 0 = TLS encrypted connection with setInsecure() for local IPs (e.g. 192.168.x.x)
// 1 = Strict CA Certificate validation (for production domain with CA)
#define USE_STRICT_SSL_CERT     0

// Optional: Root CA Certificate for strict validation (used when USE_STRICT_SSL_CERT = 1)
const char* SERVER_ROOT_CA = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdKedrN48TzANBgkqhkiG9w0BAQsFADBh\n" \
"...\n" \
"-----END CERTIFICATE-----\n";

// 2. Hardware Wakeup Pin
// Change WAKEUP_SENSOR_PIN to match your sensor's interrupt or lead-off detection pin.
#define WAKEUP_SENSOR_PIN       4       // Default RTC GPIO 4 (change when hardware is ready)
#define WAKEUP_PIN_LEVEL        0       // 0 = Wake on LOW (sensor pulled to GND / INT asserted)
                                        // 1 = Wake on HIGH

// 3. Inactivity Timeout Configuration
// Set TEST_FAST_TIMEOUT to 1 during testing (30s) or 0 for production (2 minutes).
#define TEST_FAST_TIMEOUT       0

#if TEST_FAST_TIMEOUT
  #define INACTIVITY_TIMEOUT_MS 30000UL   // 30 seconds for quick testing
#else
  #define INACTIVITY_TIMEOUT_MS 120000UL  // 2 minutes (120,000 ms) default
#endif

#define SEND_INTERVAL_MS        10000UL   // Send measurement interval (10 seconds)

// 4. Network Credentials & Endpoint
const char* WIFI_SSID     = "WIFI_NAME";
const char* WIFI_PASSWORD = "WIFI_PASSWORD";
// Secure HTTPS Endpoint
const char* SERVER_URL    = "https://192.168.1.0:8000/api/v1/measurements";
const char* DEVICE_ID     = "esp32_001";

// ==============================================================================
// State Variables
// ==============================================================================
unsigned long lastSendTime = 0;
unsigned long lastSensorActiveTime = 0;
bool sensorAttached = true;              // Simulated sensor attachment state
unsigned long lastCountdownLog = 0;

// ==============================================================================
// Diagnostics: Print Wakeup Cause
// ==============================================================================
void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  Serial.println("\n==========================================");
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.printf("[WAKEUP] Woke up from Deep Sleep by SENSOR signal on RTC GPIO %d!\n", WAKEUP_SENSOR_PIN);
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("[WAKEUP] Woke up from Deep Sleep by BOOT button (GPIO 0)!");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("[WAKEUP] Woke up by Timer!");
      break;
    default:
      Serial.printf("[STARTUP] Power-on / Normal reboot (Cause code: %d)\n", wakeup_reason);
      break;
  }
  Serial.println("==========================================\n");
}

// ==============================================================================
// Wi-Fi Connection
// ==============================================================================
void connectToWiFi() {
  Serial.print("[WIFI] Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected!");
    Serial.print("[WIFI] ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] Could not connect to Wi-Fi (offline mode).");
  }
}

// ==============================================================================
// Secure HTTPS Data Transmission with API Key Authentication
// ==============================================================================
void sendMeasurement(int heartRate, float spo2, const char* statusNote) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTPS] Wi-Fi disconnected, skipping send.");
    return;
  }

  // 1. Initialize Secure TLS Client
  WiFiClientSecure client;

  if (USE_STRICT_SSL_CERT) {
    client.setCACert(SERVER_ROOT_CA);
    Serial.println("[TLS] Configured with strict CA root certificate.");
  } else {
    // Encrypts all traffic with TLS over local IP without requiring local CA chain
    client.setInsecure();
  }

  // 2. Begin HTTPS Request
  HTTPClient https;
  if (!https.begin(client, SERVER_URL)) {
    Serial.println("[HTTPS] Could not begin secure connection.");
    return;
  }

  // 3. Set Content-Type and Secret API Key Header
  https.addHeader("Content-Type", "application/json");
  https.addHeader("X-API-Key", ESP32_API_KEY);

  // Format valid JSON payload compatible with FastAPI backend
  String payload = "{\"device_id\":\"" + String(DEVICE_ID) + "\","
                 + "\"heart_rate_bpm\":" + String(heartRate) + ","
                 + "\"spo2_percent\":" + String(spo2, 1) + "}";

  Serial.printf("[HTTPS] Sending encrypted data (%s): %s\n", statusNote, payload.c_str());
  int statusCode = https.POST(payload);

  Serial.printf("[HTTPS] Server Response Status: %d\n", statusCode);
  if (statusCode == 200) {
    Serial.print("[HTTPS] Response Verified: ");
    Serial.println(https.getString());
  } else if (statusCode == 401) {
    Serial.println("[AUTH ERROR] 401 Unauthorized! Check your ESP32_API_KEY in firmware and backend .env.");
  } else if (statusCode > 0) {
    Serial.printf("[HTTPS] Response (%d): %s\n", statusCode, https.getString().c_str());
  } else {
    Serial.printf("[HTTPS] Connection error: %s\n", HTTPClient::errorToString(statusCode).c_str());
  }

  https.end();
}

// ==============================================================================
// Enter Deep Sleep with Graceful Teardown
// ==============================================================================
void enterDeepSleep() {
  Serial.println("\n==========================================");
  Serial.printf("[SLEEP] No sensor activity for %lu seconds.\n", INACTIVITY_TIMEOUT_MS / 1000);
  Serial.println("[SLEEP] Performing graceful shutdown...");

  // 1. Notify Backend of Disconnect / Sleep (0 BPM / 0% SpO2 indicates offline)
  sendMeasurement(0, 0.0, "DEEP_SLEEP_SHUTDOWN");

  // 2. Shut Down Peripherals & Wi-Fi
  Serial.println("[SLEEP] Disconnecting Wi-Fi to save power...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  // 3. Configure Wakeup Sources
  // A. Primary: Sensor interrupt pin (auto-wake when sensor attaches)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_SENSOR_PIN, WAKEUP_PIN_LEVEL);

  // B. Secondary Fallback: Dev board BOOT button (GPIO 0) for manual wake-up
  if (WAKEUP_SENSOR_PIN != 0) {
    esp_sleep_enable_ext1_wakeup(1ULL << 0, ESP_EXT1_WAKEUP_ALL_LOW);
  }

  Serial.printf("[SLEEP] Configured automatic wake-up on GPIO %d (active %s).\n",
                WAKEUP_SENSOR_PIN, WAKEUP_PIN_LEVEL == 0 ? "LOW" : "HIGH");
  Serial.println("[SLEEP] ESP32 entering Deep Sleep NOW (ZZZ)...");
  Serial.println("==========================================\n");
  Serial.flush();

  // 4. Sleep
  esp_deep_sleep_start();
}

// ==============================================================================
// Simulation Commands via Serial Monitor
// ==============================================================================
void handleSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = (char)Serial.read();
    if (cmd == 'd' || cmd == 'D') {
      sensorAttached = false;
      Serial.println("\n[SIMULATION] Sensor DETACHED (Patient removed sensor). Inactivity timer running!");
    } else if (cmd == 'c' || cmd == 'C') {
      sensorAttached = true;
      lastSensorActiveTime = millis();
      Serial.println("\n[SIMULATION] Sensor ATTACHED (Signal detected). Inactivity timer reset!");
    } else if (cmd == 's' || cmd == 'S') {
      Serial.println("\n[SIMULATION] Immediate sleep requested.");
      enterDeepSleep();
    }
  }
}

// ==============================================================================
// Setup & Loop
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Print startup diagnostics
  printWakeupReason();

  Serial.println("--------------------------------------------------");
  Serial.println("SECURE BIOMONITOR ESP32 TELEMETRY (HTTPS + API-KEY)");
  Serial.printf("Endpoint:           %s\n", SERVER_URL);
  Serial.printf("API Key Configured: %s\n", (strlen(ESP32_API_KEY) > 0 ? "YES" : "NO"));
  Serial.printf("Deep Sleep Timeout: %lu seconds\n", INACTIVITY_TIMEOUT_MS / 1000);
  Serial.printf("Sensor Wakeup Pin:  GPIO %d\n", WAKEUP_SENSOR_PIN);
  Serial.println("Simulation Controls (Type in Serial Monitor):");
  Serial.println("  'd' -> Simulate sensor detached (starts countdown)");
  Serial.println("  'c' -> Simulate sensor attached (resets countdown)");
  Serial.println("  's' -> Enter deep sleep immediately");
  Serial.println("--------------------------------------------------");

  connectToWiFi();

  // Configure wakeup pin as input with pull-up if active LOW
  if (WAKEUP_PIN_LEVEL == 0) {
    pinMode(WAKEUP_SENSOR_PIN, INPUT_PULLUP);
  } else {
    pinMode(WAKEUP_SENSOR_PIN, INPUT_PULLDOWN);
  }

  lastSensorActiveTime = millis();
}

void loop() {
  unsigned long now = millis();

  // Check for simulation commands
  handleSerialCommands();

  // Periodic sensor telemetry transmission when sensor is active
  if (sensorAttached) {
    lastSensorActiveTime = now; // keep resetting timer while sensor is active

    if (now - lastSendTime >= SEND_INTERVAL_MS) {
      lastSendTime = now;
      // Simulated normal vital signs
      sendMeasurement(75, 98.5, "ACTIVE_VITAL_SIGNS");
    }
  } else {
    // Sensor is inactive/detached -> countdown to deep sleep
    unsigned long idleElapsed = now - lastSensorActiveTime;

    // Log countdown every 10 seconds
    if (now - lastCountdownLog >= 10000UL) {
      lastCountdownLog = now;
      unsigned long secondsLeft = (INACTIVITY_TIMEOUT_MS > idleElapsed) 
                                ? (INACTIVITY_TIMEOUT_MS - idleElapsed) / 1000 
                                : 0;
      Serial.printf("[INACTIVITY] Sensor not detected. Sleeping in %lu seconds...\n", secondsLeft);
    }

    if (idleElapsed >= INACTIVITY_TIMEOUT_MS) {
      enterDeepSleep();
    }
  }

  delay(10);
}
