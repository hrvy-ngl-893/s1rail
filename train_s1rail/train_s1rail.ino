#define IR_PIN 33 
#define MOTOR_PIN 25 

void setup() { 
  Serial.begin(115200); 
  pinMode(IR_PIN, INPUT_PULLUP); pinMode(MOTOR_PIN, OUTPUT); 
  digitalWrite(MOTOR_PIN, LOW); 
} 

void loop() { 
  int irState = digitalRead(IR_PIN); 
  if (irState == LOW) { 
    Serial.println("IR: Black (object detected) → Motor OFF"); 
    digitalWrite(MOTOR_PIN, LOW); 
    } else { 
      Serial.println("IR: White (no object) → Motor ON"); 
      digitalWrite(MOTOR_PIN, HIGH); 
    } 
  delay(5); 
}