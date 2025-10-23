#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

#define IR_PIN 33
#define MOTOR_PIN 25

unsigned long pauseStart = 0;
unsigned long pauseDuration = 0;
bool isPaused = false;

int whiteCount = 0;           // 1 = 5s, next 3 = 3s, then repeat
bool ignoreWhite = false;     // ignore consecutive white readings

bool stableState = LOW;       // current stable reading
bool lastStableState = LOW;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // must stay stable for 50ms

// ======= MPU6050 Variables =======
Adafruit_MPU6050 mpu;
unsigned long lastMPUPrint = 0;
const unsigned long mpuInterval = 1000; // print every 1s

void setup() {
  Serial.begin(115200);
  pinMode(IR_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, HIGH); // default running (black)

  // ======= MPU6050 Initialization =======
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip!");
    while (1) delay(10);
  }
  Serial.println("MPU6050 found!");
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  delay(100);
}

void loop() {
  unsigned long currentMillis = millis();
  int reading = digitalRead(IR_PIN);

  // ======= Debounce / Stability Check =======
  static int lastReading = LOW;
  if (reading != lastReading) {
    lastDebounceTime = currentMillis;
    lastReading = reading;
  }

  // Only confirm a state change if stable for debounceDelay
  if ((currentMillis - lastDebounceTime) > debounceDelay) {
    stableState = reading;
  }

  // ======= Pause Handling =======
  if (isPaused) {
    if (currentMillis - pauseStart >= pauseDuration) {
      isPaused = false;
      ignoreWhite = false;
      Serial.println("Pause done → Motor resumes.");
      digitalWrite(MOTOR_PIN, HIGH);
    } else {
      digitalWrite(MOTOR_PIN, LOW);
      // MPU readings still allowed while paused
    }
  }

  // ======= Detect black → white transition =======
  if (!ignoreWhite && stableState == HIGH && lastStableState == LOW) {
    whiteCount++;
    if (whiteCount > 4) whiteCount = 1;

    pauseDuration = (whiteCount == 1) ? 5000 : 3000;
    pauseStart = currentMillis;
    isPaused = true;
    ignoreWhite = true;

    Serial.print("White detected → Pausing for ");
    Serial.print(pauseDuration / 1000);
    Serial.println(" seconds.");
    digitalWrite(MOTOR_PIN, LOW);
  } 
  else if (!isPaused && stableState == LOW) {
    digitalWrite(MOTOR_PIN, HIGH);
  }

  lastStableState = stableState;

  // ======= MPU6050 Data Every 1 Second =======
  if (currentMillis - lastMPUPrint >= mpuInterval) {
    lastMPUPrint = currentMillis;
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    Serial.println("=== MPU6050 Readings ===");
    Serial.print("Accel X: "); Serial.print(a.acceleration.x); Serial.print(" m/s^2, ");
    Serial.print("Y: "); Serial.print(a.acceleration.y); Serial.print(", Z: "); Serial.println(a.acceleration.z);
    Serial.print("Gyro  X: "); Serial.print(g.gyro.x); Serial.print(" rad/s, ");
    Serial.print("Y: "); Serial.print(g.gyro.y); Serial.print(", Z: "); Serial.println(g.gyro.z);
    Serial.print("Temp: "); Serial.print(temp.temperature); Serial.println(" °C");
    Serial.println("=========================");
  }
}
