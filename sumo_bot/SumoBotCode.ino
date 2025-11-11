/*
 * SUMOBOT COMPETITION CODE
 * Arduino Nano Implementation
 * 
 * Strategy: Aggressive Center Control with Circle-and-Strike
 * Sensors: 5x IR (border), 3x HC-SR04 (enemy detection)
 * Drive: Differential (2 motors, independent control)
 * 
 * Author: Competition Team
 * Version: 1.0
 * Last Updated: Competition Prep
 */

// ============================================================
// CONFIGURATION - TUNE THESE VALUES
// ============================================================

// --- MOTOR PINS ---
#define MOTOR_LEFT_FWD    5   // Left motor forward (PWM)
#define MOTOR_LEFT_BACK   6   // Left motor backward (PWM)
#define MOTOR_RIGHT_FWD   9   // Right motor forward (PWM)
#define MOTOR_RIGHT_BACK  10  // Right motor backward (PWM)

// --- IR SENSOR PINS (Border Detection) ---
#define IR_FRONT_RIGHT    A0  // Front-right IR sensor
#define IR_FRONT_LEFT     A1  // Front-left IR sensor
#define IR_RIGHT          A2  // Right side IR sensor
#define IR_LEFT           A3  // Left side IR sensor
#define IR_BACK           A4  // Back IR sensor

// --- ULTRASONIC SENSOR PINS (Enemy Detection) ---
#define ULTRA_FRONT_TRIG  2
#define ULTRA_FRONT_ECHO  3
#define ULTRA_LEFT_TRIG   4
#define ULTRA_LEFT_ECHO   7
#define ULTRA_RIGHT_TRIG  8
#define ULTRA_RIGHT_ECHO  11

// --- MOTOR SPEEDS (0-255) ---
#define SPEED_MAX         255   // Full speed (100%)
#define SPEED_SEARCH      180   // Search rotation (70%)
#define SPEED_ATTACK      200   // Attack approach (80%)
#define SPEED_PUSH        255   // Aggressive push (100%)
#define SPEED_ARC_INNER   153   // Arc turn inner wheel (60%)
#define SPEED_ARC_OUTER   230   // Arc turn outer wheel (90%)
#define SPEED_CLOSE_INNER 165   // Close distance inner (65%)
#define SPEED_CLOSE_OUTER 204   // Close distance outer (80%)

// --- TIMING DURATIONS (milliseconds) ---
#define OPENING_SPIN_TIME    500   // 180° rotation duration
#define OPENING_CHARGE_TIME  800   // Initial charge duration
#define BORDER_REVERSE_TIME  300   // Reverse from border
#define BORDER_TURN_TIME     400   // Turn away from border
#define BORDER_REV_SIDE      200   // Side sensor reverse
#define BORDER_TURN_SIDE     300   // Side sensor turn
#define BORDER_REV_BACK      300   // Back sensor forward
#define BORDER_TURN_BACK     300   // Back sensor turn

// --- DISTANCE THRESHOLDS (centimeters) ---
#define ENEMY_CLOSE       30    // Transition to PUSH mode
#define ENEMY_MEDIUM      80    // Circle attack range
#define ENEMY_FAR         150   // Max detection range
#define ENEMY_LOST        50    // Re-enter SEARCH if exceeds
#define ENEMY_MIN         10    // Minimum valid distance

// --- IR SENSOR THRESHOLD ---
// CALIBRATE THIS: Read values on black vs white, set halfway
#define IR_THRESHOLD      512   // Analog threshold (0-1023)

// --- ULTRASONIC TIMING ---
#define ULTRA_TIMEOUT     30000  // Microseconds max wait (30ms)
#define ULTRA_INTERVAL    50     // Time between front reads (ms)
#define ULTRA_SIDE_INTERVAL 200  // Time between side reads (ms)

// --- DEADLOCK DETECTION ---
#define DEADLOCK_TIME     2000   // Deadlock threshold (ms)

// ============================================================
// STATE DEFINITIONS
// ============================================================
enum State {
  STATE_INIT,
  STATE_OPENING_MOVE,
  STATE_SEARCH,
  STATE_ATTACK,
  STATE_PUSH,
  STATE_BORDER_ESCAPE
};

State currentState = STATE_INIT;

// ============================================================
// GLOBAL VARIABLES
// ============================================================

// --- Sensor Data ---
bool borderFrontRight = false;
bool borderFrontLeft = false;
bool borderRight = false;
bool borderLeft = false;
bool borderBack = false;
bool borderDetected = false;

