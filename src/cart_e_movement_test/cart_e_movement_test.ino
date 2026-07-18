/*
 * ============================================================
 *  CART-E MOVEMENT TEST v2 — now with smart turning!
 * ============================================================
 *  Board  : NodeMCU ESP32 (30-pin) on the CYTRON ROBO ESP32
 *
 *  WHAT'S NEW IN v2:
 *   - New distance bands:  closer than 27 cm  -> REVERSE
 *                          27 to 33 cm        -> STOP (perfect!)
 *                          33 to 150 cm       -> FORWARD (follow)
 *                          beyond 150 cm      -> STOP + buzzer (lost)
 *   - Smarter turning: we turn TOWARD THE PERSON, not toward
 *     empty space. When the person turns a corner, they vanish
 *     from the front eye and appear in a corner eye — so the
 *     trolley turns that way until the front eye finds them again!
 *
 *  IMPORTANT PHYSICAL SETUP for the new turning to work:
 *   - Angle U2 and U3 OUTWARD about 30-45 degrees, so the three
 *     front eyes make a wide fan:   \  |  /
 *     If they all point straight ahead, they see the same thing
 *     and turning can never be detected!
 *
 *  HOW TO TEST (open Serial Monitor at 115200!):
 *   1. Flip the Robo ESP32 power switch on.
 *   2. Press your latching button -> LEDs turn GREEN.
 *   3. Walk in front:  away = follow, stand = stop at ~30 cm,
 *      closer = reverse, walk around a corner = trolley turns
 *      to chase you, run away = stop + beep.
 *
 *  LIBRARY NEEDED: only Adafruit NeoPixel (for the LEDs).
 * ============================================================
 */

#include <Adafruit_NeoPixel.h> // The rainbow LEDs

/* ============================================================
 *  PIN MAP
 * ============================================================ */

//   U1 front       -> GROVE 1      : trig D16, echo D17
//   U2 front-left  -> GROVE 3      : trig D26, echo D25
//   U3 front-right -> GROVE 5      : trig D33, echo D32
//   U4 back-left   -> servo S pins : trig D4,  echo D18
//   U5 back-right  -> servo S pins : trig D5,  echo D19
//   U6 back        -> breakout     : trig D2,  echo D36 (label "VP")
const int TRIG_PIN[6] = { 16, 26, 33,  4,  5,  2 };
const int ECHO_PIN[6] = { 17, 25, 32, 18, 19, 36 };

const int U1_FRONT       = 0;  // front eye  - watches the person
const int U2_FRONT_LEFT  = 1;  // corner eye - angled out ~30-45deg left
const int U3_FRONT_RIGHT = 2;  // corner eye - angled out ~30-45deg right
const int U4_BACK_LEFT   = 3;  // bodyguard  - back-left corner
const int U5_BACK_RIGHT  = 4;  // bodyguard  - back-right corner
const int U6_BACK        = 5;  // bodyguard  - straight behind us

// Motors (built-in driver): A HIGH + B LOW = forward
const int M1_A = 12;   // Motor 1 (LEFT wheel)
const int M1_B = 13;
const int M2_A = 14;   // Motor 2 (RIGHT wheel)
const int M2_B = 27;

// CAREFUL: the ESP32 has NO pin 24 (numbering skips 24 and 28-31)!
// Writing to "pin 24" goes nowhere — that's a silent buzzer bug.
// The Robo ESP32 buzzer is on D23. Want it quiet during testing?
// Use the little MUTE switch on the board instead.
const int BUZZER     = 23;  // built-in + external in parallel
const int NEOPIXELS  = 15;  // onboard 2 + external stick in parallel
const int NUM_PIXELS = 8;

const int SW_POWER = 35;    // latching button (LOW = ON)
                            // onboard button "1" shares this pin

/* ============================================================
 *  DISTANCE RULES (cm) — TUNE THESE during testing!
 *
 *  The follow bands (built from FOLLOW_DIST 30 +/- DEAD_ZONE 3):
 *      0 ......27 : REVERSE  (person too close, back away)
 *     27 ......33 : STOP     (perfect distance — the sweet spot)
 *     33 .....150 : FORWARD  (chase the person)
 *    150 ......   : LOST     (stop + buzzer — person gone)
 * ============================================================ */
const int FOLLOW_DIST   = 30;   // middle of the STOP sweet spot
const int DEAD_ZONE     = 3;    // sweet spot = 30 +/- 3 = 27 to 33
const int LOST_DIST     = 150;  // beyond this = person is gone
const int PERSON_RANGE  = 60;   // corner eye sees the person within this
const int GUARD_DIST    = 10;   // bodyguards: closer = DANGER
const int SQUEEZE_DIST  = 5;    // sides really scraping something

enum Move { STOP, FORWARD, REVERSE, TURN_LEFT, TURN_RIGHT, LOST, WAIT };

