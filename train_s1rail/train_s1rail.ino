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

void setup() {
  Serial.begin(115200);
  pinMode(IR_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, HIGH); // default running (black)
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
      return;
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
}