int distanceFront = 999;  // cm
int distanceLeft = 999;
int distanceRight = 999;

// --- Timing ---
unsigned long previousMillis = 0;
unsigned long stateStartTime = 0;
unsigned long lastUltraFront = 0;
unsigned long lastUltraSide = 0;
unsigned long pushStartTime = 0;
int lastEnemyDistance = 999;

// --- State Flags ---
bool enemyDetected = false;
int enemyDirection = 0;  // 0=front, -1=left, 1=right

// ============================================================
// SETUP
// ============================================================
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  Serial.println("=================================");
  Serial.println("SUMOBOT INITIALIZING");
  Serial.println("=================================");
  
  // Configure motor pins
  pinMode(MOTOR_LEFT_FWD, OUTPUT);
  pinMode(MOTOR_LEFT_BACK, OUTPUT);
  pinMode(MOTOR_RIGHT_FWD, OUTPUT);
  pinMode(MOTOR_RIGHT_BACK, OUTPUT);
  
  // Configure IR sensor pins
  pinMode(IR_FRONT_RIGHT, INPUT);
  pinMode(IR_FRONT_LEFT, INPUT);
  pinMode(IR_RIGHT, INPUT);
  pinMode(IR_LEFT, INPUT);
  pinMode(IR_BACK, INPUT);
  
  // Configure ultrasonic pins
  pinMode(ULTRA_FRONT_TRIG, OUTPUT);
  pinMode(ULTRA_FRONT_ECHO, INPUT);
  pinMode(ULTRA_LEFT_TRIG, OUTPUT);
  pinMode(ULTRA_LEFT_ECHO, INPUT);
  pinMode(ULTRA_RIGHT_TRIG, OUTPUT);
  pinMode(ULTRA_RIGHT_ECHO, INPUT);
  
  // Stop all motors
  stopMotors();
  
  // Initial sensor test
  Serial.println("Testing sensors...");
  readAllIRSensors();
  Serial.print("IR Readings - FR:");
  Serial.print(analogRead(IR_FRONT_RIGHT));
  Serial.print(" FL:");
  Serial.print(analogRead(IR_FRONT_LEFT));
  Serial.print(" R:");
  Serial.print(analogRead(IR_RIGHT));
  Serial.print(" L:");
  Serial.print(analogRead(IR_LEFT));
  Serial.print(" B:");
  Serial.println(analogRead(IR_BACK));
  
  // 5-second countdown for match start
  Serial.println("=================================");
  Serial.println("MATCH STARTING IN 5 SECONDS");
  Serial.println("=================================");
  for(int i = 5; i > 0; i--) {
    Serial.print("Countdown: ");
    Serial.println(i);
    delay(1000);
  }
  Serial.println("GO! GO! GO!");
  
  // Transition to opening move
  currentState = STATE_OPENING_MOVE;
  stateStartTime = millis();
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  unsigned long currentMillis = millis();
  
  // CRITICAL: Always read IR sensors first (border safety)
  readAllIRSensors();
  
  // PRIORITY 1: Border detection overrides everything except BORDER_ESCAPE
  if(borderDetected && currentState != STATE_BORDER_ESCAPE) {
    Serial.println("!!! BORDER DETECTED - EMERGENCY ESCAPE !!!");
    currentState = STATE_BORDER_ESCAPE;
    stateStartTime = currentMillis;
  }
  
  // Read ultrasonic sensors (staggered to avoid blocking)
  readUltrasonicSensors(currentMillis);
  
  // Execute current state behavior
  switch(currentState) {
    case STATE_INIT:
      // Should never reach here (handled in setup)
      break;
      
    case STATE_OPENING_MOVE:
      executeOpeningMove(currentMillis);
      break;
      
    case STATE_SEARCH:
      executeSearch(currentMillis);
      break;
      
    case STATE_ATTACK:
      executeAttack(currentMillis);
      break;
      
    case STATE_PUSH:
      executePush(currentMillis);
      break;
      
    case STATE_BORDER_ESCAPE:
      executeBorderEscape(currentMillis);
      break;
  }
}

// ============================================================
// SENSOR READING FUNCTIONS
// ============================================================

