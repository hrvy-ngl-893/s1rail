%% SUMOBOT ALGORITHM FLOWCHARTS
%% Complete State Machine Visualization

%% ============================================================
%% MAIN STATE MACHINE
%% ============================================================
graph TD
Start([Power On])
Init[INIT: Initialize Sensors & Motors]
Countdown[5 Second Countdown]
Opening[OPENING_MOVE: 180° Spin + Charge]

    MainLoop{Main Loop<br/>Priority Check}

    BorderCheck{Border<br/>Detected?}
    EnemyCheck{Enemy<br/>Detected?}

    BorderEscape[BORDER_ESCAPE:<br/>Emergency Maneuver]
    Search[SEARCH:<br/>360° Rotation Scan]
    Attack[ATTACK:<br/>Circle-and-Strike]
    Push[PUSH:<br/>Aggressive Forward]

    Start --> Init
    Init --> Countdown
    Countdown --> Opening
    Opening --> MainLoop

    MainLoop --> BorderCheck
    BorderCheck -->|YES<br/>CRITICAL| BorderEscape
    BorderCheck -->|NO| EnemyCheck

    EnemyCheck -->|YES| Attack
    EnemyCheck -->|NO| Search

    Attack --> MainLoop
    Push --> MainLoop
    Search --> MainLoop
    BorderEscape --> MainLoop

    Attack -.->|Enemy <30cm| Push
    Push -.->|Enemy Lost| Search
    Search -.->|Enemy Found| Attack

    style Start fill:#90EE90
    style Init fill:#87CEEB
    style BorderEscape fill:#FF6B6B
    style Push fill:#FFD700
    style Attack fill:#FFA500
    style Search fill:#DDA0DD
    style MainLoop fill:#F0E68C

%% ============================================================
%% BORDER ESCAPE STATE (Detailed)
%% ============================================================
graph TD
BE_Start([Border Detected!])
BE_Check{Which IR<br/>Sensors?}

    BE_Front[Front Sensors<br/>FR or FL or Both]
    BE_Side[Side Sensors<br/>L or R]
    BE_Back[Back Sensor<br/>B]

    BE_RevFront[Reverse 100%<br/>300ms]
    BE_RevSide[Reverse 100%<br/>200ms]
    BE_FwdBack[Forward 100%<br/>300ms]

    BE_TurnCalc{Calculate<br/>Turn Direction}
    BE_Turn135[Turn 135° Away<br/>400ms]
    BE_Turn90Side[Turn 90° Away<br/>300ms]
    BE_Turn90Back[Turn 90°<br/>Random/Toward Enemy<br/>300ms]

    BE_Verify{All IR<br/>Clear?}
    BE_ExtendedReverse[Extended Reverse<br/>+200ms]
    BE_Done([Return to SEARCH])

    BE_Start --> BE_Check

    BE_Check -->|Front| BE_Front
    BE_Check -->|Side| BE_Side
    BE_Check -->|Back| BE_Back

    BE_Front --> BE_RevFront
    BE_Side --> BE_RevSide
    BE_Back --> BE_FwdBack

    BE_RevFront --> BE_TurnCalc
    BE_TurnCalc -->|FR Triggered| BE_Turn135
    BE_TurnCalc -->|FL Triggered| BE_Turn135
    BE_TurnCalc -->|Both Triggered| BE_Turn135

    BE_RevSide --> BE_Turn90Side
    BE_FwdBack --> BE_Turn90Back

    BE_Turn135 --> BE_Verify
    BE_Turn90Side --> BE_Verify
    BE_Turn90Back --> BE_Verify

    BE_Verify -->|YES| BE_Done
    BE_Verify -->|NO| BE_ExtendedReverse
    BE_ExtendedReverse --> BE_Verify

    style BE_Start fill:#FF6B6B
    style BE_Done fill:#90EE90
    style BE_Verify fill:#FFD700