// Names for printing, in the SAME order as the enum above
const char* MOVE_NAME[] =
  { "STOP", "FORWARD", "REVERSE", "TURN LEFT", "TURN RIGHT",
    "LOST - wait for me!", "WAIT - confused" };

Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXELS, NEO_GRB + NEO_KHZ800);

bool trolleyAwake  = false;
long lastFrontDist = FOLLOW_DIST;

// A little stopwatch so we print at a readable speed
// (every 300 ms) even though the loop runs much faster.
unsigned long lastPrintAt = 0;

/* ============================================================
 *  SETUP — runs once
 * ============================================================ */
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== CART-E MOVEMENT TEST v2 ===");
  Serial.println("Press the latching button to start!");

  for (int i = 0; i < 6; i++) {
    pinMode(TRIG_PIN[i], OUTPUT);
    pinMode(ECHO_PIN[i], INPUT);
    digitalWrite(TRIG_PIN[i], LOW);
  }
  pinMode(M1_A, OUTPUT);  pinMode(M1_B, OUTPUT);
  pinMode(M2_A, OUTPUT);  pinMode(M2_B, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(SW_POWER, INPUT);   // board already has the pull-up

  stopMotors();

  pixels.begin();
  pixels.setBrightness(60);
  showColor(255, 0, 0);       // red = sleeping
}

/* ============================================================
 *  LOOP — runs forever
 * ============================================================ */
void loop() {

  bool switchOn = switchIsOn();

  if (!trolleyAwake) {
    if (switchOn) {
      trolleyAwake = true;
      showColor(0, 255, 0);
      beep(1, 100);
      Serial.println(">>> AWAKE! Following mode ON");
    }
    return;
  }

  if (!switchOn) {
    stopMotors();
    showColor(255, 0, 0);
    beep(3, 200);
    trolleyAwake = false;
    Serial.println(">>> EMERGENCY STOP (button released)");
    return;
  }

  // --- Read ALL 6 eyes (front 3 for deciding, back 3 for safety) ---
  long dFront     = readDistanceCm(U1_FRONT);
  long dLeft      = readDistanceCm(U2_FRONT_LEFT);
  long dRight     = readDistanceCm(U3_FRONT_RIGHT);
  long dBackLeft  = readDistanceCm(U4_BACK_LEFT);
  long dBackRight = readDistanceCm(U5_BACK_RIGHT);
  long dBack      = readDistanceCm(U6_BACK);

  // --- Think, check safety, then move ---
  Move wanted = chooseMove(dFront, dLeft, dRight);
  Move safe   = safetyCheck(wanted, dBackLeft, dBackRight, dBack);
  doMove(safe);

  // --- Show the trolley's "thoughts" every 300 ms ---
  if (millis() - lastPrintAt > 300) {
    lastPrintAt = millis();
    Serial.print("U1:");  Serial.print(dFront);
    Serial.print(" U2:"); Serial.print(dLeft);
    Serial.print(" U3:"); Serial.print(dRight);
    Serial.print(" U4:"); Serial.print(dBackLeft);
    Serial.print(" U5:"); Serial.print(dBackRight);
    Serial.print(" U6:"); Serial.print(dBack);
    Serial.print("  ->  ");
    Serial.print(MOVE_NAME[safe]);
    if (safe != wanted) {
      Serial.print("  (bodyguards vetoed: ");
      Serial.print(MOVE_NAME[wanted]);
      Serial.print(")");
    }
    Serial.println();
  }

  lastFrontDist = dFront;
  delay(30);
}

/* ============================================================
 *  CHOOSE MOVE — the trolley's brain
 *
 *  THE BIG IDEA for turning: follow the person, not the gaps!
 *  When the person walks around a corner, they DISAPPEAR from
 *  the front eye and APPEAR in a corner eye. So:
 *    front eye lost them + left  eye sees them -> TURN LEFT
 *    front eye lost them + right eye sees them -> TURN RIGHT
 *  We keep turning until the front eye finds them again, then
 *  the normal forward/stop/reverse logic takes over.
 * ============================================================ */
Move chooseMove(long dFront, long dLeft, long dRight) {

  // Three simple yes/no questions the trolley asks itself:
  bool personFront = (dFront <= LOST_DIST);     // someone within 150 cm ahead?
  bool personLeft  = (dLeft  <  PERSON_RANGE);  // someone in the left  fan?
  bool personRight = (dRight <  PERSON_RANGE);  // someone in the right fan?

  // CASE 12: person vanished in ONE step — they ran off too fast!
  if (lastFrontDist < FOLLOW_DIST + 10 && dFront > LOST_DIST) {
    return LOST;
  }

  // The person IS in front — normal following (cases 1, 2, 3):
  if (personFront) {
    if (dFront < FOLLOW_DIST - DEAD_ZONE) return REVERSE;  // under 27
    if (dFront > FOLLOW_DIST + DEAD_ZONE) return FORWARD;  // 33 to 150
    return STOP;                                           // 27-33 sweet spot
  }

  // From here on, the front eye sees NOBODY. Where did they go?

  // CASE 11: BOTH corner eyes see something — two people passing?
  // A narrow aisle? Too confusing — safer to wait than to guess.
  if (personLeft && personRight) {
    return WAIT;
  }

  // CASE 5: they appeared in the LEFT fan -> chase them left!
  if (personLeft) {
    return TURN_LEFT;
  }

  // CASE 6: they appeared in the RIGHT fan -> chase them right!
  if (personRight) {
    return TURN_RIGHT;
  }

  // CASE 4: nobody anywhere — abandoned. Stop, buzz, wait.
  return LOST;
}