void readAllIRSensors() {
  // Read all IR sensors (analog values)
  int frValue = analogRead(IR_FRONT_RIGHT);
  int flValue = analogRead(IR_FRONT_LEFT);
  int rValue = analogRead(IR_RIGHT);
  int lValue = analogRead(IR_LEFT);
  int bValue = analogRead(IR_BACK);
  
  // Detect white line (IR reflects more off white)
  borderFrontRight = (frValue > IR_THRESHOLD);
  borderFrontLeft = (flValue > IR_THRESHOLD);
  borderRight = (rValue > IR_THRESHOLD);
  borderLeft = (lValue > IR_THRESHOLD);
  borderBack = (bValue > IR_THRESHOLD);
  
  // Set global flag
  borderDetected = borderFrontRight || borderFrontLeft || 
                   borderRight || borderLeft || borderBack;
}

void readUltrasonicSensors(unsigned long currentMillis) {
  // Staggered reading to avoid blocking delays
  
  // Read front ultrasonic every ULTRA_INTERVAL ms
  if(currentMillis - lastUltraFront >= ULTRA_INTERVAL) {
    distanceFront = readUltrasonic(ULTRA_FRONT_TRIG, ULTRA_FRONT_ECHO);
    lastUltraFront = currentMillis;
  }
  
  // Read side ultrasonics every ULTRA_SIDE_INTERVAL ms
  if(currentMillis - lastUltraSide >= ULTRA_SIDE_INTERVAL) {
    distanceLeft = readUltrasonic(ULTRA_LEFT_TRIG, ULTRA_LEFT_ECHO);
    distanceRight = readUltrasonic(ULTRA_RIGHT_TRIG, ULTRA_RIGHT_ECHO);
    lastUltraSide = currentMillis;
  }
  
  // Update enemy detection status
  updateEnemyStatus();
}

int readUltrasonic(int trigPin, int echoPin) {
  // Send pulse
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Read echo with timeout
  long duration = pulseIn(echoPin, HIGH, ULTRA_TIMEOUT);
  
  // Calculate distance (speed of sound: 343 m/s)
  int distance = duration * 0.0343 / 2;
  
  // Validate reading
  if(distance < 2 || distance > 400) {
    return 999;  // Invalid reading
  }
  
  return distance;
}

void updateEnemyStatus() {
  // Check if enemy detected in valid range
  bool frontEnemy = (distanceFront >= ENEMY_MIN && distanceFront <= ENEMY_FAR);
  bool leftEnemy = (distanceLeft >= ENEMY_MIN && distanceLeft <= ENEMY_FAR);
  bool rightEnemy = (distanceRight >= ENEMY_MIN && distanceRight <= ENEMY_FAR);
  
  enemyDetected = frontEnemy || leftEnemy || rightEnemy;
  
  // Determine enemy direction
  if(frontEnemy) {
    enemyDirection = 0;  // Front
  } else if(leftEnemy && !rightEnemy) {
    enemyDirection = -1;  // Left
  } else if(rightEnemy && !leftEnemy) {
    enemyDirection = 1;   // Right
  } else if(leftEnemy && rightEnemy) {
    // Both sides - choose closer one
    enemyDirection = (distanceLeft < distanceRight) ? -1 : 1;
  }
}

// ============================================================
// MOTOR CONTROL FUNCTIONS
// ============================================================

void setMotors(int leftSpeed, int rightSpeed) {
  // Left motor
  if(leftSpeed > 0) {
    analogWrite(MOTOR_LEFT_FWD, leftSpeed);
    analogWrite(MOTOR_LEFT_BACK, 0);
  } else if(leftSpeed < 0) {
    analogWrite(MOTOR_LEFT_FWD, 0);
    analogWrite(MOTOR_LEFT_BACK, -leftSpeed);
  } else {
    analogWrite(MOTOR_LEFT_FWD, 0);
    analogWrite(MOTOR_LEFT_BACK, 0);
  }
  
  // Right motor
  if(rightSpeed > 0) {
    analogWrite(MOTOR_RIGHT_FWD, rightSpeed);
    analogWrite(MOTOR_RIGHT_BACK, 0);
  } else if(rightSpeed < 0) {
    analogWrite(MOTOR_RIGHT_FWD, 0);
    analogWrite(MOTOR_RIGHT_BACK, -rightSpeed);
  } else {
    analogWrite(MOTOR_RIGHT_FWD, 0);
    analogWrite(MOTOR_RIGHT_BACK, 0);
  }
}

void stopMotors() {
  setMotors(0, 0);
}

void moveForward(int speed) {
  setMotors(speed, speed);
}

void moveBackward(int speed) {
  setMotors(-speed, -speed);
}