%% ============================================================
%% ENEMY SEARCH STATE (Detailed)
%% ============================================================
graph TD
ES_Start([SEARCH State])
ES_Rotate[Rotate in Place<br/>Left: 75% Fwd<br/>Right: 75% Back]

    ES_ReadFront[Read Front<br/>Ultrasonic]
    ES_ReadSides[Read Left & Right<br/>Ultrasonic<br/>Every 200ms]

    ES_FrontCheck{Front Enemy<br/>10-150cm?}
    ES_SideCheck{Side Enemy<br/>Detected?}
    ES_BorderCheck{Border<br/>Detected?}

    ES_CalcAngle[Calculate Angle<br/>to Enemy]
    ES_RotateToEnemy[Rotate to<br/>Face Enemy]

    ES_ToAttack([Switch to ATTACK])
    ES_ToBorder([Switch to BORDER_ESCAPE])
    ES_Continue[Continue Rotating]

    ES_Start --> ES_Rotate
    ES_Rotate --> ES_ReadFront
    ES_ReadFront --> ES_ReadSides

    ES_ReadSides --> ES_BorderCheck
    ES_BorderCheck -->|YES| ES_ToBorder
    ES_BorderCheck -->|NO| ES_FrontCheck

    ES_FrontCheck -->|YES| ES_ToAttack
    ES_FrontCheck -->|NO| ES_SideCheck

    ES_SideCheck -->|YES| ES_CalcAngle
    ES_SideCheck -->|NO| ES_Continue

    ES_CalcAngle --> ES_RotateToEnemy
    ES_RotateToEnemy --> ES_ToAttack

    ES_Continue --> ES_ReadFront

    style ES_Start fill:#DDA0DD
    style ES_ToAttack fill:#FFA500
    style ES_ToBorder fill:#FF6B6B
    style ES_Continue fill:#87CEEB

%% ============================================================
%% ATTACK STATE - Circle and Strike (Detailed)
%% ============================================================
graph TD
AT_Start([ATTACK State<br/>Enemy Detected])
AT_BorderCheck{Border<br/>Detected?}
AT_DistCheck{Enemy<br/>Distance?}

    AT_Far[">30cm:<br/>Arc Approach"]
    AT_Close["≤30cm:<br/>Transition"]

    AT_ArcTurn[Arc Turn:<br/>Inner Wheel 60%<br/>Outer Wheel 90%]
    AT_ReadUltra[Read Front Ultrasonic<br/>Every 30ms]
    AT_AdjustArc[Adjust Arc<br/>Based on Position]

    AT_CloseDistance[Close Distance:<br/>Inner 65%<br/>Outer 80%]

    AT_ToPush([Switch to PUSH])
    AT_ToBorder([Switch to BORDER_ESCAPE])
    AT_ToSearch([Enemy Lost<br/>Switch to SEARCH])

    AT_LostCheck{Enemy Lost<br/>>1 second?}

    AT_Start --> AT_BorderCheck
    AT_BorderCheck -->|YES| AT_ToBorder
    AT_BorderCheck -->|NO| AT_DistCheck

    AT_DistCheck -->|Far| AT_Far
    AT_DistCheck -->|Close| AT_Close

    AT_Far --> AT_ArcTurn
    AT_ArcTurn --> AT_ReadUltra
    AT_ReadUltra --> AT_AdjustArc
    AT_AdjustArc --> AT_CloseDistance
    AT_CloseDistance --> AT_LostCheck

    AT_LostCheck -->|NO| AT_BorderCheck
    AT_LostCheck -->|YES| AT_ToSearch

    AT_Close --> AT_ToPush

    style AT_Start fill:#FFA500
    style AT_ToPush fill:#FFD700
    style AT_ToBorder fill:#FF6B6B
    style AT_ToSearch fill:#DDA0DD

%% ============================================================
%% PUSH STATE - Aggressive Forward (Detailed)
%% ============================================================
graph TD
PU_Start([PUSH State<br/>Enemy <30cm])
PU_MaxPower[Both Motors<br/>100% Forward]

    PU_ReadUltra[Read Front<br/>Ultrasonic Only]
    PU_ReadAllIR[Read ALL<br/>IR Sensors]

    PU_BorderCheck{Border<br/>Detected?}
    PU_EnemyCheck{Enemy<br/>Still Present?}

    PU_DistCheck{Enemy<br/>Distance?}

    PU_ToBorder([IMMEDIATE<br/>BORDER_ESCAPE])
    PU_ToSearch([Enemy Escaped<br/>Switch to SEARCH])
    PU_Continue[Continue<br/>Pushing]

    PU_DeadlockCheck{Deadlock?<br/>No Progress<br/>>2 sec}
    PU_Reposition[Reverse 300ms<br/>Turn 90°<br/>Re-engage]

    PU_Start --> PU_MaxPower
    PU_MaxPower --> PU_ReadAllIR
    PU_ReadAllIR --> PU_BorderCheck

    PU_BorderCheck -->|YES| PU_ToBorder
    PU_BorderCheck -->|NO| PU_ReadUltra

    PU_ReadUltra --> PU_EnemyCheck
    PU_EnemyCheck -->|NO| PU_ToSearch
    PU_EnemyCheck -->|YES| PU_DistCheck

    PU_DistCheck -->|"<50cm"| PU_DeadlockCheck
    PU_DistCheck -->|">50cm"| PU_ToSearch

    PU_DeadlockCheck -->|YES| PU_Reposition
    PU_DeadlockCheck -->|NO| PU_Continue

    PU_Reposition --> PU_Start
    PU_Continue --> PU_ReadAllIR

    style PU_Start fill:#FFD700
    style PU_ToBorder fill:#FF6B6B
    style PU_ToSearch fill:#DDA0DD
    style PU_Continue fill:#90EE90

