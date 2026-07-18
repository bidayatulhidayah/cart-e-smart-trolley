/*
 * ============================================================
 *  CART-E : THE HUMAN-FOLLOWING SMART TROLLEY  (FULL VERSION)
 * ============================================================
 *  Board  : NodeMCU ESP32 (30-pin) on the CYTRON ROBO ESP32
 *
 *  This is the COMPLETE program:
 *    MOVEMENT BRAIN v2 (tested & calibrated)  +  RFID SHOPPING
 *
 *  MOVEMENT (same as the tested movement code):
 *    closer than 27 cm  -> REVERSE
 *    27 to 33 cm        -> STOP (the sweet spot)
 *    33 to 150 cm       -> FORWARD (follow the person)
 *    beyond 150 cm      -> LOST: stop + buzzer
 *    Turning: we chase THE PERSON, not empty space. When they
 *    turn a corner they vanish from the front eye and appear in
 *    a corner eye -> turn that way until the front eye finds them.
 *
 *  SHOPPING (new in this version):
 *    - Tap an RFID sticker on the reader -> "boop!", item price
 *      shows on the OLED for 20 seconds, then back to the total.
 *    - Unknown sticker -> polite "ask staff" message.
 *    - The total survives emergency stops — nothing is lost.
 *
 *  PHYSICAL SETUP REMINDERS:
 *    - U2 and U3 angled OUTWARD 30-45 degrees, like a fan \ | /
 *    - Buzzer MUTE switch ON (and remember: ESP32 has NO pin 24 —
 *      the buzzer lives on D23!)
 *    - PN532 DIP switches set to I2C mode
 *    - RFID + OLED share I2C: SDA = D21, SCL = D22
 *      (Grove Port 2 and the Maker Port are that same I2C line)
 *
 *  LIBRARIES YOU NEED (install from Library Manager):
 *    - Adafruit PN532        (for the RFID reader)
 *    - Adafruit SSD1306      (for the OLED screen)
 *    - Adafruit GFX Library  (helper for the screen)
 *    - Adafruit NeoPixel     (for the rainbow LEDs)
 *    (Adafruit BusIO installs automatically as a dependency —
 *     say YES when the Library Manager asks!)
 * ============================================================
 */

#include <Wire.h>              // Lets the ESP32 talk on the I2C "party line"
#include <Adafruit_PN532.h>    // The RFID reader
#include <Adafruit_GFX.h>      // Drawing helper for the screen
#include <Adafruit_SSD1306.h>  // The OLED screen
#include <Adafruit_NeoPixel.h> // The rainbow LEDs

/* ============================================================
 *  PIN MAP  (identical to the tested movement code —
 *  no wires need to move!)
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
// The buzzer lives on D23. Want it quiet? Use the board's MUTE switch.
const int BUZZER     = 23;  // built-in + external in parallel
const int NEOPIXELS  = 15;  // onboard 2 + external stick in parallel
const int NUM_PIXELS = 8;

const int SW_POWER = 35;    // latching button (LOW = ON)
                            // onboard button "1" shares this pin

// (RFID reader and OLED both plug into I2C: SDA = D21, SCL = D22.)

/* ============================================================
 *  DISTANCE RULES (cm) — the calibrated numbers!
 *
 *  Follow bands (built from FOLLOW_DIST 30 +/- DEAD_ZONE 3):
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

/* ============================================================
 *  RFID PRICE LIST
 *  Each sticker has a unique ID (UID). We keep a little price
 *  list here, like a mini shop database.
 *  HOW TO ADD YOUR OWN ITEMS:
 *   1. Upload this code and open the Serial Monitor (115200).
 *   2. Tap a sticker — its UID is printed.
 *   3. Copy the UID into this list with a name and price.
 * ============================================================ */
struct Item {
  uint8_t uid[7];    // the sticker's ID number
  uint8_t uidLen;    // how many bytes the ID has (usually 4 or 7)
  const char *name;  // what the item is
  float price;       // how much it costs (RM)
};