void rotateClockwise(int speed) {
  setMotors(speed, -speed);
}

void rotateCounterClockwise(int speed) {
  setMotors(-speed, speed);
}

// ============================================================
// STATE EXECUTION FUNCTIONS
// ============================================================

void executeOpeningMove(unsigned long currentMillis) {
  unsigned long elapsed = currentMillis - stateStartTime;
  
  if(elapsed < OPENING_SPIN_TIME) {
    // Phase 1: Spin 180°
    Serial.println("[OPENING] Spinning 180 degrees...");
    rotateClockwise(SPEED_MAX);
    
  } else if(elapsed < OPENING_SPIN_TIME + OPENING_CHARGE_TIME) {
    // Phase 2: Charge forward to center
    Serial.println("[OPENING] Charging center!");
    moveForward(SPEED_MAX);
    
  } else {
    // Opening complete, transition to search
    Serial.println("[OPENING] Complete - Starting search");
    currentState = STATE_SEARCH;
    stateStartTime = currentMillis;
  }
}

void executeSearch(unsigned long currentMillis) {
  Serial.print("[SEARCH] Scanning... Front:");
  Serial.print(distanceFront);
  Serial.print(" Left:");
  Serial.print(distanceLeft);
  Serial.print(" Right:");
  Serial.println(distanceRight);
  
  // Check for enemy detection
  if(enemyDetected) {
    if(enemyDirection == 0) {
      // Enemy in front - immediate attack
      Serial.println("[SEARCH] ENEMY FRONT - ATTACKING!");
      currentState = STATE_ATTACK;
      stateStartTime = currentMillis;
      return;
    } else {
      // Enemy on side - rotate to face
      Serial.print("[SEARCH] Enemy detected on ");
      Serial.println(enemyDirection == -1 ? "LEFT" : "RIGHT");
      
      // Rotate toward enemy
      if(enemyDirection == -1) {
        rotateCounterClockwise(SPEED_SEARCH);
      } else {
        rotateClockwise(SPEED_SEARCH);
      }
      
      // Wait until roughly facing enemy, then attack
      // (simplified - in real match, use more sophisticated tracking)
      delay(200);
      currentState = STATE_ATTACK;
      stateStartTime = currentMillis;
      return;
    }
  }
  
  // No enemy - continue rotating
  rotateClockwise(SPEED_SEARCH);
}

void executeAttack(unsigned long currentMillis) {
  Serial.print("[ATTACK] Distance:");
  Serial.print(distanceFront);
  Serial.print("cm Direction:");
  Serial.println(enemyDirection);
  
  // Check if enemy lost
  if(!enemyDetected || distanceFront > ENEMY_LOST) {
    unsigned long lostTime = currentMillis - stateStartTime;
    if(lostTime > 1000) {
      Serial.println("[ATTACK] Enemy lost - returning to search");
      currentState = STATE_SEARCH;
      stateStartTime = currentMillis;
      return;
    }
  }
  
  // Check distance for mode transition
  if(distanceFront <= ENEMY_CLOSE) {
    // Close enough - switch to aggressive push
    Serial.println("[ATTACK] CLOSE RANGE - ENGAGING PUSH!");
    currentState = STATE_PUSH;
    stateStartTime = currentMillis;
    pushStartTime = currentMillis;
    return;
  }
  
  // Execute circle-and-strike approach
  if(distanceFront > ENEMY_MEDIUM) {
    // Far range - arc approach
    Serial.println("[ATTACK] Arc approach (far)");
    setMotors(SPEED_ARC_INNER, SPEED_ARC_OUTER);
  } else {
    // Medium range - close distance with slight arc
    Serial.println("[ATTACK] Closing distance (medium)");
    setMotors(SPEED_CLOSE_INNER, SPEED_CLOSE_OUTER);
  }
}

void executePush(unsigned long currentMillis) {
  Serial.print("[PUSH] MAXIMUM POWER! Distance:");
  Serial.println(distanceFront);
  
  // Check for deadlock (no progress for >2 seconds)
  unsigned long pushDuration = currentMillis - pushStartTime;
  if(pushDuration > DEADLOCK_TIME) {
    if(abs(distanceFront - lastEnemyDistance) < 5) {
      // Deadlock detected - reposition
      Serial.println("[PUSH] DEADLOCK! Repositioning...");
      moveBackward(SPEED_MAX);
      delay(300);
      rotateClockwise(SPEED_MAX);
      delay(300);
      currentState = STATE_ATTACK;
      stateStartTime = currentMillis;
      return;
    }
  }
  
  lastEnemyDistance = distanceFront;
  
  // Check if enemy escaped
  if(!enemyDetected || distanceFront > ENEMY_LOST) {
    Serial.println("[PUSH] Enemy escaped!");
    currentState = STATE_SEARCH;
    stateStartTime = currentMillis;
    return;
  }
  
  // PUSH AT MAXIMUM POWER
  moveForward(SPEED_PUSH);
}

