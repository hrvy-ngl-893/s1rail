/*
 * ESP8266 Station RFID Reader
 * Reads RFID cards and sends validation requests to Raspberry Pi backend
 * 
 * Hardware:
 * - ESP8266 (NodeMCU/Wemos D1 Mini)
 * - MFRC522 RFID Reader
 * - Green LED (optional)
 * - Red LED (optional)
 * 
 * Wiring:
 * MFRC522 -> ESP8266
 *   SDA/SS -> D8 (GPIO15)
 *   SCK    -> D5 (GPIO14)
 *   MOSI   -> D7 (GPIO13)
 *   MISO   -> D6 (GPIO12)
 *   IRQ    -> Not connected
 *   GND    -> GND
 *   RST    -> D3 (GPIO0)
 *   3.3V   -> 3.3V
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>

// ============================================================================
// 🔧 CONFIGURATION - CHANGE THESE VALUES FOR YOUR SETUP
// ============================================================================

// 📡 WiFi Configuration
const char* WIFI_SSID = "YourWiFiName";          // ⚠️ CHANGE THIS
const char* WIFI_PASSWORD = "YourWiFiPassword";  // ⚠️ CHANGE THIS

// 🖥️ Raspberry Pi Backend Configuration
const char* RASPI_IP = "192.168.1.100";          // ⚠️ CHANGE THIS to your Pi's IP
const int RASPI_PORT = 8000;
const char* API_ENDPOINT = "/api/station/validate";

// 🏢 Station Configuration
const char* STATION_ID = "station-a";            // ⚠️ CHANGE THIS (station-a, station-b, etc.)
const char* STATION_NAME = "Downtown Station";   // ⚠️ CHANGE THIS

// 📌 Hardware Pin Configuration
#define SS_PIN D8      // SDA/SS pin (GPIO15) - ⚠️ Change if needed
#define RST_PIN D3     // RST pin (GPIO0)     - ⚠️ Change if needed
#define GREEN_LED D1   // Green LED pin       - ⚠️ Change if needed (optional)
#define RED_LED D2     // Red LED pin         - ⚠️ Change if needed (optional)

// ⏱️ Timing Configuration
#define LED_DISPLAY_TIME 2000  // How long to show LED (milliseconds)
#define CARD_COOLDOWN 1500     // Prevent multiple reads of same card

// ============================================================================
// END OF CONFIGURATION
// ============================================================================

MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClient wifiClient;

unsigned long lastCardRead = 0;
String lastCardUID = "";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("==================================================");
  Serial.println("   🚉 SMART TRAIN STATION - RFID READER");
  Serial.println("==================================================");
  Serial.print("Station ID: ");
  Serial.println(STATION_ID);
  Serial.print("Station Name: ");
  Serial.println(STATION_NAME);
  Serial.println("==================================================\n");
  
  // Setup LEDs (optional)
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  
  // Initialize SPI and RFID reader
  SPI.begin();
  mfrc522.PCD_Init();
  
  Serial.println("📡 Checking for RFID reader...");
  delay(1000);
  
  // Verify RFID reader is connected
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("⚠️  RFID reader not detected. Check wiring and power!");
    while (true) {
      // Blink red LED to indicate error
      digitalWrite(RED_LED, HIGH);
      delay(500);
      digitalWrite(RED_LED, LOW);
      delay(500);
    }
  }
  
  Serial.print("✅ RFID Reader detected (MFRC522 version: 0x");
  Serial.print(v, HEX);
  Serial.println(")");
  
  // Connect to WiFi
  connectWiFi();
  
  Serial.println("\n✅ System ready!");
  Serial.println("Ready to scan cards...");
  Serial.println("------------------------------\n");
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi disconnected. Reconnecting...");
    connectWiFi();
  }
  
  // Check for RFID card
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }
  
  // Read card UID
  String cardUID = getCardUID();
  
  // Prevent duplicate reads
  unsigned long currentTime = millis();
  if (cardUID == lastCardUID && (currentTime - lastCardRead) < CARD_COOLDOWN) {
    Serial.println("⚠️  Duplicate read ignored");
    mfrc522.PICC_HaltA();
    return;
  }
  
  lastCardUID = cardUID;
  lastCardRead = currentTime;
  
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.print("🎫 Card detected: ");
  Serial.println(cardUID);
  Serial.print("📍 Station: ");
  Serial.println(STATION_NAME);
  
  // Send validation request to backend
  bool approved = validateCard(cardUID);
  
  // Show result with LEDs
  if (approved) {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, LOW);
  } else {
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
  }
  
  delay(LED_DISPLAY_TIME);
  
  // Turn off LEDs
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  
  // Halt PICC
  mfrc522.PICC_HaltA();
  
  delay(500); // Small delay before next card
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

String getCardUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uid += "0";
    }
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

bool validateCard(String cardUID) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ No WiFi connection");
    return false;
  }
  
  HTTPClient http;
  
  // Build API URL
  String url = "http://" + String(RASPI_IP) + ":" + String(RASPI_PORT) + String(API_ENDPOINT);
  
  Serial.print("📤 Sending validation request to: ");
  Serial.println(url);
  
  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");
  
  // ⚠️ THIS IS THE PAYLOAD FORMAT YOUR BACKEND EXPECTS
  // Create JSON payload (NO TIMESTAMP - backend generates it)
  StaticJsonDocument<256> doc;
  doc["station_id"] = STATION_ID;
  doc["card_uid"] = cardUID;
  
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  Serial.print("📦 Payload: ");
  Serial.println(jsonPayload);
  
  // Send POST request
  int httpCode = http.POST(jsonPayload);
  
  Serial.print("📥 Response code: ");
  Serial.println(httpCode);
  
  bool approved = false;
  
  if (httpCode > 0) {
    String response = http.getString();
    Serial.print("📄 Response: ");
    Serial.println(response);
    
    // Parse response JSON
    StaticJsonDocument<512> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (!error) {
      approved = responseDoc["approved"];
      const char* message = responseDoc["message"];
      float balance = responseDoc["new_balance"] | -1.0;
      
      if (approved) {
        Serial.println("✅ ACCESS GRANTED");
        Serial.print("💰 New Balance: ₱");
        if (balance >= 0) {
          Serial.println(balance, 2);
        } else {
          Serial.println("N/A");
        }
      } else {
        Serial.println("❌ ACCESS DENIED");
      }
      
      if (message) {
        Serial.print("📝 Message: ");
        Serial.println(message);
      }
    } else {
      Serial.print("❌ JSON parse error: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.print("❌ HTTP request failed: ");
    Serial.println(http.errorToString(httpCode));
  }
  
  http.end();
  return approved;
}