Item shopItems[] = {
  { {0xDE, 0xAD, 0xBE, 0xEF}, 4, "Apple juice", 3.50 },
  { {0x12, 0x34, 0x56, 0x78}, 4, "Biscuits",    5.90 },
  { {0xAA, 0xBB, 0xCC, 0xDD}, 4, "Toothpaste",  8.20 },
};
const int NUM_ITEMS = sizeof(shopItems) / sizeof(shopItems[0]);

/* ============================================================
 *  GLOBAL OBJECTS AND VARIABLES
 * ============================================================ */

// RFID reader on I2C. The -1, -1 means "IRQ and RESET pins are
// not wired" — the library officially supports this for I2C mode.
Adafruit_PN532 rfid(-1, -1, &Wire);

Adafruit_SSD1306 oled(128, 64, &Wire, -1);          // 128x64 OLED screen

Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXELS, NEO_GRB + NEO_KHZ800);

bool  trolleyAwake   = false;  // is the latching button switched ON?
float totalPrice     = 0.0;    // running total of the shopping
long  lastFrontDist  = FOLLOW_DIST; // remembers last U1 reading

// Timer for showing an item price for 20 seconds on the OLED.
// We use millis() (a stopwatch) instead of delay() (freezing),
// so the trolley keeps driving while the screen counts down.
unsigned long itemShownAt   = 0;
bool          showingItem   = false;
const unsigned long SHOW_ITEM_MS = 20000; // 20 seconds

// Print the robot's "thoughts" every 300 ms (not every loop)
unsigned long lastPrintAt = 0;

/* ============================================================
 *  SETUP — runs ONCE when the trolley is switched on
 * ============================================================ */
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== CART-E FULL VERSION starting up... ===");

  for (int i = 0; i < 6; i++) {
    pinMode(TRIG_PIN[i], OUTPUT);
    pinMode(ECHO_PIN[i], INPUT);
    digitalWrite(TRIG_PIN[i], LOW);
  }
  pinMode(M1_A, OUTPUT);  pinMode(M1_B, OUTPUT);
  pinMode(M2_A, OUTPUT);  pinMode(M2_B, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(SW_POWER, INPUT);   // board already has the pull-up

  stopMotors();               // always start standing still!

  pixels.begin();
  pixels.setBrightness(60);
  showColor(255, 0, 0);       // red = sleeping

  Wire.begin(21, 22);         // start the I2C party line

  // --- Wake up the OLED screen ---
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found - check the wiring!");
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("CART-E");
  oled.println("Press button to start");
  oled.display();

  // --- Wake up the RFID reader ---
  rfid.begin();
  if (!rfid.getFirmwareVersion()) {
    Serial.println("PN532 not found - check the wiring!");
  } else {
    rfid.SAMConfig();         // "SAM config" = get ready to read cards
    Serial.println("RFID reader ready!");
  }
}

/* ============================================================
 *  LOOP — runs again and again, forever (very fast!)
 * ============================================================ */
void loop() {

  /* ---- STEP 0 + 1: The ONE button (power + emergency) ----
   * Latched closed = ON.  Released open = OFF.
   * Turning it off while driving IS the emergency stop!        */
  bool switchOn = switchIsOn();

  if (!trolleyAwake) {
    if (switchOn) {                       // just latched ON — wake up!
      trolleyAwake = true;
      showColor(0, 255, 0);
      beep(1, 100);
      showTotal();
      Serial.println(">>> AWAKE! Following mode ON");
    }
    return;  // still off — skip everything below and check again
  }

  if (!switchOn) {                        // released while driving —
    emergencyStop();                      // STOP EVERYTHING, go to sleep
    return;
  }

  /* ---- STEP 2: Read ALL 6 eyes ---- */
  long dFront     = readDistanceCm(U1_FRONT);
  long dLeft      = readDistanceCm(U2_FRONT_LEFT);
  long dRight     = readDistanceCm(U3_FRONT_RIGHT);
  long dBackLeft  = readDistanceCm(U4_BACK_LEFT);
  long dBackRight = readDistanceCm(U5_BACK_RIGHT);
  long dBack      = readDistanceCm(U6_BACK);

  /* ---- STEP 3: Think, check safety, then move ---- */
  Move wanted = chooseMove(dFront, dLeft, dRight);
  Move safe   = safetyCheck(wanted, dBackLeft, dBackRight, dBack);
  doMove(safe);

  /* ---- STEP 4: Any shopping scanned? ---- */
  checkRFID();

  /* ---- STEP 5: Has the 20-second price display finished? ---- */
  updateScreenTimer();

  /* ---- STEP 6: Show the trolley's "thoughts" every 300 ms ---- */
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

  lastFrontDist = dFront;   // remember for the "too fast" check
  delay(30);                // tiny rest so sensors don't argue
}

