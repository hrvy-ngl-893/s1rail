# Sumobot Algorithm Documentation

## Table of Contents
1. [Overview](#overview)
2. [Design Philosophy](#design-philosophy)
3. [State Machine Architecture](#state-machine-architecture)
4. [Sensor Configuration](#sensor-configuration)
5. [Behavior States](#behavior-states)
6. [Timing & Performance](#timing--performance)
7. [Edge Case Handling](#edge-case-handling)
8. [Tuning Guide](#tuning-guide)

---

## Overview

This sumobot is designed for **aggressive center control** with a **seek-and-destroy** strategy. The algorithm prioritizes:
- **Fast reaction times** to border detection (safety first)
- **Active enemy search** when no target is detected
- **Angular attack approach** to destabilize opponents
- **Efficient sensor polling** optimized for Arduino Nano

### Competition Specifications
- **Arena**: 1.5m diameter circle, black surface, 2.5cm white border
- **Bot constraints**: 30cm × 30cm max, <3kg, 12V max
- **Match format**: Best of 3 rounds, 2 minutes each

---

## Design Philosophy

### Priority Hierarchy
The bot operates on a strict priority system:

1. **CRITICAL: Border Detection** (highest priority)
   - Overrides all other behaviors
   - ~13cm stopping distance from edge
   
2. **HIGH: Enemy Engagement** 
   - Attack when enemy detected
   - Maintain aggression while staying safe
   
3. **MEDIUM: Active Search**
   - Rotate in place when no enemy found
   - Never sit idle
   
4. **LOW: Recovery & Positioning**
   - Return to center control after maneuvers

### Core Strategy: "Aggressive Center Control"
- **Opening Move**: Fast 180° spin to face center, immediate charge
- **Positioning**: Maintain center ring control
- **Attack Style**: Circle to opponent's side, attack from angle
- **Defense**: Quick border escapes with minimal retreat

---

## State Machine Architecture

The bot operates in **6 primary states**:

```
INIT → OPENING_MOVE → SEARCH → ATTACK → PUSH → BORDER_ESCAPE
         ↓              ↑         ↓        ↓          ↓
         └──────────────┴─────────┴────────┴──────────┘
                    (Main Loop with Priority Checks)
```

### State Descriptions

| State | Purpose | Duration | Exit Condition |
|-------|---------|----------|----------------|
| **INIT** | Sensor initialization, calibration | 1-2 sec | Auto-advance to OPENING_MOVE |
| **OPENING_MOVE** | 180° spin + charge center | ~1 sec | Completes rotation |
| **SEARCH** | Spin 360° to find enemy | Continuous | Enemy detected OR border detected |
| **ATTACK** | Position for angular strike | 0.5-1 sec | Reached attack angle OR border detected |
| **PUSH** | Maximum forward push | Continuous | Enemy lost OR border detected |
| **BORDER_ESCAPE** | Emergency retreat + turn | 0.3-0.5 sec | Clear of border |

---

## Sensor Configuration

### IR Sensors (5x) - Border Detection
**Purpose**: Detect white border line on black surface

| Sensor | Position | Coverage |
|--------|----------|----------|
| **FR** (Front Right) | Front-right corner, 2cm from edge | 45° forward-right |
| **FL** (Front Left) | Front-left corner, 2cm from edge | 45° forward-left |
| **R** (Right) | Right side center, 2cm from edge | 90° right |
| **L** (Left) | Left side center, 2cm from edge | 90° left |
| **B** (Back) | Rear center, 2cm from edge | 180° rear |

**Detection Logic**:
- IR reflects more off white than black
- Threshold tuning required per arena lighting
- Reading time: <1ms per sensor

### Ultrasonic Sensors (3x) - Enemy Detection
**Hardware**: HC-SR04
- **Range**: 2cm - 400cm (practical: 10cm - 150cm)
- **Beam angle**: ~15° cone
- **Reading time**: ~30ms per sensor

| Sensor | Position | Angle | Purpose |
|--------|----------|-------|---------|
| **Front** | Center front | 0° | Primary enemy detection |
| **Left** | Left side | 90° | Flank detection |
| **Right** | Right side | -90° | Flank detection |

**Distance Thresholds**:
- **Close range (<30cm)**: Aggressive push mode
- **Medium range (30-80cm)**: Circle-and-strike mode
- **Far range (>80cm)**: Fast approach mode

---

## Behavior States

### 1. INIT (Initialization)
**Purpose**: Prepare sensors and motors for competition

**Actions**:
1. Initialize serial communication (115200 baud)
2. Set all motor pins to OUTPUT
3. Calibrate IR sensor thresholds
4. Test ultrasonic sensors (single ping each)
5. Enter 5-second countdown for match start

**Exit**: Auto-transition to OPENING_MOVE after countdown

---

### 2. OPENING_MOVE
**Purpose**: Immediately gain center control

**Actions**:
1. **Spin 180°**: 
   - Left motor: 100% forward
   - Right motor: 100% backward
   - Duration: ~500ms (tunable)
   
2. **Charge Center**:
   - Both motors: 100% forward
   - Duration: ~800ms
   - Goal: Reach center before opponent

**Border Check**: Active during entire sequence
**Exit**: Timer expires → SEARCH state

---

### 3. SEARCH (Active Enemy Search)
**Purpose**: Locate enemy through 360° rotation

**Actions**:
1. **Rotate in place**:
   - Left motor: 75% forward
   - Right motor: 75% backward
   - Continuous rotation clockwise
   
2. **Scan with Front Ultrasonic**:
   - Read every 50ms
   - Log distances to serial
   
3. **Check side ultrasonics** periodically (every 200ms)

**Decision Logic**:
```
IF Front ultrasonic detects enemy (10-150cm):
    → ATTACK state
ELSE IF Left/Right ultrasonic detects enemy:
    → Rotate to face enemy → ATTACK state
ELSE IF Border detected:
    → BORDER_ESCAPE state
ELSE:
    → Continue rotating
```

**Exit**: Enemy detected OR border detected

---

### 4. ATTACK (Circle-and-Strike)
**Purpose**: Position for angular push from opponent's side

**Strategy**: "Hybrid Tracking"
- Turn slowly while reading Front ultrasonic to maintain lock
- Approach in arc rather than straight line
- Attack from 30-45° angle to opponent's side

**Actions**:

#### Phase 1: Approach Arc (if enemy >30cm)
```
1. Calculate desired angle (30° offset from direct line)
2. Arc turn:
   - Inner wheel: 60% speed
   - Outer wheel: 90% speed
3. Monitor Front ultrasonic every 30ms
4. Adjust arc if enemy moves
```

#### Phase 2: Close Distance
```
IF enemy distance >30cm:
    - Both motors: 80% forward
    - Slight arc maintained (65% inner, 80% outer)
ELSE IF enemy distance ≤30cm:
    → Transition to PUSH state
```

**Border Check**: CRITICAL - check every loop iteration
**Exit**: 
- Enemy <30cm → PUSH state
- Border detected → BORDER_ESCAPE state
- Enemy lost for >1 second → SEARCH state

---

### 5. PUSH (Aggressive Forward Push)
**Purpose**: Maximum power shove to eject opponent

**Actions**:
1. **Full power forward**:
   - Both motors: 100% speed
   - No turning, pure forward thrust
   
2. **Maintain ultrasonic lock**:
   - Front sensor only (faster reads)
   - Verify enemy still in front (<50cm)
   
3. **Continuous border monitoring**:
   - ALL IR sensors checked every loop
   - Instant response to border detection

**Decision Logic**:
```
IF any IR sensor detects border:
    → IMMEDIATE transition to BORDER_ESCAPE
ELSE IF Front ultrasonic loses enemy (>50cm or no reading):
    → SEARCH state (enemy escaped)
ELSE:
    → Continue pushing
```

**Exit**: Border detected OR enemy lost

---

### 6. BORDER_ESCAPE (Emergency Maneuver)
**Purpose**: Rapid retreat from ring edge with directional turn-away

**Actions**:

#### Step 1: Identify Border Location
```
Check which IR sensors triggered:
- Front sensors (FR, FL): Moving forward toward border
- Side sensors (L, R): Drifting sideways toward border  
- Back sensor (B): Reversing toward border
```

#### Step 2: Execute Escape Maneuver

**If Front sensors (FR or FL or both)**:
```
1. REVERSE at 100% speed for 300ms
2. TURN 135° away from detected side:
   - If FR triggered: Turn left (CCW)
   - If FL triggered: Turn right (CW)
   - If BOTH triggered: Turn based on stronger signal
3. Duration: ~400ms
```

**If Side sensors (L or R)**:
```
1. REVERSE at 100% speed for 200ms
2. TURN 90° away from border:
   - If L triggered: Turn right
   - If R triggered: Turn left
3. Duration: ~300ms
```

**If Back sensor (B)**:
```
1. FORWARD at 100% speed for 300ms
2. TURN 90° (random direction or toward last known enemy)
3. Duration: ~300ms
```

#### Step 3: Verify Clearance
```
Re-check ALL IR sensors
IF still detecting border:
    → Repeat escape with longer reverse duration
ELSE:
    → Return to SEARCH state
```

**Exit**: All IR sensors clear → SEARCH state

---

## Timing & Performance

### Sensor Polling Strategy

**Goal**: Minimize latency while avoiding blocking delays

#### IR Sensors (Border Detection)
- **Priority**: HIGHEST
- **Polling rate**: Every loop iteration (~5-10ms)
- **Method**: Direct `digitalRead()` - non-blocking
- **Total read time**: <1ms for all 5 sensors

#### Ultrasonic Sensors (Enemy Detection)
- **Priority**: HIGH
- **Polling rate**: Staggered reads
- **Method**: Non-blocking with `millis()` timing

**Optimal Polling Schedule**:
```
Loop 1 (0ms):    Read Front ultrasonic, Read all IR
Loop 2 (30ms):   Process Front data, Read all IR
Loop 3 (60ms):   Read Left ultrasonic, Read all IR
Loop 4 (90ms):   Process Left data, Read all IR
Loop 5 (120ms):  Read Right ultrasonic, Read all IR
Loop 6 (150ms):  Process Right data, Read all IR
Loop 7 (180ms):  Read Front ultrasonic again...
```

**Why Staggered?**
- HC-SR04 requires 30ms per reading
- Reading all 3 sequentially = 90ms delay = DANGEROUS
- Staggering keeps loop time <35ms = better border reaction

### Arduino Nano Constraints

**Memory Budget**:
- **Flash (Program)**: 30,720 bytes available
  - Estimated usage: ~8-10KB for this algorithm
  - Plenty of headroom for optimizations
  
- **SRAM (Variables)**: 2,048 bytes available
  - Sensor readings: ~50 bytes
  - State variables: ~100 bytes
  - Stack overhead: ~200 bytes
  - **Safe margin**: Use <1KB total

**CPU Performance**:
- Clock speed: 16 MHz
- Loop iteration: ~5-10ms with ultrasonic reads
- Border reaction time: <15ms (acceptable)

---

## Edge Case Handling

### 1. Both Robots Reach Center Simultaneously
**Scenario**: Head-on collision in center ring

**Detection**:
- Front ultrasonic shows enemy <10cm
- Both motors pushing at 100%
- Enemy not moving (distance constant for >500ms)

**Response**:
```
IF deadlock detected:
    1. REVERSE at 100% for 200ms
    2. TURN 45° (random direction)
    3. RE-ENGAGE with circle attack
```

### 2. Enemy Pushes You Toward Border
**Scenario**: Opponent has superior pushing power

**Detection**:
- IR sensors detect border
- Front ultrasonic shows enemy <30cm (enemy is pushing)

**Response**:
```
PRIORITY: Border escape takes precedence
1. BORDER_ESCAPE maneuver (reverse + turn)
2. Use turn to rotate PAST enemy (not away)
3. Counter-attack from new angle
```

**Key**: Turn 135° positions you alongside enemy, not fleeing

### 3. Enemy Disappears Mid-Attack
**Scenario**: Opponent performs evasive maneuver

**Detection**:
- Was in ATTACK or PUSH state
- Ultrasonic suddenly reads >100cm or no echo
- Persists for >500ms

**Response**:
```
1. STOP forward motion
2. Quick 360° scan (SEARCH state)
3. If enemy not found after full rotation:
   → Return to center and resume search
```

### 4. Multiple IR Sensors Trigger (Corner Case)
**Scenario**: Bot is at corner of ring, multiple borders detected

**Detection**:
- 2+ IR sensors active simultaneously
- Example: FR + R both triggered

**Response**:
```
1. REVERSE at 100% (immediate)
2. Calculate average angle of all triggered sensors
3. TURN 180° opposite direction
4. Duration: Extended (500ms reverse, 600ms turn)
```

**Why**: Corner = critical danger, need aggressive escape

### 5. Ultrasonic False Readings
**Scenario**: Sensor returns erratic data (common with HC-SR04)

**Detection**:
- Reading >400cm (out of range)
- Reading <2cm when no obstacle visible
- Rapid fluctuations (>50cm change in 30ms)

**Response**:
```
1. Discard reading
2. Use previous valid reading for decision
3. Log warning to serial
4. Continue with last known enemy position
```

### 6. Stuck Against Opponent (Wheels Slip)
**Scenario**: Pushing enemy but no movement (deadlock)

**Detection**:
- PUSH state active for >2 seconds
- Front ultrasonic shows constant distance
- Motors at 100% but no progress

**Response**:
```
1. REVERSE 300ms
2. TURN 90° (shift angle of attack)
3. RE-ENGAGE with fresh approach angle
```

### 7. No Enemy Detected After 10 Seconds
**Scenario**: Both robots searching, haven't found each other

**Detection**:
- SEARCH state for >10 seconds
- All ultrasonics return no valid readings

**Response**:
```
1. STOP rotation
2. MOVE FORWARD 500ms (expand search radius)
3. RESUME 360° rotation
4. Repeat spiral pattern
```

---

## Tuning Guide

### Variables to Adjust for Optimal Performance

#### Motor Speeds
```cpp
// Located at top of Arduino code
#define SPEED_MAX 255        // Full speed (100%)
#define SPEED_SEARCH 180     // Search rotation (70%)
#define SPEED_ATTACK 200     // Attack approach (80%)
#define SPEED_PUSH 255       // Aggressive push (100%)
```

**Tuning Tips**:
- Higher SPEED_SEARCH = faster enemy acquisition
- Lower SPEED_ATTACK = better tracking accuracy
- Always keep SPEED_PUSH at maximum

#### Timing Durations
```cpp
#define OPENING_SPIN_TIME 500    // 180° rotation duration (ms)
#define OPENING_CHARGE_TIME 800  // Initial charge duration (ms)
#define BORDER_REVERSE_TIME 300  // Reverse duration (ms)
#define BORDER_TURN_TIME 400     // Turn duration (ms)
```

**Tuning Process**:
1. Test OPENING_SPIN_TIME on flat surface
   - Bot should rotate exactly 180°
   - Adjust ±50ms if overshooting/undershooting
   
2. Test BORDER_REVERSE_TIME on practice ring
   - Should clear border by 5-10cm
   - Too short = re-trigger, too long = loses positioning

#### Distance Thresholds
```cpp
#define ENEMY_CLOSE 30       // Transition to PUSH mode (cm)
#define ENEMY_MEDIUM 80      // Circle attack range (cm)
#define ENEMY_FAR 150        // Max detection range (cm)
#define ENEMY_LOST 50        // Re-enter SEARCH if exceeds (cm)
```

**Tuning Tips**:
- Increase ENEMY_CLOSE if you want earlier aggression
- Decrease ENEMY_MEDIUM for tighter circles
- Adjust based on ultrasonic reliability in your arena

#### IR Sensor Thresholds
```cpp
#define IR_THRESHOLD 512     // Border detection threshold (0-1023)
```

**Calibration Procedure**:
1. Place bot on BLACK surface
2. Read all IR sensors → note values (should be LOW)
3. Place bot on WHITE line
4. Read all IR sensors → note values (should be HIGH)
5. Set threshold halfway between: `(BLACK + WHITE) / 2`

**Example**:
- Black surface: IR reads 200
- White line: IR reads 800
- Threshold: (200 + 800) / 2 = 500

---

## Testing Checklist

### Phase 1: Sensor Validation
- [ ] All 5 IR sensors trigger on white line
- [ ] IR sensors DO NOT trigger on black surface
- [ ] Front ultrasonic detects objects 10-150cm
- [ ] Left ultrasonic detects objects at 90°
- [ ] Right ultrasonic detects objects at 90°
- [ ] Serial monitor shows real-time sensor data

### Phase 2: Basic Maneuvers
- [ ] Opening move: spins 180° correctly
- [ ] Opening move: charges forward after spin
- [ ] Search: rotates smoothly in place
- [ ] Border escape: reverses when IR triggers
- [ ] Border escape: turns away from border
- [ ] Push: moves forward at full speed

### Phase 3: Integrated Behaviors
- [ ] Detects stationary object at 50cm and attacks
- [ ] Transitions from ATTACK to PUSH at 30cm
- [ ] Escapes border during PUSH state
- [ ] Re-acquires enemy after border escape
- [ ] Completes circle attack on moving target
- [ ] Handles corner cases (multiple IR triggers)

### Phase 4: Competition Simulation
- [ ] 5-second countdown works
- [ ] Wins against passive opponent (pushes out)
- [ ] Survives against aggressive opponent (escapes borders)
- [ ] Recovers from being pushed toward edge
- [ ] Maintains center control throughout match
- [ ] No freezing or infinite loops observed

---

## Competition Day Checklist

### Pre-Match
1. **Battery Check**: Fully charged, secure connections
2. **Sensor Test**: Quick IR calibration on actual ring
3. **Weight Verification**: Ensure <3kg with battery
4. **Size Check**: Confirm <30cm × 30cm
5. **Code Upload**: Latest tuned version on Arduino

### Between Rounds
1. **Inspect Wheels**: Check for debris/damage
2. **Battery Swap**: Fresh battery if voltage drops
3. **Quick Sensor Test**: Verify IR still calibrated
4. **Review Serial Logs**: Check for unexpected behavior
5. **Strategy Adjustment**: Modify based on opponent

### If Things Go Wrong
| Problem | Quick Fix |
|---------|-----------|
| Bot drives out immediately | Increase IR_THRESHOLD |
| Bot won't find enemy | Decrease SPEED_SEARCH |
| Bot freezes at border | Check IR wiring, recalibrate |
| Losing push battles | Increase SPEED_PUSH (already max?) |
| Overshoots 180° spin | Decrease OPENING_SPIN_TIME |

---

## Algorithm Strengths & Weaknesses

### Strengths ✅
- **Fast border reaction** (<15ms from detection to action)
- **Active search** (never sits idle)
- **Aggressive positioning** (center control)
- **Angular attacks** (destabilizes opponents)
- **Robust edge case handling**
- **Memory efficient** (<1KB RAM usage)

### Weaknesses ⚠️
- **Ultrasonic limitations**: 30ms read time can miss fast opponents
- **HC-SR04 noise**: False readings require filtering
- **Predictable search**: Clockwise rotation can be exploited
- **No opponent profiling**: Doesn't adapt to enemy strategy
- **Corner vulnerability**: Multiple IR triggers = longer escape

### Future Improvements 🚀
1. **Adaptive search patterns**: Randomize rotation direction
2. **Enemy behavior learning**: Track opponent patterns
3. **Sensor fusion**: Combine IR + ultrasonic for better tracking
4. **Gyroscope integration**: Precise angle control
5. **Variable attack patterns**: Unpredictable maneuvers

---

## Conclusion

This algorithm provides a **solid foundation for competitive sumo robotics** with emphasis on:
- **Safety** (border detection priority)
- **Aggression** (center control, active search)
- **Reliability** (tested edge cases, robust error handling)
- **Tunability** (easily adjustable parameters)

**Success depends on**:
1. Proper sensor calibration
2. Sufficient motor power
3. Practice and tuning
4. Quick thinking during competition

**Good luck in the arena! 🤖🏆**

---

*Document Version: 1.0*  
*Last Updated: Competition Prep*  
*Status: Ready for Implementation*