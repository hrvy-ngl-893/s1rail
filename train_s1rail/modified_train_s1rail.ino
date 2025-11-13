/*
 * ============================================================================
 * ESP32 Smart Train Controller - Enhanced Version
 * ============================================================================
 * 
 * PURPOSE:
 * Controls train movement, detects stations via IR sensor, and sends real-time
 * telemetry to Raspberry Pi backend for live tracking and monitoring.
 * 
 * HARDWARE REQUIREMENTS:
 * - ESP32 Development Board
 * - IR Sensor Module (for station detection)
 * - Motor Driver Module (L298N, L293D, or relay)
 * - MPU6050 Accelerometer/Gyroscope (optional - for motion telemetry)
 * 
 * WIRING DIAGRAM:
 * ┌──────────────────────────────────────────────────────────────┐
 * │ Component    │ ESP32 Pin │ Description                       │
 * ├──────────────────────────────────────────────────────────────┤
 * │ IR Sensor    │ GPIO 33   │ Station detection (INPUT_PULLUP)  │
 * │ Motor Driver │ GPIO 25   │ Motor control (HIGH=run, LOW=stop)│
 * │ MPU6050 SDA  │ GPIO 21   │ I2C Data (default SDA)            │
 * │ MPU6050 SCL  │ GPIO 22   │ I2C Clock (default SCL)           │
 * └──────────────────────────────────────────────────────────────┘
 * 
 * FEATURES:
 * - Automatic station detection and stopping
 * - Variable duration stops (first station: 5s, others: 3s)
 * - Real-time telemetry to backend API
 * - Motion tracking with MPU6050 (optional)
 * - Debounced IR sensor reading
 * - Automatic WiFi reconnection
 * - Comprehensive event logging
 * - Route cycle tracking
 * 
 * VERSION: 2.0
 * LAST UPDATED: November 2025
 * ============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

// ============================================================================
// 🔧 CONFIGURATION SECTION - CUSTOMIZE FOR YOUR DEPLOYMENT
// ============================================================================

// 📡 WiFi Credentials
const char* WIFI_SSID = "YourWiFiName";          // Your WiFi network name
const char* WIFI_PASSWORD = "YourWiFiPassword";  // Your WiFi password

// 🖥️ Backend Server Configuration
const char* RASPI_IP = "192.168.1.100";          // Raspberry Pi IP address
const int RASPI_PORT = 8000;                     // Backend server port
const char* API_ENDPOINT = "/api/trains/telemetry"; // Telemetry API endpoint

// 🚂 Train Identity
const char* TRAIN_ID = "train-001";              // Unique train identifier
const char* TRAIN_NAME = "Express Line 1";       // Human-readable train name

// 🗺️ Route Configuration (must match backend stations)
const char* ROUTE[] = {"station-a", "station-b", "station-c"};
const int ROUTE_SIZE = 3;

// 📌 Hardware Pin Configuration
#define IR_PIN 33          // IR sensor input pin
#define MOTOR_PIN 25       // Motor control output pin

// ⏱️ Timing Configuration (milliseconds)
#define FIRST_STOP_TIME 5000       // First station stop duration
#define REGULAR_STOP_TIME 3000     // Regular station stop duration
#define DEBOUNCE_DELAY 50          // IR sensor debounce time
#define MPU_INTERVAL 5000          // MPU6050 reading interval
#define WIFI_CONNECT_TIMEOUT 15000 // WiFi connection timeout
#define HTTP_TIMEOUT 5000          // HTTP request timeout

// 🐛 Debug Settings
#define DEBUG_MODE true            // Enable detailed logging

// ============================================================================
// END OF CONFIGURATION
// ============================================================================

// State tracking variables
struct TrainState {
  int currentStationIndex;    // Current position in route
  int whiteCount;             // Station counter (for stop duration logic)
  bool isPaused;              // Currently stopped at station
  bool ignoreWhite;           // Ignore consecutive white readings
  unsigned long pauseStart;   // When current pause started
  unsigned long pauseDuration;// How long to pause
  unsigned long systemStart;  // System boot time
  unsigned long totalStops;   // Total stations visited
  String lastStatus;          // Last telemetry status sent
} train;

// IR sensor debouncing
bool stableIRState = LOW;
bool lastStableIRState = LOW;
unsigned long lastDebounceTime = 0;
int lastRawIRReading = LOW;

// MPU6050 sensor
Adafruit_MPU6050 mpu;
bool mpuAvailable = false;
unsigned long lastMPURead = 0;

// WiFi management
unsigned long lastWiFiCheck = 0;
int wifiReconnectAttempts = 0;

// Statistics
unsigned long successfulTelemetry = 0;
unsigned long failedTelemetry = 0;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void printStartupBanner();
void initializeHardware();
void initializeMPU6050();
void connectToWiFi();
void ensureWiFiConnected();
void processIRSensor();
void handleStationArrival();
void handleStationDeparture();
void sendTelemetry(const char* status, const char* stationId);
void printMPUData();
void logEvent(const char* eventType, const char* message);
void printDiagnostics();

// ============================================================================
// SETUP - RUNS ONCE AT STARTUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  train.systemStart = millis();
  train.currentStationIndex = 0;
  train.whiteCount = 0;
  train.isPaused = false;
  train.ignoreWhite = false;
  train.pauseStart = 0;
  train.pauseDuration = 0;
  train.totalStops = 0;
  train.lastStatus = "";
  
  // Print startup information
  printStartupBanner();
  
  // Initialize hardware
  initializeHardware();
  initializeMPU6050();
  
  // Connect to WiFi
  connectToWiFi();
  
  // Send initial telemetry
  logEvent("SYSTEM", "Train system initialized and starting");
  sendTelemetry("moving", nullptr);
  
  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.println("║          ✅ SYSTEM READY - TRAIN STARTING          ║");
  Serial.println("╚════════════════════════════════════════════════════╝\n");
  
  Serial.println("🚄 Train is now moving on route...\n");
}

// ============================================================================
// MAIN LOOP - RUNS CONTINUOUSLY
// ============================================================================

void loop() {
  unsigned long currentTime = millis();
  
  // Monitor and maintain WiFi connection
  ensureWiFiConnected();
  
  // Process IR sensor for station detection
  processIRSensor();
  
  // Handle station pause timing
  if (train.isPaused) {
    if (currentTime - train.pauseStart >= train.pauseDuration) {
      handleStationDeparture();
    } else {
      // Keep motor stopped while at station
      digitalWrite(MOTOR_PIN, LOW);
    }
  }
  
  // Read and log MPU6050 data periodically
  if (mpuAvailable && (currentTime - lastMPURead >= MPU_INTERVAL)) {
    lastMPURead = currentTime;
    printMPUData();
  }
}

// ============================================================================
// INITIALIZATION FUNCTIONS
// ============================================================================

void printStartupBanner() {
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════════════════╗");
  Serial.println("║        🚂 SMART TRAIN CONTROLLER v2.0             ║");
  Serial.println("║     Real-time Telemetry & Station Detection       ║");
  Serial.println("╚════════════════════════════════════════════════════╝");
  Serial.println();
  Serial.print("🆔 Train ID: ");
  Serial.println(TRAIN_ID);
  Serial.print("🏷️  Train Name: ");
  Serial.println(TRAIN_NAME);
  Serial.print("🗺️  Route: ");
  for (int i = 0; i < ROUTE_SIZE; i++) {
    Serial.print(ROUTE[i]);
    if (i < ROUTE_SIZE - 1) Serial.print(" → ");
  }
  Serial.println();
  Serial.print("🐛 Debug Mode: ");
  Serial.println(DEBUG_MODE ? "ENABLED" : "DISABLED");
  Serial.println("════════════════════════════════════════════════════");
  Serial.println();
}

void initializeHardware() {
  Serial.println("🔧 Initializing hardware components...");
  
  // Configure IR sensor
  pinMode(IR_PIN, INPUT_PULLUP);
  Serial.print("   ✅ IR Sensor initialized (GPIO ");
  Serial.print(IR_PIN);
  Serial.println(")");
  
  // Configure motor control
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, HIGH); // Start with motor running
  Serial.print("   ✅ Motor controller initialized (GPIO ");
  Serial.print(MOTOR_PIN);
  Serial.println(")");
  Serial.println("   🔌 Motor status: RUNNING (HIGH)");
  
  Serial.println();
}

void initializeMPU6050() {
  Serial.println("📊 Initializing MPU6050 motion sensor...");
  
  if (mpu.begin()) {
    mpuAvailable = true;
    
    // Configure sensor ranges
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    
    Serial.println("✅ MPU6050 detected and configured!");
    Serial.println("   • Accelerometer range: ±8G");
    Serial.println("   • Gyroscope range: ±500°/s");
    Serial.println("   • Filter bandwidth: 21 Hz");
    
    delay(100);
  } else {
    mpuAvailable = false;
    Serial.println("⚠️  MPU6050 not found (optional - continuing without it)");
    Serial.println("   System will operate normally without motion telemetry");
  }
  
  Serial.println();
}

void connectToWiFi() {
  Serial.println("📡 Connecting to WiFi...");
  Serial.print("   SSID: ");
  Serial.println(WIFI_SSID);
  
  // Disconnect first to clear any previous connection state
  WiFi.disconnect(true);
  delay(1000);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long startAttempt = millis();
  int dotCount = 0;
  
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_CONNECT_TIMEOUT) {
    delay(500);
    Serial.print(".");
    dotCount++;
    if (dotCount % 50 == 0) Serial.println();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected successfully!");
    Serial.print("   IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("   Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("   MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("   Gateway: ");
    Serial.println(WiFi.gatewayIP());
    wifiReconnectAttempts = 0;
  } else {
    Serial.println("\n❌ WiFi connection failed!");
    Serial.println("   TROUBLESHOOTING:");
    Serial.println("   • Verify SSID and password are correct");
    Serial.println("   • Check if router is powered on and in range");
    Serial.println("   • Ensure WiFi uses 2.4GHz (ESP32 doesn't support 5GHz)");
    Serial.println("   ⚠️  Train will operate but telemetry will not be sent");
  }
  
  Serial.println();
}

// ============================================================================
// WIFI MANAGEMENT
// ============================================================================

void ensureWiFiConnected() {
  unsigned long currentTime = millis();
  
  // Check WiFi status every 10 seconds
  if (currentTime - lastWiFiCheck < 10000) {
    return;
  }
  lastWiFiCheck = currentTime;
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiReconnectAttempts++;
    
    Serial.println("\n⚠️  WiFi connection lost!");
    Serial.print("   Reconnection attempt #");
    Serial.println(wifiReconnectAttempts);
    
    WiFi.disconnect();
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    // Brief wait for connection (non-blocking)
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
      delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ WiFi reconnected successfully!");
      Serial.print("   IP: ");
      Serial.println(WiFi.localIP());
      wifiReconnectAttempts = 0;
    } else {
      Serial.println("❌ Reconnection failed - will retry in 10 seconds");
      Serial.println("   Train continues to operate normally");
    }
    Serial.println();
  }
}

// ============================================================================
// IR SENSOR PROCESSING
// ============================================================================

void processIRSensor() {
  unsigned long currentTime = millis();
  int rawReading = digitalRead(IR_PIN);
  
  // Debounce logic: only accept reading if stable
  if (rawReading != lastRawIRReading) {
    lastDebounceTime = currentTime;
    lastRawIRReading = rawReading;
  }
  
  // Update stable state if reading has been consistent
  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    stableIRState = rawReading;
  }
  
  // Detect transition from BLACK (LOW) to WHITE (HIGH) = station marker
  if (!train.ignoreWhite && 
      stableIRState == HIGH && 
      lastStableIRState == LOW) {
    
    handleStationArrival();
  }
  
  lastStableIRState = stableIRState;
}

// ============================================================================
// STATION HANDLING
// ============================================================================

void handleStationArrival() {
  train.whiteCount++;
  if (train.whiteCount > ROUTE_SIZE) {
    train.whiteCount = 1;
  }
  
  // Determine which station we're at
  int stationIndex = train.currentStationIndex % ROUTE_SIZE;
  const char* stationId = ROUTE[stationIndex];
  
  // Determine stop duration (first stop is longer)
  train.pauseDuration = (train.whiteCount == 1) ? FIRST_STOP_TIME : REGULAR_STOP_TIME;
  train.pauseStart = millis();
  train.isPaused = true;
  train.ignoreWhite = true;
  train.totalStops++;
  
  // Stop the motor
  digitalWrite(MOTOR_PIN, LOW);
  
  // Log arrival event
  Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("🛑 STATION ARRIVAL DETECTED");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.print("   🚉 Station: ");
  Serial.print(stationId);
  Serial.print(" (");
  Serial.print(stationIndex + 1);
  Serial.print("/");
  Serial.print(ROUTE_SIZE);
  Serial.println(")");
  Serial.print("   ⏱️  Stop Duration: ");
  Serial.print(train.pauseDuration / 1000);
  Serial.println(" seconds");
  Serial.print("   📊 Total Stops: ");
  Serial.println(train.totalStops);
  Serial.print("   ⏰ Arrival Time: ");
  Serial.print((millis() - train.systemStart) / 1000);
  Serial.println("s since boot");
  Serial.print("   🔌 Motor: STOPPED (Pin ");
  Serial.print(MOTOR_PIN);
  Serial.println(" = LOW)");
  
  // Send "stopped" telemetry
  logEvent("TELEMETRY", "Sending 'stopped' status");
  sendTelemetry("stopped", stationId);
  
  // Wait 1 second, then send "loading" status
  delay(1000);
  
  Serial.println("   🚪 Opening doors...");
  logEvent("TELEMETRY", "Sending 'loading' status");
  sendTelemetry("loading", stationId);
  
  Serial.println("   👥 Loading/unloading passengers...");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  
  // Move to next station in route
  train.currentStationIndex = (train.currentStationIndex + 1) % ROUTE_SIZE;
}

void handleStationDeparture() {
  train.isPaused = false;
  train.ignoreWhite = false;
  
  // Resume motor
  digitalWrite(MOTOR_PIN, HIGH);
  
  // Log departure event
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("🚀 DEPARTING STATION");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.print("   ⏰ Departure Time: ");
  Serial.print((millis() - train.systemStart) / 1000);
  Serial.println("s since boot");
  Serial.print("   🔌 Motor: RUNNING (Pin ");
  Serial.print(MOTOR_PIN);
  Serial.println(" = HIGH)");
  Serial.print("   🎯 Next Station: ");
  Serial.println(ROUTE[train.currentStationIndex % ROUTE_SIZE]);
  
  // Send "moving" telemetry
  logEvent("TELEMETRY", "Sending 'moving' status");
  sendTelemetry("moving", nullptr);
  
  Serial.println("   🚄 Train moving to next station...");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

// ============================================================================
// TELEMETRY FUNCTIONS
// ============================================================================

void sendTelemetry(const char* status, const char* stationId) {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Cannot send telemetry: WiFi not connected");
    failedTelemetry++;
    return;
  }
  
  HTTPClient http;
  
  // Build API URL
  String url = "http://" + String(RASPI_IP) + ":" + 
               String(RASPI_PORT) + String(API_ENDPOINT);
  
  if (DEBUG_MODE) {
    Serial.println("\n📤 SENDING TELEMETRY TO BACKEND");
    Serial.print("   URL: ");
    Serial.println(url);
  }
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT);
  
  // Create JSON payload
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
  
  Serial.print("   📦 Payload: ");
  Serial.println(jsonPayload);
  Serial.print("   📏 Size: ");
  Serial.print(jsonPayload.length());
  Serial.println(" bytes");
  
  // Send POST request
  unsigned long requestStart = millis();
  int httpCode = http.POST(jsonPayload);
  unsigned long requestDuration = millis() - requestStart;
  
  Serial.println("\n📥 BACKEND RESPONSE");
  Serial.print("   HTTP Code: ");
  Serial.print(httpCode);
  Serial.print(" (");
  
  // Describe HTTP code
  if (httpCode == 200) {
    Serial.println("OK)");
    successfulTelemetry++;
  } else if (httpCode == 201) {
    Serial.println("Created)");
    successfulTelemetry++;
  } else if (httpCode > 0) {
    Serial.print("Error ");
    Serial.print(httpCode);
    Serial.println(")");
    failedTelemetry++;
  } else {
    Serial.print("Connection Failed - ");
    Serial.print(http.errorToString(httpCode));
    Serial.println(")");
    failedTelemetry++;
  }
  
  Serial.print("   ⏱️  Response Time: ");
  Serial.print(requestDuration);
  Serial.println("ms");
  
  if (httpCode > 0) {
    String response = http.getString();
    if (response.length() > 0) {
      Serial.print("   📄 Response Body: ");
      Serial.println(response);
    }
  }
  
  Serial.print("   📊 Success Rate: ");
  if (successfulTelemetry + failedTelemetry > 0) {
    float successRate = (float)successfulTelemetry / 
                       (successfulTelemetry + failedTelemetry) * 100.0;
    Serial.print(successRate, 1);
    Serial.print("% (");
    Serial.print(successfulTelemetry);
    Serial.print("/");
    Serial.print(successfulTelemetry + failedTelemetry);
    Serial.println(")");
  } else {
    Serial.println("N/A");
  }
  
  Serial.println();
  
  http.end();
  train.lastStatus = String(status);
}

// ============================================================================
// MPU6050 SENSOR READING
// ============================================================================

void printMPUData() {
  if (!mpuAvailable) return;
  
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);
  
  Serial.println("╔════════════════════════════════════════════════════╗");
  Serial.println("║           📊 MPU6050 MOTION TELEMETRY              ║");
  Serial.println("╠════════════════════════════════════════════════════╣");
  
  Serial.print("║ ⏱️  Uptime: ");
  unsigned long uptime = (millis() - train.systemStart) / 1000;
  Serial.print(uptime);
  Serial.println(" seconds");
  
  Serial.print("║ 🚂 Train Status: ");
  Serial.println(train.isPaused ? "STOPPED AT STATION" : "MOVING");
  
  Serial.println("║");
  Serial.println("║ 📈 Accelerometer (m/s²):");
  Serial.print("║    X: ");
  Serial.print(accel.acceleration.x, 2);
  Serial.print("  Y: ");
  Serial.print(accel.acceleration.y, 2);
  Serial.print("  Z: ");
  Serial.println(accel.acceleration.z, 2);
  
  Serial.println("║");
  Serial.println("║ 🔄 Gyroscope (rad/s):");
  Serial.print("║    X: ");
  Serial.print(gyro.gyro.x, 2);
  Serial.print("  Y: ");
  Serial.print(gyro.gyro.y, 2);
  Serial.print("  Z: ");
  Serial.println(gyro.gyro.z, 2);
  
  Serial.println("║");
  Serial.print("║ 🌡️  Temperature: ");
  Serial.print(temp.temperature, 1);
  Serial.println(" °C");
  
  Serial.println("╚════════════════════════════════════════════════════╝");
  Serial.println();
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void logEvent(const char* eventType, const char* message) {
  if (!DEBUG_MODE) return;
  
  unsigned long uptime = (millis() - train.systemStart) / 1000;
  Serial.print("[");
  Serial.print(uptime);
  Serial.print("s] [");
  Serial.print(eventType);
  Serial.print("] ");
  Serial.println(message);
}

void printDiagnostics() {
  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.println("║              SYSTEM DIAGNOSTICS                    ║");
  Serial.println("╚════════════════════════════════════════════════════╝");
  
  unsigned long uptime = (millis() - train.systemStart) / 1000;
  Serial.print("⏱️  System Uptime: ");
  Serial.print(uptime / 3600);
  Serial.print("h ");
  Serial.print((uptime % 3600) / 60);
  Serial.print("m ");
  Serial.print(uptime % 60);
  Serial.println("s");
  
  Serial.print("🚂 Train ID: ");
  Serial.println(TRAIN_ID);
  
  Serial.print("📍 Current Route Position: ");
  Serial.print(train.currentStationIndex % ROUTE_SIZE + 1);
  Serial.print("/");
  Serial.println(ROUTE_SIZE);
  
  Serial.print("🏁 Total Stations Visited: ");
  Serial.println(train.totalStops);
  
  Serial.print("🚦 Current Status: ");
  Serial.println(train.isPaused ? "STOPPED" : "MOVING");
  
  if (train.lastStatus.length() > 0) {
    Serial.print("📡 Last Telemetry: ");
    Serial.println(train.lastStatus);
  }
  
  Serial.print("📊 IR Sensor State: ");
  Serial.println(stableIRState == HIGH ? "WHITE (Station)" : "BLACK (Track)");
  
  Serial.print("🔌 Motor State: ");
  Serial.println(digitalRead(MOTOR_PIN) == HIGH ? "RUNNING" : "STOPPED");
  
  Serial.print("📡 WiFi Status: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected (");
    Serial.print(WiFi.localIP());
    Serial.print(", ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm)");
  } else {
    Serial.print("Disconnected (");
    Serial.print(wifiReconnectAttempts);
    Serial.println(" reconnect attempts)");
  }
  
  Serial.print("📊 Telemetry Stats: ");
  Serial.print(successfulTelemetry);
  Serial.print(" successful, ");
  Serial.print(failedTelemetry);
  Serial.println(" failed");
  
  Serial.print("📡 MPU6050: ");
  Serial.println(mpuAvailable ? "Available" : "Not Available");
  
  Serial.print("💾 Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  
  Serial.println("════════════════════════════════════════════════════\n");
}