/* ============================================================
 *  SAFETY CHECK — bodyguards veto dangerous moves (cases 7-10)
 * ============================================================ */
Move safetyCheck(Move wanted, long dBackLeft, long dBackRight, long dBack) {

  // CASE 7: can't reverse — something behind us!
  if (wanted == REVERSE && dBack < GUARD_DIST) {
    beep(2, 80);
    return STOP;
  }

  // CASE 8: can't turn left — back-left corner would hit the rack!
  if (wanted == TURN_LEFT && dBackLeft < GUARD_DIST) {
    beep(2, 80);
    return STOP;
  }

  // CASE 9: can't turn right — same on the other side.
  if (wanted == TURN_RIGHT && dBackRight < GUARD_DIST) {
    beep(2, 80);
    return STOP;
  }

  // CASE 10: squeezed against something while going forward.
  if (wanted == FORWARD &&
      (dBackLeft < SQUEEZE_DIST || dBackRight < SQUEEZE_DIST)) {
    beep(3, 60);
    return STOP;
  }

  return wanted;
}

/* ============================================================
 *  DO MOVE — wheels + lights
 *  (Turn directions below are YOUR calibrated ones — the spin
 *   turn, both wheels opposite ways, rotates on the spot.)
 * ============================================================ */
void doMove(Move m) {
  switch (m) {
    case FORWARD:
      showColor(0, 255, 0);  goForward();                break;
    case REVERSE:
      showColor(0, 255, 0);  goBackward();               break;
    case TURN_LEFT:
      showColor(0, 255, 0);  motorM1(-1); motorM2(+1);   break;
    case TURN_RIGHT:
      showColor(0, 255, 0);  motorM1(+1); motorM2(-1);   break;
    case LOST:
      stopMotors();
      showColor(255, 150, 0);          // yellow: "wait for me!"
      beep(1, 300);
      break;
    case WAIT:
      stopMotors();
      showColor(255, 150, 0);          // yellow: thinking...
      break;
    case STOP:
    default:
      stopMotors();
      showColor(0, 255, 0);            // green: happy, resting
      break;
  }
}

/* ============================================================
 *  MOTOR HELPERS — Cytron truth table:
 *  A HIGH + B LOW = forward, A LOW + B HIGH = backward,
 *  both LOW = brake. (+1 forward, -1 backward, 0 stop)
 *  Wheel spins the wrong way? Swap its 2 wires at the terminal!
 * ============================================================ */
void motorM1(int dir) {
  digitalWrite(M1_A, dir > 0 ? HIGH : LOW);
  digitalWrite(M1_B, dir < 0 ? HIGH : LOW);
}
void motorM2(int dir) {
  digitalWrite(M2_A, dir > 0 ? HIGH : LOW);
  digitalWrite(M2_B, dir < 0 ? HIGH : LOW);
}
void goForward()  { motorM1(+1); motorM2(+1); }
void goBackward() { motorM1(-1); motorM2(-1); }
void stopMotors() { motorM1(0);  motorM2(0);  }

/* ============================================================
 *  READ DISTANCE — send a click, time the echo.
 *  Sound travels ~0.034 cm per microsecond, there AND back,
 *  so we divide by 2. No echo heard = 999 (far away).
 * ============================================================ */
long readDistanceCm(int sensor) {
  digitalWrite(TRIG_PIN[sensor], LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN[sensor], HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN[sensor], LOW);

  long duration = pulseIn(ECHO_PIN[sensor], HIGH, 25000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

/* ============================================================
 *  SMALL HELPERS — switch, lights, beeps
 * ============================================================ */

// Latching switch: true = ON. Double-checks after 30 ms
// because real switches "bounce" when they change.
bool switchIsOn() {
  static bool lastState = false;
  bool nowState = (digitalRead(SW_POWER) == LOW);
  if (nowState != lastState) {
    delay(30);
    nowState = (digitalRead(SW_POWER) == LOW);
    lastState = nowState;
  }
  return nowState;
}

// Paint every NeoPixel (onboard + external) the same colour.
void showColor(int r, int g, int b) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

// Beep n times using tone() — the piezo needs a musical note.
void beep(int n, int ms) {
  for (int i = 0; i < n; i++) {
    tone(BUZZER, 2000);
    delay(ms);
    noTone(BUZZER);
    if (i < n - 1) delay(ms);
  }
}
