/*
 * ============================================================================
 * ESP32 Smart Station RFID Reader - Enhanced Version
 * ============================================================================
 * 
 * PURPOSE:
 * Reads RFID cards at train stations and validates them against a central
 * Raspberry Pi backend system. Handles fare deduction and access control.
 * 
 * HARDWARE REQUIREMENTS:
 * - ESP32 Development Board
 * - RC522 RFID Reader Module
 * - Green LED + 220Ω resistor (optional, for visual feedback)
 * - Red LED + 220Ω resistor (optional, for visual feedback)
 * 
 * WIRING DIAGRAM:
 * ┌──────────────────────────────────────────────────────────────┐
 * │ RC522 Pin    │ ESP32 Pin │ Description                       │
 * ├──────────────────────────────────────────────────────────────┤
 * │ SDA          │ D5        │ Chip Select                       │
 * │ SCK          │ D18       │ SPI Clock                         │
 * │ MOSI         │ D23       │ SPI Master Out Slave In           │
 * │ MISO         │ D19       │ SPI Master In Slave Out           │
 * │ IRQ          │ -         │ Not connected                     │
 * │ GND          │ GND       │ Ground                            │
 * │ RST          │ D22       │ Reset                             │
 * │ VCC          │ 3V3       │ Power (3.3V ONLY!)                │
 * ├──────────────────────────────────────────────────────────────┤
 * │ Green LED +  │ D26       │ Access granted indicator          │
 * │ Red LED +    │ D27       │ Access denied indicator           │
 * │ LED -        │ GND       │ Through 220Ω resistor each        │
 * └──────────────────────────────────────────────────────────────┘
 * 
 * FEATURES:
 * - Automatic WiFi reconnection with exponential backoff
 * - Duplicate card read prevention with cooldown timer
 * - Comprehensive event logging with timestamps
 * - HTTP request retry mechanism with timeout handling
 * - Visual feedback via LEDs (green=success, red=denied)
 * - Detailed error messages for troubleshooting
 * - Debug mode toggle for verbose output
 * - Uptime tracking and statistics
 * 
 * VERSION: 2.0
 * LAST UPDATED: November 2025
 * ============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>

// ============================================================================
// 🔧 CONFIGURATION SECTION - CUSTOMIZE FOR YOUR DEPLOYMENT
// ============================================================================

// 📡 WiFi Credentials
const char* WIFI_SSID = "TIP PayCR";              // Your WiFi network name
const char* WIFI_PASSWORD = "arthurleywin";            // Your WiFi password

// 🖥️ Backend Server Configuration
const char* RASPI_IP = "192.168.1.100";            // Raspberry Pi IP address
const int RASPI_PORT = 8000;                       // Backend server port
const char* API_ENDPOINT = "/api/station/validate"; // API validation endpoint

// 🏢 Station Identity
const char* STATION_ID = "station-a";              // Unique station identifier
const char* STATION_NAME = "Downtown Station";     // Human-readable station name

// 📌 Hardware Pin Assignments (ESP32)
#define SS_PIN 5        // SDA/Chip Select - connects to RC522 SDA
#define RST_PIN 22      // Reset pin - connects to RC522 RST
#define GREEN_LED 26    // Success indicator LED (optional)
#define RED_LED 27      // Failure indicator LED (optional)

// ⏱️ Timing Configuration (milliseconds)
#define LED_DISPLAY_TIME 2000       // Duration to show LED feedback
#define CARD_COOLDOWN 1500          // Minimum time between same card reads
#define HTTP_TIMEOUT 5000           // HTTP request timeout
#define WIFI_CONNECT_TIMEOUT 30000  // WiFi connection timeout

// 🔧 Advanced Settings
#define DEBUG_MODE true             // Enable detailed serial logging
#define MAX_HTTP_RETRIES 2          // Number of retry attempts for failed requests
#define WIFI_RETRY_DELAY 500        // Initial delay between WiFi reconnect attempts

// ============================================================================
// END OF USER CONFIGURATION
// ============================================================================

// Global objects
MFRC522 rfid(SS_PIN, RST_PIN);
WiFiClient wifiClient;

// State tracking variables
unsigned long lastCardReadTime = 0;
String lastCardUID = "";
unsigned long systemStartTime = 0;
unsigned long totalCardsScanned = 0;
unsigned long successfulValidations = 0;
unsigned long failedValidations = 0;

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct ValidationResult {
  bool success;           // HTTP request succeeded
  bool approved;          // Access approved by backend
  String message;         // Response message from backend
  float newBalance;       // Updated card balance
  int httpCode;          // HTTP response code
  String errorDetails;   // Error details if failed
};

// ============================================================================
// FUNCTION DECLARATIONS (Forward declarations for Arduino IDE)
// ============================================================================

void printStartupBanner();
void initializeLEDs();
void initializeSPI();
void initializeRFID();
void connectToWiFi();
void printSystemReady();
void ensureWiFiConnected();
String extractCardUID();
bool isDuplicateRead(String cardUID);
void updateReadState(String cardUID);
void logCardDetection(String cardUID);
ValidationResult validateCardWithBackend(String cardUID);
ValidationResult performHTTPValidation(String cardUID);
void logValidationResult(ValidationResult result);
String getHTTPStatusDescription(int code);
void displayValidationResult(ValidationResult result);
void blinkErrorPattern();
void printDiagnostics();

// ============================================================================
// SETUP - RUNS ONCE AT STARTUP
// ============================================================================

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(1000);
  
  systemStartTime = millis();
  
  // Print startup banner
  printStartupBanner();
  
  // Initialize hardware components
  initializeLEDs();
  initializeSPI();
  initializeRFID();
  
  // Connect to network
  connectToWiFi();
  
  // System ready
  printSystemReady();
}

// ============================================================================
// MAIN LOOP - RUNS CONTINUOUSLY
// ============================================================================

void loop() {
  // Monitor and maintain WiFi connection
  ensureWiFiConnected();
  
  // Check for RFID card presence
  if (!rfid.PICC_IsNewCardPresent()) {
    return; // No card detected, continue loop
  }
  
  // Attempt to read card data
  if (!rfid.PICC_ReadCardSerial()) {
    if (DEBUG_MODE) {
      Serial.println("⚠️  Card detected but read failed - try again");
    }
    return;
  }
  
  // Extract card UID
  String cardUID = extractCardUID();
  
  // Prevent duplicate reads using cooldown timer
  if (isDuplicateRead(cardUID)) {
    rfid.PICC_HaltA();
    return;
  }
  
  // Update tracking variables
  updateReadState(cardUID);
  totalCardsScanned++;
  
  // Log card detection event
  logCardDetection(cardUID);
  
  // Validate card with backend server
  ValidationResult result = validateCardWithBackend(cardUID);
  
  // Display result to user
  displayValidationResult(result);
  
  // Clean up RFID state
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  
  // Brief pause before accepting next card
  delay(500);
}

// ============================================================================
// INITIALIZATION FUNCTIONS
// ============================================================================

void printStartupBanner() {
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════════════════╗");
  Serial.println("║     🚉 SMART TRAIN STATION - RFID READER          ║");
  Serial.println("║            Enhanced Version 2.0                    ║");
  Serial.println("╚════════════════════════════════════════════════════╝");
  Serial.println();
  Serial.print("📍 Station ID: ");
  Serial.println(STATION_ID);
  Serial.print("🏢 Station Name: ");
  Serial.println(STATION_NAME);
  Serial.print("🐛 Debug Mode: ");
  Serial.println(DEBUG_MODE ? "ENABLED" : "DISABLED");
  Serial.println("════════════════════════════════════════════════════");
  Serial.println();
}

void initializeLEDs() {
  Serial.println("💡 Initializing LED indicators...");
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  
  // LED test sequence
  digitalWrite(GREEN_LED, HIGH);
  delay(200);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  delay(200);
  digitalWrite(RED_LED, LOW);
  
  Serial.println("✅ LEDs initialized successfully");
}

void initializeSPI() {
  Serial.println("🔌 Initializing SPI bus...");
  SPI.begin(18, 19, 23, 5);  // SCK=18, MISO=19, MOSI=23, SS=5
  Serial.println("✅ SPI bus ready");
}

void initializeRFID() {
  Serial.println("📡 Initializing RC522 RFID reader...");
  rfid.PCD_Init();
  delay(500);
  
  // Verify RFID module is connected and responsive
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  
  if (version == 0x00 || version == 0xFF) {
    Serial.println("❌ CRITICAL ERROR: RC522 not detected!");
    Serial.println("   Check the following:");
    Serial.println("   • All wiring connections are secure");
    Serial.println("   • RC522 is powered with 3.3V (NOT 5V!)");
    Serial.println("   • SPI pins match the configuration");
    Serial.println("\n⚠️  System halted. Fix the issue and reset ESP32.");
    
    // Blink red LED rapidly to indicate critical error
    while (true) {
      digitalWrite(RED_LED, HIGH);
      delay(200);
      digitalWrite(RED_LED, LOW);
      delay(200);
    }
  }
  
  Serial.print("✅ RC522 detected - Firmware version: 0x");
  Serial.println(version, HEX);
  
  // Display additional RFID reader information
  if (DEBUG_MODE) {
    rfid.PCD_DumpVersionToSerial();
  }
}

void connectToWiFi() {
  Serial.println("\n📡 Connecting to WiFi...");
  Serial.print("   SSID: ");
  Serial.println(WIFI_SSID);
  
  // Disconnect first to clear any previous connection state
  WiFi.disconnect(true);
  delay(1000);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long startAttempt = millis();
  int dotCount = 0;
  
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startAttempt > WIFI_CONNECT_TIMEOUT) {
      Serial.println("\n❌ WiFi connection timeout!");
      Serial.println("   TROUBLESHOOTING STEPS:");
      Serial.println("   • Verify SSID and password are correct");
      Serial.println("   • Check if WiFi router is powered on");
      Serial.println("   • Ensure ESP32 is within WiFi range");
      Serial.println("   • Try moving ESP32 closer to router");
      Serial.println("   • Check if WiFi uses 2.4GHz (ESP32 doesn't support 5GHz)");
      Serial.print("   • Current MAC Address: ");
      Serial.println(WiFi.macAddress());
      Serial.println("\n⚠️  System will continue - cards CAN still be detected!");
      Serial.println("   (But validation will fail without WiFi)");
      return;
    }
    
    delay(500);
    Serial.print(".");
    dotCount++;
    
    if (dotCount % 50 == 0) {
      Serial.println();
    }
  }
  
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
  Serial.print("   Subnet: ");
  Serial.println(WiFi.subnetMask());
}

void printSystemReady() {
  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.println("║          ✅ SYSTEM READY - WAITING FOR CARDS       ║");
  Serial.println("╚════════════════════════════════════════════════════╝");
  Serial.println("Ready to scan RFID cards...\n");
}

// ============================================================================
// WIFI MANAGEMENT FUNCTIONS
// ============================================================================

void ensureWiFiConnected() {
  static unsigned long lastCheckTime = 0;
  static int reconnectAttempts = 0;
  
  // Check WiFi status every 10 seconds (not too frequently to avoid blocking)
  if (millis() - lastCheckTime < 10000) {
    return;
  }
  lastCheckTime = millis();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️  WiFi connection lost!");
    Serial.print("   Reconnection attempt #");
    Serial.println(++reconnectAttempts);
    
    // Blink red LED to indicate connection issue
    digitalWrite(RED_LED, HIGH);
    
    // Try to reconnect (non-blocking approach)
    WiFi.disconnect();
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    // Wait for connection with shorter timeout (don't block card reading for too long)
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
      delay(100);
      // Still process RFID even while reconnecting
      if (rfid.PICC_IsNewCardPresent()) {
        Serial.println("   (Card detected during reconnect - will process after WiFi attempt)");
      }
    }
    
    digitalWrite(RED_LED, LOW);
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ WiFi reconnected successfully");
      Serial.print("   IP: ");
      Serial.println(WiFi.localIP());
      reconnectAttempts = 0;
    } else {
      Serial.println("❌ Reconnection failed - cards will still be detected");
      Serial.println("   (Validations will fail until WiFi is restored)");
    }
  } else {
    // WiFi is connected - reset reconnect counter if it was previously high
    if (reconnectAttempts > 0) {
      reconnectAttempts = 0;
    }
  }
}

// ============================================================================
// RFID CARD PROCESSING FUNCTIONS
// ============================================================================

String extractCardUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0"; // Pad single hex digits with leading zero
    }
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

bool isDuplicateRead(String cardUID) {
  unsigned long currentTime = millis();
  
  if (cardUID == lastCardUID && 
      (currentTime - lastCardReadTime) < CARD_COOLDOWN) {
    if (DEBUG_MODE) {
      Serial.print("⏸️  Duplicate read ignored (cooldown: ");
      Serial.print(CARD_COOLDOWN - (currentTime - lastCardReadTime));
      Serial.println("ms remaining)");
    }
    return true;
  }
  
  return false;
}

void updateReadState(String cardUID) {
  lastCardUID = cardUID;
  lastCardReadTime = millis();
}

void logCardDetection(String cardUID) {
  Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("🎫 RFID CARD DETECTED");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.print("   Card UID: ");
  Serial.println(cardUID);
  Serial.print("   Station: ");
  Serial.print(STATION_NAME);
  Serial.print(" (");
  Serial.print(STATION_ID);
  Serial.println(")");
  Serial.print("   Timestamp: ");
  Serial.print(millis() / 1000);
  Serial.println("s since boot");
  Serial.print("   Total Cards Scanned: ");
  Serial.println(totalCardsScanned + 1);
}

// ============================================================================
// BACKEND VALIDATION FUNCTIONS
// ============================================================================

ValidationResult validateCardWithBackend(String cardUID) {
  ValidationResult result = {false, false, "", -1.0, 0, ""};
  
  // Check WiFi before attempting request
  if (WiFi.status() != WL_CONNECTED) {
    result.errorDetails = "No WiFi connection";
    Serial.println("❌ Cannot validate: WiFi not connected");
    return result;
  }
  
  // Try validation with retry mechanism
  for (int attempt = 1; attempt <= MAX_HTTP_RETRIES; attempt++) {
    if (attempt > 1) {
      Serial.print("🔄 Retry attempt ");
      Serial.print(attempt);
      Serial.print("/");
      Serial.println(MAX_HTTP_RETRIES);
      delay(1000 * attempt); // Exponential backoff
    }
    
    result = performHTTPValidation(cardUID);
    
    if (result.success) {
      if (result.approved) {
        successfulValidations++;
      } else {
        failedValidations++;
      }
      return result;
    }
  }
  
  // All retries failed
  Serial.println("❌ All retry attempts exhausted");
  failedValidations++;
  return result;
}

ValidationResult performHTTPValidation(String cardUID) {
  ValidationResult result = {false, false, "", -1.0, 0, ""};
  HTTPClient http;
  
  // Construct API URL
  String url = "http://" + String(RASPI_IP) + ":" + 
               String(RASPI_PORT) + String(API_ENDPOINT);
  
  Serial.println("\n📤 SENDING VALIDATION REQUEST");
  Serial.print("   URL: ");
  Serial.println(url);
  
  // Prepare JSON payload
  StaticJsonDocument<256> requestDoc;
  requestDoc["station_id"] = STATION_ID;
  requestDoc["card_uid"] = cardUID;
  
  String jsonPayload;
  serializeJson(requestDoc, jsonPayload);
  
  Serial.print("   Payload: ");
  Serial.println(jsonPayload);
  Serial.print("   Payload Size: ");
  Serial.print(jsonPayload.length());
  Serial.println(" bytes");
  
  // Configure and send HTTP request
  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT);
  
  unsigned long requestStartTime = millis();
  result.httpCode = http.POST(jsonPayload);
  unsigned long requestDuration = millis() - requestStartTime;
  
  Serial.println("\n📥 RESPONSE RECEIVED");
  Serial.print("   HTTP Code: ");
  Serial.print(result.httpCode);
  Serial.print(" (");
  Serial.print(getHTTPStatusDescription(result.httpCode));
  Serial.println(")");
  Serial.print("   Response Time: ");
  Serial.print(requestDuration);
  Serial.println("ms");
  
  // Process response
  if (result.httpCode > 0) {
    String responseBody = http.getString();
    
    if (DEBUG_MODE) {
      Serial.print("   Raw Response: ");
      Serial.println(responseBody);
    }
    
    // Parse JSON response
    StaticJsonDocument<512> responseDoc;
    DeserializationError parseError = deserializeJson(responseDoc, responseBody);
    
    if (parseError) {
      result.errorDetails = "JSON parse error: " + String(parseError.c_str());
      Serial.print("❌ ");
      Serial.println(result.errorDetails);
    } else {
      result.success = true;
      result.approved = responseDoc["approved"] | false;
      result.message = responseDoc["message"] | "No message";
      result.newBalance = responseDoc["new_balance"] | -1.0;
      
      // Log parsed response details
      logValidationResult(result);
    }
  } else {
    result.errorDetails = http.errorToString(result.httpCode);
    Serial.print("❌ HTTP Error: ");
    Serial.println(result.errorDetails);
  }
  
  http.end();
  return result;
}

void logValidationResult(ValidationResult result) {
  Serial.println("\n📋 VALIDATION RESULT");
  Serial.println("   ─────────────────────────────────────");
  
  if (result.approved) {
    Serial.println("   Status: ✅ ACCESS GRANTED");
  } else {
    Serial.println("   Status: ❌ ACCESS DENIED");
  }
  
  Serial.print("   Message: ");
  Serial.println(result.message);
  
  if (result.newBalance >= 0) {
    Serial.print("   New Balance: ₱");
    Serial.println(result.newBalance, 2);
  }
  
  Serial.println("   ─────────────────────────────────────");
  
  // Print statistics
  Serial.print("   Session Stats: ");
  Serial.print(successfulValidations);
  Serial.print(" approved, ");
  Serial.print(failedValidations);
  Serial.println(" denied");
}

String getHTTPStatusDescription(int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    case -1: return "Connection Failed";
    case -2: return "Send Header Failed";
    case -3: return "Send Payload Failed";
    case -4: return "Not Connected";
    case -5: return "Connection Lost";
    case -6: return "No Stream";
    case -7: return "No HTTP Server";
    case -8: return "Too Less RAM";
    case -9: return "Encoding";
    case -10: return "Stream Write";
    case -11: return "Timeout";
    default: return "Unknown";
  }
}

// ============================================================================
// USER FEEDBACK FUNCTIONS
// ============================================================================

void displayValidationResult(ValidationResult result) {
  if (!result.success) {
    // Request failed - show error pattern
    blinkErrorPattern();
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    return;
  }
  
  if (result.approved) {
    // Access granted - solid green
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, LOW);
    Serial.println("👍 Access granted - Gate should open");
  } else {
    // Access denied - solid red
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);
    Serial.println("👎 Access denied - Gate remains closed");
  }
  
  delay(LED_DISPLAY_TIME);
  
  // Clear LEDs
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

void blinkErrorPattern() {
  // Blink both LEDs alternately to indicate error
  for (int i = 0; i < 3; i++) {
    digitalWrite(RED_LED, HIGH);
    delay(150);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
    delay(150);
    digitalWrite(GREEN_LED, LOW);
  }
}

// ============================================================================
// DIAGNOSTIC FUNCTIONS (can be called via serial commands if needed)
// ============================================================================

void printDiagnostics() {
  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.println("║              SYSTEM DIAGNOSTICS                    ║");
  Serial.println("╚════════════════════════════════════════════════════╝");
  
  Serial.print("Uptime: ");
  Serial.print((millis() - systemStartTime) / 1000);
  Serial.println(" seconds");
  
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  }
  
  Serial.print("Total Cards Scanned: ");
  Serial.println(totalCardsScanned);
  Serial.print("Successful Validations: ");
  Serial.println(successfulValidations);
  Serial.print("Failed Validations: ");
  Serial.println(failedValidations);
  
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  
  Serial.println("════════════════════════════════════════════════════\n");
}