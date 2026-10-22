/*
 * =====================================================================================
 *  PROJECT: Biomedical Patient Telemetry - ESP32 Firmware
 *  FEATURE: Secure HTTPS Telemetry + 2-Minute Inactivity Deep Sleep & Auto Wake-Up
 * =====================================================================================
 *
 *  👋 HELLO TEAM / COLLABORATOR!
 *  This firmware is built to send biomedical vital signs to our FastAPI backend safely.
 *  Here is a quick summary of what this code does so you don't have to guess:
 *
 *  1. SECURE HTTPS & AUTHENTICATION:
 *     - Encrypts all sensor data in transit using TLS (WiFiClientSecure).
 *     - Authenticates every transmission using a secret 'X-API-Key' header.
 *     - By default, it uses 'setInsecure()' so we can test HTTPS on local IP addresses 
 *       (like 192.168.x.x) without having to buy or configure a public SSL domain.
 *
 *  2. AUTOMATIC 2-MINUTE DEEP SLEEP:
 *     - When sensors are detached (or inactive) for 2 minutes (120 seconds), the ESP32
 *       shuts down Wi-Fi, radio, and CPU into Deep Sleep to preserve battery life (~10µA).
 *     - Before sleeping, it sends a final graceful disconnect message to the backend.
 *
 *  3. AUTOMATIC HARDWARE WAKE-UP:
 *     - Wakes up automatically when the sensor line goes LOW (WAKEUP_SENSOR_PIN).
 *     - Also wakes up if you press the built-in 'BOOT' button (GPIO 0) on the dev board.
 *     - Automatically diagnoses and logs the exact wake-up cause on boot.
 *
 *  4. NO HARDWARE YET? (BUILT-IN SERIAL SIMULATOR):
 *     - Open the Serial Monitor (115200 baud) and type:
 *       • 'd' -> Simulates sensor detached (starts the 2-minute countdown).
 *       • 'c' -> Simulates sensor attached (resets the countdown and sends data).
 *       • 's' -> Forces the ESP32 to enter deep sleep immediately.
 * =====================================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_sleep.h>

// ==============================================================================
// 🛠️ CONFIGURATION SECTION (Change your settings here!)
// ==============================================================================

// ------------------------------------------------------------------------------
// 1. Security & Authentication Settings
// ------------------------------------------------------------------------------
// This secret key MUST match the 'ESP32_API_KEY' in the backend '.env' file.
// If the key is wrong or missing, the server will reject requests with '401 Unauthorized'.
#define ESP32_API_KEY           "bme_secret_esp32_key_2026"

// SSL/TLS Certificate Mode:
// - Set to 0 (Default): Uses client.setInsecure(). This provides full TLS encryption
//   for local development (e.g. 192.168.x.x) without needing a public Certificate Authority.
// - Set to 1: Validates against 'SERVER_ROOT_CA' below (use when deployed to a real public domain).
#define USE_STRICT_SSL_CERT     0

// Optional: Root Certificate Authority (CA) certificate used when USE_STRICT_SSL_CERT = 1.
const char* SERVER_ROOT_CA = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdKedrN48TzANBgkqhkiG9w0BAQsFADBh\n" \
"...\n" \
"-----END CERTIFICATE-----\n";

// ------------------------------------------------------------------------------
// 2. Hardware Wake-Up Pin Settings
// ------------------------------------------------------------------------------
// When the real sensor is attached (e.g. MAX30102 INT pin or AD8232 leads-off pin),
// this RTC GPIO will trigger and wake the ESP32 from deep sleep instantly.
// NOTE: Must be an RTC-capable GPIO (GPIO 0, 2, 4, 12-15, 25-27, 32-36, 39).
#define WAKEUP_SENSOR_PIN       4       // RTC GPIO 4 (change to whichever pin your sensor INT is wired to)
#define WAKEUP_PIN_LEVEL        0       // 0 = Wake on LOW (sensor line pulled to GND when active)
                                        // 1 = Wake on HIGH

// ------------------------------------------------------------------------------
// 3. Inactivity & Deep Sleep Timers
// ------------------------------------------------------------------------------
// Inactivity timeout: How long to wait with NO sensor activity before sleeping.
// Set TEST_FAST_TIMEOUT to 1 if you want to test in 30 seconds instead of waiting 2 minutes!
#define TEST_FAST_TIMEOUT       0

#if TEST_FAST_TIMEOUT
  #define INACTIVITY_TIMEOUT_MS 30000UL   // 30 seconds (quick test mode)
#else
  #define INACTIVITY_TIMEOUT_MS 120000UL  // 120 seconds (2 minutes production default)
#endif

// How often (in milliseconds) the ESP32 sends telemetry to the server while active:
#define SEND_INTERVAL_MS        10000UL   // Send data every 10 seconds

// ------------------------------------------------------------------------------
// 4. Wi-Fi & Backend Endpoint Configuration
// ------------------------------------------------------------------------------
// Enter your local Wi-Fi router or hotspot details here:
const char* WIFI_SSID     = "WIFI_NAME";
const char* WIFI_PASSWORD = "WIFI_PASSWORD";

// Enter your computer's local LAN IP running the FastAPI backend:
// Example: "https://192.168.1.50:8000/api/v1/measurements"
const char* SERVER_URL    = "https://192.168.1.0:8000/api/v1/measurements";

// Unique identifier for this patient monitor hardware unit:
const char* DEVICE_ID     = "esp32_001";


// ==============================================================================
// ⏱️ STATE TRACKING VARIABLES
// ==============================================================================
unsigned long lastSendTime = 0;          // Timestamp of the last successful transmission
unsigned long lastSensorActiveTime = 0;  // Timestamp of the last detected sensor signal
bool sensorAttached = true;              // Flag: Is the patient currently wearing the sensor?
unsigned long lastCountdownLog = 0;      // Timer to throttle countdown prints in Serial


// ==============================================================================
// 🔍 DIAGNOSTICS: Check Why the ESP32 Woke Up
// ==============================================================================
// After deep sleep, the ESP32 restarts setup() like a fresh boot.
// This function checks the RTC controller to tell us how it was woken up.
void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  Serial.println("\n==========================================");
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      // Woken up by the primary sensor line (e.g. finger placed on sensor)
      Serial.printf("[WAKEUP] Woke up from Deep Sleep by SENSOR signal on RTC GPIO %d!\n", WAKEUP_SENSOR_PIN);
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      // Woken up by the BOOT button (GPIO 0)
      Serial.println("[WAKEUP] Woke up from Deep Sleep by BOOT button (GPIO 0)!");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("[WAKEUP] Woke up by internal Timer!");
      break;
    default:
      // First boot / regular power cable plugged in
      Serial.printf("[STARTUP] Fresh Power-On or Manual Reset (Cause code: %d)\n", wakeup_reason);
      break;
  }
  Serial.println("==========================================\n");
}


// ==============================================================================
// 📡 WI-FI CONNECTION HANDLER
// ==============================================================================
void connectToWiFi() {
  Serial.print("[WIFI] Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Attempt connection for up to 10 seconds (20 x 500ms)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected successfully!");
    Serial.print("[WIFI] Assigned ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] Warning: Could not connect to Wi-Fi. (Running in offline/simulation mode)");
  }
}


// ==============================================================================
// 🔒 SECURE HTTPS TELEMETRY SENDER
// ==============================================================================
// Sends patient measurements over TLS/HTTPS with the required 'X-API-Key' header.
void sendMeasurement(int heartRate, float spo2, const char* statusNote) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTPS] Wi-Fi is offline, skipping transmission.");
    return;
  }

  // 1. Initialize the TLS secure client
  WiFiClientSecure client;

  if (USE_STRICT_SSL_CERT) {
    // Production mode: verifies against the embedded Root CA certificate
    client.setCACert(SERVER_ROOT_CA);
    Serial.println("[TLS] Using strict CA root certificate verification.");
  } else {
    // Development mode: encrypts data over TLS without requiring a domain CA chain
    client.setInsecure();
  }

  // 2. Open HTTPS connection to FastAPI
  HTTPClient https;
  if (!https.begin(client, SERVER_URL)) {
    Serial.println("[HTTPS] Error: Could not initialize secure connection to server.");
    return;
  }

  // 3. Inject Security Headers
  // - 'Content-Type': Tells FastAPI we are sending JSON
  // - 'X-API-Key': The shared secret password required by our backend
  https.addHeader("Content-Type", "application/json");
  https.addHeader("X-API-Key", ESP32_API_KEY);

  // 4. Construct JSON payload matching FastAPI's 'MeasurementInput' model
  String payload = "{\"device_id\":\"" + String(DEVICE_ID) + "\","
                 + "\"heart_rate_bpm\":" + String(heartRate) + ","
                 + "\"spo2_percent\":" + String(spo2, 1) + "}";

  Serial.printf("[HTTPS] Sending encrypted packet (%s): %s\n", statusNote, payload.c_str());
  int statusCode = https.POST(payload);

  // 5. Evaluate Response
  Serial.printf("[HTTPS] Server Response Status: %d\n", statusCode);
  if (statusCode == 200) {
    Serial.print("[HTTPS] Verified Response Body: ");
    Serial.println(https.getString());
  } else if (statusCode == 401) {
    // 401 means the API key was missing or didn't match the backend .env!
    Serial.println("[AUTH ERROR] 401 Unauthorized! Check that ESP32_API_KEY matches the backend .env file.");
  } else if (statusCode > 0) {
    Serial.printf("[HTTPS] Response (%d): %s\n", statusCode, https.getString().c_str());
  } else {
    Serial.printf("[HTTPS] Network/Connection error: %s\n", HTTPClient::errorToString(statusCode).c_str());
  }

  // Always free up HTTPS connection resources
  https.end();
}


// ==============================================================================
// 🌙 GRACEFUL DEEP SLEEP SHUTDOWN
// ==============================================================================
// Shuts down Wi-Fi, notifies the backend, arms the wake-up interrupts, and sleeps.
void enterDeepSleep() {
  Serial.println("\n==========================================");
  Serial.printf("[SLEEP] Inactivity condition met (No sensor signal for %lu seconds).\n", INACTIVITY_TIMEOUT_MS / 1000);
  Serial.println("[SLEEP] Beginning graceful shutdown sequence...");

  // Step 1: Send a final '0 BPM / 0% SpO2' packet so the dashboard/server knows
  // that the patient detached the unit intentionally (rather than a crash or dead battery)
  sendMeasurement(0, 0.0, "DEEP_SLEEP_NOTIFICATION");

  // Step 2: Power down Wi-Fi and Radio to prevent sleep current leaks
  Serial.println("[SLEEP] Powering down Wi-Fi and radio...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Step 3: Arm Automatic Hardware Wake-Up Sources
  // A. Primary: Sensor Interrupt pin (wakes when sensor line triggers)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_SENSOR_PIN, WAKEUP_PIN_LEVEL);

  // B. Secondary Fallback: Dev board BOOT button (GPIO 0) for manual patient wake-up
  if (WAKEUP_SENSOR_PIN != 0) {
    esp_sleep_enable_ext1_wakeup(1ULL << 0, ESP_EXT1_WAKEUP_ALL_LOW);
  }

  Serial.printf("[SLEEP] Armed automatic sensor wake-up on GPIO %d (active %s).\n",
                WAKEUP_SENSOR_PIN, WAKEUP_PIN_LEVEL == 0 ? "LOW" : "HIGH");
  Serial.println("[SLEEP] ESP32 is entering Deep Sleep NOW (~10µA power draw). Goodnight! (ZZZ)");
  Serial.println("==========================================\n");
  Serial.flush(); // Ensure all serial bytes are transmitted before CPU sleeps

  // Step 4: Shut down CPU and enter Deep Sleep
  esp_deep_sleep_start();
}


// ==============================================================================
// ⌨️ SERIAL MONITOR SIMULATION CONTROLS
// ==============================================================================
// Allows testing sensor detachment & sleep without physical hardware attached.
void handleSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = (char)Serial.read();

    if (cmd == 'd' || cmd == 'D') {
      // Patient removed sensor -> triggers the 2-minute countdown
      sensorAttached = false;
      Serial.println("\n[SIMULATION] Sensor DETACHED (Patient removed sensor). 2-Minute countdown started!");
    } else if (cmd == 'c' || cmd == 'C') {
      // Patient reconnected sensor -> cancels countdown and resumes sending
      sensorAttached = true;
      lastSensorActiveTime = millis();
      Serial.println("\n[SIMULATION] Sensor ATTACHED (Signal detected). Countdown cancelled & timer reset!");
    } else if (cmd == 's' || cmd == 'S') {
      // Immediately test entering deep sleep
      Serial.println("\n[SIMULATION] Manual immediate sleep requested.");
      enterDeepSleep();
    }
  }
}


// ==============================================================================
// 🚀 ARDUINO SETUP (Runs once on power-on or wake-up)
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000); // Allow serial monitor to open

  // Print startup diagnostics & wake-up cause
  printWakeupReason();

  Serial.println("-----------------------------------------------------------------");
  Serial.println("🏥 SECURE BIOMONITOR ESP32 TELEMETRY (HTTPS + API-KEY + DEEP SLEEP)");
  Serial.printf("Endpoint:           %s\n", SERVER_URL);
  Serial.printf("API Key Configured: %s\n", (strlen(ESP32_API_KEY) > 0 ? "YES" : "NO"));
  Serial.printf("Inactivity Timeout: %lu seconds\n", INACTIVITY_TIMEOUT_MS / 1000);
  Serial.printf("Sensor Wakeup Pin:  GPIO %d\n", WAKEUP_SENSOR_PIN);
  Serial.println("\n💡 SIMULATION CONTROLS (Type letter into Serial Monitor):");
  Serial.println("   'd' -> Simulate sensor detached (starts countdown to sleep)");
  Serial.println("   'c' -> Simulate sensor attached (resumes sending & resets timer)");
  Serial.println("   's' -> Put ESP32 into deep sleep immediately");
  Serial.println("-----------------------------------------------------------------\n");

  // Connect to Wi-Fi
  connectToWiFi();

  // Configure the hardware wake-up pin as input with internal pull-up / pull-down
  if (WAKEUP_PIN_LEVEL == 0) {
    pinMode(WAKEUP_SENSOR_PIN, INPUT_PULLUP);
  } else {
    pinMode(WAKEUP_SENSOR_PIN, INPUT_PULLDOWN);
  }

  // Initialize active timer
  lastSensorActiveTime = millis();
}


// ==============================================================================
// 🔄 ARDUINO MAIN LOOP
// ==============================================================================
void loop() {
  unsigned long now = millis();

  // Check if collaborator typed any simulation commands into Serial
  handleSerialCommands();

  // ----------------------------------------------------------------------------
  // CASE A: Sensor is attached and actively measuring patient
  // ----------------------------------------------------------------------------
  if (sensorAttached) {
    // Keep resetting the inactivity timer while signal is detected
    lastSensorActiveTime = now;

    // Send telemetry packet every SEND_INTERVAL_MS (e.g. 10 seconds)
    if (now - lastSendTime >= SEND_INTERVAL_MS) {
      lastSendTime = now;

      // Simulated vital signs (75 BPM, 98.5% SpO2)
      sendMeasurement(75, 98.5, "ACTIVE_VITAL_SIGNS");
    }
  }
  // ----------------------------------------------------------------------------
  // CASE B: Sensor is detached / no signal -> countdown to Deep Sleep
  // ----------------------------------------------------------------------------
  else {
    unsigned long idleElapsed = now - lastSensorActiveTime;

    // Print countdown reminder every 10 seconds so collaborator sees progress
    if (now - lastCountdownLog >= 10000UL) {
      lastCountdownLog = now;
      unsigned long secondsLeft = (INACTIVITY_TIMEOUT_MS > idleElapsed) 
                                ? (INACTIVITY_TIMEOUT_MS - idleElapsed) / 1000 
                                : 0;
      Serial.printf("[INACTIVITY] Sensor detached! Entering Deep Sleep in %lu seconds...\n", secondsLeft);
    }

    // When 2 minutes (120 seconds) have passed without sensor: Sleep!
    if (idleElapsed >= INACTIVITY_TIMEOUT_MS) {
      enterDeepSleep();
    }
  }

  delay(10); // Yield to FreeRTOS background watchdog
}