%% ============================================================
%% OPENING MOVE STATE (Detailed)
%% ============================================================
graph TD
OP_Start([Match Start<br/>Countdown Complete])
OP_Spin[Spin 180°:<br/>Left 100% Fwd<br/>Right 100% Back<br/>Duration: 500ms]

    OP_SpinTimer{Timer<br/>Complete?}
    OP_BorderCheck1{Border<br/>During Spin?}

    OP_Charge[Charge Forward:<br/>Both Motors 100%<br/>Duration: 800ms]

    OP_ChargeTimer{Timer<br/>Complete?}
    OP_BorderCheck2{Border<br/>During Charge?}

    OP_ToSearch([Switch to SEARCH])
    OP_ToBorder([Switch to BORDER_ESCAPE])

    OP_Start --> OP_Spin
    OP_Spin --> OP_BorderCheck1
    OP_BorderCheck1 -->|YES| OP_ToBorder
    OP_BorderCheck1 -->|NO| OP_SpinTimer

    OP_SpinTimer -->|NO| OP_BorderCheck1
    OP_SpinTimer -->|YES| OP_Charge

    OP_Charge --> OP_BorderCheck2
    OP_BorderCheck2 -->|YES| OP_ToBorder
    OP_BorderCheck2 -->|NO| OP_ChargeTimer

    OP_ChargeTimer -->|NO| OP_BorderCheck2
    OP_ChargeTimer -->|YES| OP_ToSearch

    style OP_Start fill:#90EE90
    style OP_ToSearch fill:#DDA0DD
    style OP_ToBorder fill:#FF6B6B

%% ============================================================
%% SENSOR READING LOGIC
%% ============================================================
graph TD
SR_Start([Sensor Reading<br/>Loop Iteration])
SR_IRRead[Read ALL 5 IR Sensors<br/>Total Time: <1ms]

    SR_IRCheck{Any IR<br/>Triggered?}
    SR_IRProcess[Identify Which Sensors<br/>Calculate Border Direction]
    SR_SetFlag[Set BORDER_DETECTED Flag]

    SR_UltraCheck{Time for<br/>Ultrasonic?}
    SR_WhichUltra{Which Sensor<br/>This Cycle?}

    SR_Front[Read Front<br/>Ultrasonic<br/>~30ms]
    SR_Left[Read Left<br/>Ultrasonic<br/>~30ms]
    SR_Right[Read Right<br/>Ultrasonic<br/>~30ms]

    SR_ValidCheck{Valid<br/>Reading?}
    SR_Store[Store Distance<br/>Update Timestamp]
    SR_Discard[Discard Invalid<br/>Use Previous Value]

    SR_Done([Continue Main Loop])

    SR_Start --> SR_IRRead
    SR_IRRead --> SR_IRCheck

    SR_IRCheck -->|YES| SR_IRProcess
    SR_IRCheck -->|NO| SR_UltraCheck
    SR_IRProcess --> SR_SetFlag
    SR_SetFlag --> SR_Done

    SR_UltraCheck -->|NO| SR_Done
    SR_UltraCheck -->|YES| SR_WhichUltra

    SR_WhichUltra -->|Front| SR_Front
    SR_WhichUltra -->|Left| SR_Left
    SR_WhichUltra -->|Right| SR_Right

    SR_Front --> SR_ValidCheck
    SR_Left --> SR_ValidCheck
    SR_Right --> SR_ValidCheck

    SR_ValidCheck -->|YES| SR_Store
    SR_ValidCheck -->|NO| SR_Discard

    SR_Store --> SR_Done
    SR_Discard --> SR_Done

    style SR_Start fill:#87CEEB
    style SR_SetFlag fill:#FF6B6B
    style SR_Done fill:#90EE90
