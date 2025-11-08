/*
 * ESP32 Train Controller
 * Controls train movement, detects stations via IR sensor, sends telemetry to Raspberry Pi
 * 
 * Hardware:
 * - ESP32
 * - IR Sensor (for station detection)
 * - Motor Driver (L298N or similar)
 * - MPU6050 (optional - for telemetry)
 * 
 * Wiring:
 * IR Sensor -> GPIO 33
 * Motor Control -> GPIO 25
 * MPU6050 -> I2C (SDA: GPIO21, SCL: GPIO22)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

// ============================================================================
// 🔧 CONFIGURATION - CHANGE THESE VALUES FOR YOUR SETUP
// ============================================================================

// 📡 WiFi Configuration
const char* WIFI_SSID = "YourWiFiName";          // ⚠️ CHANGE THIS
const char* WIFI_PASSWORD = "YourWiFiPassword";  // ⚠️ CHANGE THIS

// 🖥️ Raspberry Pi Backend Configuration
const char* RASPI_IP = "192.168.1.100";          // ⚠️ CHANGE THIS to your Pi's IP
const int RASPI_PORT = 8000;
const char* API_ENDPOINT = "/api/trains/telemetry";

// 🚂 Train Configuration
const char* TRAIN_ID = "train-001";              // ⚠️ CHANGE THIS if multiple trains
const char* TRAIN_NAME = "Express Line 1";

// 🗺️ Route Configuration (must match backend)
const char* ROUTE[] = {"station-a", "station-b", "station-c"};  // ⚠️ CHANGE THIS
const int ROUTE_SIZE = 3;

// 📌 Hardware Pin Configuration
#define IR_PIN 33          // IR sensor pin      - ⚠️ Change if needed
#define MOTOR_PIN 25       // Motor control pin  - ⚠️ Change if needed

// ⏱️ Station Stop Configuration
#define FIRST_STOP_TIME 5000    // First station: 5 seconds
#define REGULAR_STOP_TIME 3000  // Other stations: 3 seconds

// 🔧 IR Sensor Configuration
#define DEBOUNCE_DELAY 50       // Stability check time (ms)

// ============================================================================
// END OF CONFIGURATION
// ============================================================================

// State tracking
int currentStationIndex = 0;
int whiteCount = 0;
bool isPaused = false;
bool ignoreWhite = false;
unsigned long pauseStart = 0;
unsigned long pauseDuration = 0;

// IR sensor debouncing
bool stableState = LOW;
bool lastStableState = LOW;
unsigned long lastDebounceTime = 0;

// MPU6050
Adafruit_MPU6050 mpu;
bool mpuAvailable = false;
unsigned long lastMPUPrint = 0;
const unsigned long mpuInterval = 5000; // Print every 5s

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("==================================================");
  Serial.println("   🚂 SMART TRAIN CONTROLLER");
  Serial.println("==================================================");
  Serial.print("Train ID: ");
  Serial.println(TRAIN_ID);
  Serial.print("Train Name: ");
  Serial.println(TRAIN_NAME);
  Serial.print("Route: ");
  for (int i = 0; i < ROUTE_SIZE; i++) {
    Serial.print(ROUTE[i]);
    if (i < ROUTE_SIZE - 1) Serial.print(" → ");
  }
  Serial.println("\n==================================================\n");
  
  // Setup pins
  pinMode(IR_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, HIGH); // Start moving
  
  // Initialize MPU6050 (optional)
  if (mpu.begin()) {
    Serial.println("✅ MPU6050 found!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    mpuAvailable = true;
  } else {
    Serial.println("⚠️  MPU6050 not found (optional - continuing without it)");
    mpuAvailable = false;
  }
  
  // Connect to WiFi
  connectWiFi();
  
  // Send initial telemetry
  sendTelemetry("moving", nullptr);
  
  Serial.println("\n✅ System ready!");
  Serial.println("Train is now moving...");
  Serial.println("------------------------------\n");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi disconnected. Reconnecting...");
    connectWiFi();
  }
  
  // Read IR sensor with debouncing
  int reading = digitalRead(IR_PIN);
  static int lastReading = LOW;
  
  if (reading != lastReading) {
    lastDebounceTime = currentMillis;
    lastReading = reading;
  }
  
  // Confirm stable state
  if ((currentMillis - lastDebounceTime) > DEBOUNCE_DELAY) {
    stableState = reading;
  }
  
  // ========== PAUSE HANDLING (AT STATION) ==========
  if (isPaused) {
    if (currentMillis - pauseStart >= pauseDuration) {
      // Pause complete - resume movement
      isPaused = false;
      ignoreWhite = false;
      
      Serial.println("✅ Departing station...");
      digitalWrite(MOTOR_PIN, HIGH);
      
      // Send "moving" telemetry
      sendTelemetry("moving", nullptr);
      
      Serial.println("🚄 Train moving...\n");
    } else {
      // Still at station
      digitalWrite(MOTOR_PIN, LOW);
    }
  }
  
  // ========== DETECT STATION (BLACK → WHITE TRANSITION) ==========
  if (!ignoreWhite && stableState == HIGH && lastStableState == LOW) {
    whiteCount++;
    if (whiteCount > ROUTE_SIZE) whiteCount = 1;
    
    // Determine which station we're at
    int stationIndex = (currentStationIndex) % ROUTE_SIZE;
    const char* stationId = ROUTE[stationIndex];
    
    // First stop is 5 seconds, others are 3 seconds
    pauseDuration = (whiteCount == 1) ? FIRST_STOP_TIME : REGULAR_STOP_TIME;
    pauseStart = currentMillis;
    isPaused = true;
    ignoreWhite = true;
    
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.print("🛑 Station detected: ");
    Serial.println(stationId);
    Serial.print("⏱️  Stopping for ");
    Serial.print(pauseDuration / 1000);
    Serial.println(" seconds");
    
    // Stop motor
    digitalWrite(MOTOR_PIN, LOW);
    
    // Send "stopped" telemetry
    sendTelemetry("stopped", stationId);
    
    // After 1 second, send "loading" telemetry (doors open)
    delay(1000);
    sendTelemetry("loading", stationId);
    
    Serial.println("🚪 Loading passengers...");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    // Move to next station in route
    currentStationIndex = (currentStationIndex + 1) % ROUTE_SIZE;
  }
  
  lastStableState = stableState;
  
  // ========== MPU6050 TELEMETRY (OPTIONAL) ==========
  if (mpuAvailable && (currentMillis - lastMPUPrint >= mpuInterval)) {
    lastMPUPrint = currentMillis;
    printMPUData();
  }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void connectWiFi() {
  Serial.print("📡 Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n❌ WiFi connection failed!");
  }
}

void sendTelemetry(const char* status, const char* stationId) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ No WiFi - cannot send telemetry");
    return;
  }
  
  HTTPClient http;
  
  // Build API URL
  String url = "http://" + String(RASPI_IP) + ":" + String(RASPI_PORT) + String(API_ENDPOINT);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // ⚠️ THIS IS THE PAYLOAD FORMAT YOUR BACKEND EXPECTS
  // Create JSON payload (NO TIMESTAMP - backend generates it)
  StaticJsonDocument<256> doc;
  doc["train_id"] = TRAIN_ID;
  doc["status"] = status;
  
  if (stationId != nullptr) {
    doc["station_id"] = stationId;
  } else {
    doc["station_id"] = (char*)0; // null
  }
  
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  Serial.print("📤 Sending telemetry: ");
  Serial.println(jsonPayload);
  
  // Send POST request
  int httpCode = http.POST(jsonPayload);
  
  if (httpCode > 0) {
    Serial.print("✅ Response: ");
    Serial.println(httpCode);
    
    if (httpCode == 200) {
      String response = http.getString();
      Serial.print("📄 ");
      Serial.println(response);
    }
  } else {
    Serial.print("❌ HTTP error: ");
    Serial.println(http.errorToString(httpCode));
  }
  
  http.end();
}

void printMPUData() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  Serial.println("=== 📊 MPU6050 Telemetry ===");
  Serial.print("Accel X: "); Serial.print(a.acceleration.x, 2);
  Serial.print(" | Y: "); Serial.print(a.acceleration.y, 2);
  Serial.print(" | Z: "); Serial.println(a.acceleration.z, 2);
  
  Serial.print("Gyro  X: "); Serial.print(g.gyro.x, 2);
  Serial.print(" | Y: "); Serial.print(g.gyro.y, 2);
  Serial.print(" | Z: "); Serial.println(g.gyro.z, 2);
  
  Serial.print("Temp: "); Serial.print(temp.temperature, 1);
  Serial.println(" °C");
  Serial.println("============================\n");
}