void executeBorderEscape(unsigned long currentMillis) {
  unsigned long elapsed = currentMillis - stateStartTime;
  
  // Determine which sensors triggered
  bool frontBorder = borderFrontRight || borderFrontLeft;
  bool sideBorder = borderRight || borderLeft;
  bool backBorder = borderBack;
  
  Serial.print("[ESCAPE] Sensors - FR:");
  Serial.print(borderFrontRight);
  Serial.print(" FL:");
  Serial.print(borderFrontLeft);
  Serial.print(" R:");
  Serial.print(borderRight);
  Serial.print(" L:");
  Serial.print(borderLeft);
  Serial.print(" B:");
  Serial.println(borderBack);
  
  // Execute appropriate escape maneuver
  if(frontBorder) {
    // FRONT BORDER - Reverse and turn 135°
    if(elapsed < BORDER_REVERSE_TIME) {
      Serial.println("[ESCAPE] Reversing from front border...");
      moveBackward(SPEED_MAX);
    } else if(elapsed < BORDER_REVERSE_TIME + BORDER_TURN_TIME) {
      Serial.println("[ESCAPE] Turning 135° away...");
      // Determine turn direction
      if(borderFrontRight && !borderFrontLeft) {
        rotateCounterClockwise(SPEED_MAX);  // Turn left
      } else if(borderFrontLeft && !borderFrontRight) {
        rotateClockwise(SPEED_MAX);  // Turn right
      } else {
        // Both triggered - turn based on stronger signal
        int frValue = analogRead(IR_FRONT_RIGHT);
        int flValue = analogRead(IR_FRONT_LEFT);
        if(frValue > flValue) {
          rotateCounterClockwise(SPEED_MAX);
        } else {
          rotateClockwise(SPEED_MAX);
        }
      }
    } else {
      // Escape complete - verify clearance
      if(!borderDetected) {
        Serial.println("[ESCAPE] Clear! Returning to search");
        currentState = STATE_SEARCH;
        stateStartTime = currentMillis;
      } else {
        // Still detecting border - extend escape
        Serial.println("[ESCAPE] Still at border - extending maneuver");
        stateStartTime = currentMillis - BORDER_REVERSE_TIME;
      }
    }
    
  } else if(sideBorder) {
    // SIDE BORDER - Reverse and turn 90°
    if(elapsed < BORDER_REV_SIDE) {
      Serial.println("[ESCAPE] Reversing from side border...");
      moveBackward(SPEED_MAX);
    } else if(elapsed < BORDER_REV_SIDE + BORDER_TURN_SIDE) {
      Serial.println("[ESCAPE] Turning 90° away...");
      if(borderLeft) {
        rotateClockwise(SPEED_MAX);  // Turn right
      } else {
        rotateCounterClockwise(SPEED_MAX);  // Turn left
      }
    } else {
      // Escape complete
      if(!borderDetected) {
        Serial.println("[ESCAPE] Clear! Returning to search");
        currentState = STATE_SEARCH;
        stateStartTime = currentMillis;
      } else {
        stateStartTime = currentMillis - BORDER_REV_SIDE;
      }
    }
    
  } else if(backBorder) {
    // BACK BORDER - Move forward and turn
    if(elapsed < BORDER_REV_BACK) {
      Serial.println("[ESCAPE] Moving forward from back border...");
      moveForward(SPEED_MAX);
    } else if(elapsed < BORDER_REV_BACK + BORDER_TURN_BACK) {
      Serial.println("[ESCAPE] Turning 90°...");
      rotateClockwise(SPEED_MAX);  // Random direction
    } else {
      // Escape complete
      if(!borderDetected) {
        Serial.println("[ESCAPE] Clear! Returning to search");
        currentState = STATE_SEARCH;
        stateStartTime = currentMillis;
      } else {
        stateStartTime = currentMillis - BORDER_REV_BACK;
      }
    }
  }
}

// ============================================================
// END OF CODE
// ============================================================