/* ============================================================
 *  CHOOSE MOVE — the trolley's brain  (movement v2, calibrated)
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
 *  (Turn directions are the CALIBRATED ones from testing —
 *   the spin turn, both wheels opposite ways.)
 * ============================================================ */
void doMove(Move m) {
  switch (m) {
    case FORWARD:
      showColor(0, 255, 0);  goForward();                break;
    case REVERSE:
      showColor(0, 255, 0);  goBackward();               break;
    case TURN_LEFT:
      showColor(0, 255, 0);  motorM1(+1); motorM2(-1);   break;
    case TURN_RIGHT:
      showColor(0, 255, 0);  motorM1(-1); motorM2(+1);   break;
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
 *  RFID — scanning the shopping
 * ============================================================ */
void checkRFID() {
  uint8_t uid[7];
  uint8_t uidLen;

  // Ask the reader: "any card touching you right now?"
  // Timeout 20 ms so we don't hold up the driving loop.
  if (!rfid.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 20)) {
    return;   // no card — nothing to do
  }

  // Print the UID so you can add new items to the price list!
  Serial.print("Card tapped! UID: ");
  for (int i = 0; i < uidLen; i++) {
    Serial.print(uid[i], HEX); Serial.print(" ");
  }
  Serial.println();

  // Look through our little price list for a match
  for (int i = 0; i < NUM_ITEMS; i++) {
    if (uidLen == shopItems[i].uidLen &&
        memcmp(uid, shopItems[i].uid, uidLen) == 0) {

      totalPrice += shopItems[i].price;      // add to the bill
      beep(1, 100);                          // "boop!" = item added

      // Show THIS item on the screen for 20 seconds
      oled.clearDisplay();
      oled.setCursor(0, 0);
      oled.setTextSize(1);
      oled.println("Item added:");
      oled.println(shopItems[i].name);
      oled.print("RM ");
      oled.println(shopItems[i].price, 2);   // 2 = two decimal places
      oled.display();

      showingItem = true;
      itemShownAt = millis();                // start the 20 s stopwatch
      return;
    }
  }

  // Card not in the list — tell the user politely
  beep(2, 50);
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println("Unknown item :(");
  oled.println("Ask staff for help");
  oled.display();
  showingItem = true;
  itemShownAt = millis();
}

/* ============================================================
 *  SCREEN TIMER — after 20 seconds, go back to the total.
 *  Uses millis() so the trolley never stops driving to count!
 * ============================================================ */
void updateScreenTimer() {
  if (showingItem && millis() - itemShownAt >= SHOW_ITEM_MS) {
    showingItem = false;
    showTotal();
  }
}

void showTotal() {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println("Your total:");
  oled.setTextSize(2);                 // big friendly numbers
  oled.print("RM ");
  oled.println(totalPrice, 2);
  oled.display();
}

/* ============================================================
 *  EMERGENCY STOP — case 13, beats everything else
 *  Happens the moment the latching button is released (OFF).
 *  The shopping total is kept safe — nothing is lost!
 * ============================================================ */
void emergencyStop() {
  stopMotors();
  showColor(255, 0, 0);                // red
  beep(3, 200);
  Serial.println(">>> EMERGENCY STOP (button released)");

  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(2);
  oled.println("STOPPED!");
  oled.setTextSize(1);
  oled.println("Press the button");
  oled.println("to shop again");
  oled.display();

  trolleyAwake = false;                // asleep until latched ON again
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
