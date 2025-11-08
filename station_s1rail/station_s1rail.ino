#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 5
#define RST_PIN 22

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

void setup() {
  Serial.begin(115200);
  SPI.begin();            
  mfrc522.PCD_Init();    

  Serial.println("Checking for RFID reader...");
  delay(1000);


  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("⚠️  RFID reader not detected. Check wiring and power!");
    while (true) delay(1000);
  }

  Serial.print("✅ RFID Reader detected (MFRC522 version: 0x");
  Serial.print(v, HEX);
  Serial.println(")");
  Serial.println("Ready to scan cards...");
  Serial.println("------------------------------");
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  Serial.print("Card UID: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }

  Serial.println();

  unsigned long uidDec = 0;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uidDec = uidDec * 256 + mfrc522.uid.uidByte[i];
  }

  Serial.print("Card UID (Decimal): ");
  Serial.println(uidDec);
  Serial.println("------------------------------");

  delay(1000);
}