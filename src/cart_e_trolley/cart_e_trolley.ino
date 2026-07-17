/*
 * ============================================================
 *  CART-E : THE HUMAN-FOLLOWING SMART TROLLEY
 * ============================================================
 *  Board  : NodeMCU ESP32 (30-pin) sitting on the
 *           CYTRON ROBO ESP32 expansion board
 *  IDE    : Arduino IDE  (select board "NodeMCU-32S" or "ESP32 Dev Module")
 *
 *  WHY THE ROBO ESP32 MAKES OUR LIFE EASY:
 *    - Motor driver is BUILT IN  (no L298N board needed!)
 *    - Buzzer is BUILT IN on D23 (flip its MUTE switch to ON!)
 *      Your external buzzer joins in PARALLEL on the same pin.
 *    - 2 rainbow NeoPixel LEDs BUILT IN on D15
 *      Your external NeoPixel light joins in PARALLEL too and
 *      always copies the same colour. Both, no extra pins!
 *
 *  HOW TO THINK ABOUT THIS ROBOT:
 *  The trolley has 6 "eyes" (ultrasonic sensors):
 *    - 1 front eye (U1)      -> watches the person in front
 *    - 2 turning eyes (U2,U3)-> see gaps on the left and right
 *    - 3 bodyguard eyes      -> U4 (back-left), U5 (back-right),
 *                               U6 (back) stop us hitting things
 *
 *  Every loop the trolley does 4 things, in this order:
 *    1. Is the ONE button still latched ON? -> released = STOP ALL
 *    2. Where is my friend?               -> read U1, U2, U3
 *    3. What move do I want to make?      -> forward/stop/turn/back
 *    4. Do my bodyguards say it is safe?  -> read U4, U5, U6
 *  Only if the bodyguards say YES do the wheels move!
 *
 *  LIBRARIES YOU NEED (install from Library Manager):
 *    - Adafruit PN532        (for the RFID reader)
 *    - Adafruit SSD1306      (for the OLED screen)
 *    - Adafruit GFX Library  (helper for the screen)
 *    - Adafruit NeoPixel     (for the onboard rainbow LEDs)
 *    (Adafruit BusIO installs automatically as a dependency —
 *     say YES when the Library Manager asks!)
 * ============================================================
 */

#include <Wire.h>              // Lets the ESP32 talk on the I2C "party line"
#include <Adafruit_PN532.h>    // The RFID reader
#include <Adafruit_GFX.h>      // Drawing helper for the screen
#include <Adafruit_SSD1306.h>  // The OLED screen
#include <Adafruit_NeoPixel.h> // The onboard rainbow LEDs

/* ============================================================
 *  PIN MAP — checked against the official Cytron ROBO-ESP32
 *  datasheet, so these numbers match the board's printing!
 * ============================================================ */

// --- The 6 ultrasonic sensors (each one has TRIG and ECHO) ---
// TRIG = the mouth (sends a click), ECHO = the ear (hears it back)
//
// GROVE CABLE TRICK: one Grove port has exactly 4 wires
// (signal, signal, 3V3, GND) — a perfect match for one sensor's
// (Trig, Echo, VCC, GND)! But look closely at the board printing:
// neighbouring Grove ports SHARE a pin (Grove 3 & 4 both have D25!)
// and Grove 7's pins (D36/D39) can only LISTEN, never talk.
// So we use Grove ports 1, 3 and 5 (their pins don't overlap),
// the servo headers for two more, and the breakout header for the last:
//
//   U1 front       -> GROVE 1      : trig D16, echo D17
//   U2 front-left  -> GROVE 3      : trig D26, echo D25
//   U3 front-right -> GROVE 5      : trig D33, echo D32
//   U4 back-left   -> servo S pins : trig D4,  echo D18
//   U5 back-right  -> servo S pins : trig D5,  echo D19
//   U6 back        -> breakout     : trig D2,  echo D36 (label "VP")
//
// (If one sensor always reads 999, its Trig/Echo wires are swapped —
//  either swap the two wires, or swap its two numbers below!)
const int TRIG_PIN[6] = { 16, 26, 33,  4,  5,  2 };
const int ECHO_PIN[6] = { 17, 25, 32, 18, 19, 36 };

// Friendly names so we never mix the sensors up.
// (These are just positions 0-5 inside the arrays above.)
const int U1_FRONT       = 0;  // front eye  - watches the person
const int U2_FRONT_LEFT  = 1;  // turning eye - left
const int U3_FRONT_RIGHT = 2;  // turning eye - right
const int U4_BACK_LEFT   = 3;  // bodyguard  - back-left corner
const int U5_BACK_RIGHT  = 4;  // bodyguard  - back-right corner
const int U6_BACK        = 5;  // bodyguard  - straight behind us

// --- Motors: the driver is BUILT INTO the Robo ESP32! ---
// From the Cytron datasheet's truth table:
//   A = HIGH, B = LOW  -> motor spins FORWARD
//   A = LOW,  B = HIGH -> motor spins BACKWARD
//   A = LOW,  B = LOW  -> brake (stop)
// Just wire M1 to the left wheel, M2 to the right wheel terminals.
const int M1_A = 12;   // Motor 1 (LEFT wheel)  terminal M1A
const int M1_B = 13;   //                        terminal M1B
const int M2_A = 14;   // Motor 2 (RIGHT wheel) terminal M2A
const int M2_B = 27;   //                        terminal M2B

// --- Buzzers: built-in AND external, singing together! ---
// The Robo ESP32 has a piezo buzzer built in on D23 (flip its
// MUTE switch to ON). Your EXTERNAL buzzer connects IN PARALLEL:
// wire it to the D23 breakout pin + GND, and both buzzers get the
// same song at the same time — like a duet, no extra pin needed.
// (Use a small PASSIVE piezo buzzer, or a buzzer module with its
//  own little transistor, so we don't overwork the pin.)
const int BUZZER    = 23;

// --- Rainbow LEDs: built-in AND external, matching colours! ---
// The 2 onboard NeoPixels sit on D15. Your EXTERNAL NeoPixel
// module/stick connects IN PARALLEL to the same D15 breakout pin
// (+ 3V3 + GND). It receives the exact same colour message, so the
// big external light always copies the small onboard ones. Magic!
const int NEOPIXELS  = 15;
const int NUM_PIXELS = 8;  // covers 2 onboard + an external stick of
                           // up to 8 lights. External strip longer?
                           // Just raise this number to match it.

// --- THE ONE AND ONLY BUTTON ---
// A self-latching SPST push button (normally open):
//   press once  -> it latches closed  -> trolley ON
//   press again -> it springs open    -> trolley OFF (emergency stop!)
// Because it LATCHES, we don't look for a "press" — we simply read
// whether the switch is closed (ON) or open (OFF) right now.
// Wire it between the D35 breakout pin and GND. D35 already has a
// pull-up ON THE ROBO ESP32 BOARD (it belongs to onboard button 1),
// so open = HIGH, latched = LOW.
// Bonus: onboard button "1" is on the same pin — holding it down
// works as a quick test of the trolley without your big button!
const int SW_POWER = 35;  // LOW = latched ON, HIGH = released OFF

// (The RFID reader and OLED both plug into I2C: SDA = D21, SCL = D22.
//  On the Robo ESP32 that is Grove Port 2 or the Maker Port —
//  like two friends sharing one phone line.)

/* ============================================================
 *  DISTANCE RULES  (all in centimetres)
 *  These numbers decide how the trolley behaves. You can tune
 *  them during testing — that's what engineers do!
 * ============================================================ */
const int FOLLOW_DIST   = 20;  // perfect distance to the person
const int LOST_DIST     = 40;  // further than this = person is gone
const int SIDE_GAP      = 10;  // U2/U3 closer than this = person/wall beside us
const int GUARD_DIST    = 10;  // bodyguards: closer than this = DANGER
const int SQUEEZE_DIST  = 5;   // sides really scraping something
const int DEAD_ZONE     = 3;   // +/- wiggle room so we don't jitter

/* ============================================================
 *  THE MOVES the trolley can choose from
 * ============================================================ */
enum Move { STOP, FORWARD, REVERSE, TURN_LEFT, TURN_RIGHT, LOST, WAIT };

/* ============================================================
 *  RFID PRICE LIST
 *  Each sticker has a unique ID (UID). We keep a little price
 *  list here, like a mini shop database.
 *  HOW TO ADD YOUR OWN ITEMS:
 *   1. Upload this code and open the Serial Monitor.
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
 *  ("Global" = the whole program can see them)
 * ============================================================ */

// RFID reader on I2C. The -1, -1 means "IRQ and RESET pins are
// not wired" — the library officially supports this for I2C mode.
Adafruit_PN532 rfid(-1, -1, &Wire);

Adafruit_SSD1306 oled(128, 64, &Wire, -1);          // 128x64 OLED screen

// The two onboard rainbow LEDs (NeoPixels) on pin D15
Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXELS, NEO_GRB + NEO_KHZ800);

bool  trolleyAwake   = false;  // is the latching button switched ON?
float totalPrice     = 0.0;    // running total of the shopping
long  lastFrontDist  = FOLLOW_DIST; // remembers last U1 reading
                                    // (to spot "person ran away!")

// Timer for showing an item price for 20 seconds on the OLED.
// We use millis() (a stopwatch) instead of delay() (freezing),
// so the trolley can keep driving while the screen counts down.
unsigned long itemShownAt   = 0;
bool          showingItem   = false;
const unsigned long SHOW_ITEM_MS = 20000; // 20 seconds

/* ============================================================
 *  SETUP — runs ONCE when the trolley is switched on
 * ============================================================ */
void setup() {
  Serial.begin(115200);           // so we can print messages to the computer
  Serial.println("Cart-E (Robo ESP32 edition) is starting up...");

  // Tell every pin what job it has (INPUT = ear, OUTPUT = mouth)
  for (int i = 0; i < 6; i++) {
    pinMode(TRIG_PIN[i], OUTPUT);
    pinMode(ECHO_PIN[i], INPUT);
    digitalWrite(TRIG_PIN[i], LOW);
  }
  pinMode(M1_A, OUTPUT);  pinMode(M1_B, OUTPUT);
  pinMode(M2_A, OUTPUT);  pinMode(M2_B, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  // D35 is an "ears-only" pin and the Robo ESP32 board already
  // gives it a pull-up resistor, so plain INPUT is all we need.
  pinMode(SW_POWER, INPUT);          // switch wired to GND

  stopMotors();                   // always start standing still!

  pixels.begin();                 // wake up the rainbow LEDs
  pixels.setBrightness(60);       // not too bright — save the eyes!
  showColor(255, 0, 0);           // red while sleeping

  Wire.begin(21, 22);             // start the I2C party line

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
    rfid.SAMConfig();             // "SAM config" = get ready to read cards
    Serial.println("RFID reader ready!");
  }
}

/* ============================================================
 *  LOOP — runs again and again, forever (very fast!)
 * ============================================================ */
void loop() {

  /* ---- STEP 0 + 1: Read the ONE button (power + emergency) ----
   * Latched closed = ON.  Released open = OFF.
   * Turning it off while driving IS the emergency stop!            */
  bool switchOn = switchIsOn();

  if (!trolleyAwake) {
    if (switchOn) {                       // just latched ON — wake up!
      trolleyAwake = true;
      showColor(0, 255, 0);               // green = following mode!
      beep(1, 100);                       // happy little beep
      showTotal();
      Serial.println("Cart-E is awake and following!");
    }
    return;  // still off — skip everything below and check again
  }

  if (!switchOn) {                        // released while driving —
    emergencyStop();                      // STOP EVERYTHING, go to sleep
    return;
  }

  /* ---- STEP 2: Where is my friend? (read the front 3 eyes) ---- */
  long dFront = readDistanceCm(U1_FRONT);
  long dLeft  = readDistanceCm(U2_FRONT_LEFT);
  long dRight = readDistanceCm(U3_FRONT_RIGHT);

  /* ---- STEP 3: Choose a move (cases 1-6, 11, 12) ---- */
  Move wantedMove = chooseMove(dFront, dLeft, dRight);

  /* ---- STEP 4: Ask the bodyguards (cases 7-10) ---- */
  Move safeMove = safetyCheck(wantedMove);

  /* ---- STEP 5: Do the move! ---- */
  doMove(safeMove);

  /* ---- STEP 6: Any shopping scanned? ---- */
  checkRFID();

  /* ---- STEP 7: Has the 20-second price display finished? ---- */
  updateScreenTimer();

  lastFrontDist = dFront;   // remember for the "too fast" check
  delay(30);                // tiny rest so sensors don't argue
}

/* ============================================================
 *  CHOOSE MOVE — the trolley's brain
 *  Looks at the front 3 eyes and picks what it WANTS to do.
 *  (The bodyguards may still say no afterwards!)
 * ============================================================ */
Move chooseMove(long dFront, long dLeft, long dRight) {

  // CASE 12: person moved TOO FAST — they "vanished" in one step.
  // Last time they were close, now suddenly very far.
  if (lastFrontDist < FOLLOW_DIST + 5 && dFront > LOST_DIST) {
    return LOST;   // "Wait for meee!"
  }

  // CASE 11: CONFUSED — something close on BOTH sides at once.
  // Maybe two people walked past. Safer to wait than to guess.
  if (dLeft < SIDE_GAP && dRight < SIDE_GAP && dFront > LOST_DIST) {
    return WAIT;
  }

  // CASE 5: person moved to the LEFT (left eye sees them close,
  // front eye sees empty space) -> turn left to follow.
  if (dLeft < SIDE_GAP && dFront > FOLLOW_DIST) {
    return TURN_LEFT;
  }

  // CASE 6: person moved to the RIGHT -> turn right to follow.
  if (dRight < SIDE_GAP && dFront > FOLLOW_DIST) {
    return TURN_RIGHT;
  }

  // CASE 4: ABANDONED — nobody in front anymore.
  if (dFront > LOST_DIST) {
    return LOST;
  }

  // CASE 3: person is TOO CLOSE — politely back away.
  if (dFront < FOLLOW_DIST - DEAD_ZONE) {
    return REVERSE;
  }

  // CASE 1: person is comfortably ahead — follow them!
  if (dFront > FOLLOW_DIST + DEAD_ZONE) {
    return FORWARD;
  }

  // CASE 2: person is at the perfect distance — stand still.
  return STOP;
}

/* ============================================================
 *  SAFETY CHECK — the bodyguards get the final word
 *  Takes the move the brain wants, and vetoes it if the back
 *  sensors see danger. Safety ALWAYS wins over wanting!
 * ============================================================ */
Move safetyCheck(Move wanted) {
  long dBackLeft  = readDistanceCm(U4_BACK_LEFT);
  long dBackRight = readDistanceCm(U5_BACK_RIGHT);
  long dBack      = readDistanceCm(U6_BACK);

  // CASE 7: wants to reverse, but something is BEHIND us.
  if (wanted == REVERSE && dBack < GUARD_DIST) {
    beep(2, 80);          // "beep beep - can't go back!"
    return STOP;
  }

  // CASE 8: wants to turn left, but the back-left corner would
  // swing into a rack. (When you turn, your back sticks out!)
  if (wanted == TURN_LEFT && dBackLeft < GUARD_DIST) {
    beep(2, 80);
    return STOP;
  }

  // CASE 9: same idea for turning right.
  if (wanted == TURN_RIGHT && dBackRight < GUARD_DIST) {
    beep(2, 80);
    return STOP;
  }

  // CASE 10: SQUEEZED — driving forward but scraping a rack
  // or a person on our side. Stop before we bump them.
  if (wanted == FORWARD &&
      (dBackLeft < SQUEEZE_DIST || dBackRight < SQUEEZE_DIST)) {
    beep(3, 60);
    return STOP;
  }

  return wanted;   // bodyguards say: "all clear, go ahead!"
}

/* ============================================================
 *  DO MOVE — turn the chosen move into wheel commands + lights
 * ============================================================ */
void doMove(Move m) {
  switch (m) {

    case FORWARD:
      showColor(0, 255, 0);               // green = happy following
      goForward();
      break;

    case REVERSE:
      showColor(0, 255, 0);               // green
      goBackward();
      break;

    case TURN_LEFT:                       // left wheel stops,
      showColor(0, 255, 0);               // right wheel pushes -> swing left
      motorM1(0); motorM2(+1);
      break;

    case TURN_RIGHT:                      // right wheel stops,
      showColor(0, 255, 0);               // left wheel pushes -> swing right
      motorM1(+1); motorM2(0);
      break;

    case LOST:                            // person gone or too fast:
      stopMotors();
      showColor(255, 150, 0);             // yellow = "wait for me!"
      beep(1, 300);                       // one long beep
      showColor(255, 0, 0);               // quick red flash too
      delay(100);
      showColor(255, 150, 0);
      break;

    case WAIT:                            // confused — just hold still
      showColor(255, 150, 0);             // yellow
      stopMotors();
      break;

    case STOP:
    default:
      showColor(0, 255, 0);               // green (happy, just resting)
      stopMotors();
      break;
  }
}

/* ============================================================
 *  MOTOR HELPERS — using the Robo ESP32's built-in driver
 *  From the Cytron truth table:
 *    A HIGH + B LOW  = forward
 *    A LOW  + B HIGH = backward
 *    A LOW  + B LOW  = brake (stop)
 *  motorM1 / motorM2 take: +1 = forward, -1 = backward, 0 = stop
 *  (If a wheel spins the wrong way, just swap its two motor
 *   wires on the green terminal — no code change needed!)
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
 *  READ DISTANCE — how one ultrasonic "eye" works
 *  1. TRIG sends a tiny click (too high for humans to hear)
 *  2. The click bounces off whatever is in front
 *  3. ECHO hears it come back
 *  4. Sound travels ~0.034 cm every microsecond, and the click
 *     travelled THERE AND BACK, so we divide by 2.
 * ============================================================ */
long readDistanceCm(int sensor) {
  digitalWrite(TRIG_PIN[sensor], LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN[sensor], HIGH);   // send the click...
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN[sensor], LOW);

  // ...and time how long until we hear it back (max wait 25 ms,
  // so one slow sensor can't freeze the whole trolley)
  long duration = pulseIn(ECHO_PIN[sensor], HIGH, 25000);

  if (duration == 0) return 999;          // heard nothing = far away
  return duration * 0.034 / 2;            // convert time -> centimetres
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
  Serial.println("EMERGENCY STOP! (button released)");

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
 *  SMALL HELPERS — switch, rainbow LEDs, beeps
 * ============================================================ */

// Reads the latching switch: true = latched ON, false = OFF.
// Real switches "bounce" (flicker) for a few milliseconds when they
// change, so if the reading changed we double-check after 30 ms.
bool switchIsOn() {
  static bool lastState = false;          // remembers between loops
  bool nowState = (digitalRead(SW_POWER) == LOW);  // LOW = latched ON

  if (nowState != lastState) {            // did it just change?
    delay(30);                            // wait out the bounce...
    nowState = (digitalRead(SW_POWER) == LOW);     // ...and re-check
    lastState = nowState;
  }
  return nowState;
}

// Paint BOTH onboard rainbow LEDs the same colour.
// (red, green, blue) each go from 0 (off) to 255 (full power).
//   green  = (0, 255, 0)     red = (255, 0, 0)
//   yellow = (255, 150, 0)   off = (0, 0, 0)
void showColor(int r, int g, int b) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

// Beep n times, each lasting ms milliseconds.
// The Robo ESP32 buzzer is a PIEZO — it needs a musical note
// (a frequency), not just ON/OFF. tone() plays the note for us.
// 2000 Hz is a nice clear "robot beep" pitch.
void beep(int n, int ms) {
  for (int i = 0; i < n; i++) {
    tone(BUZZER, 2000);       // start singing at 2000 Hz
    delay(ms);
    noTone(BUZZER);           // stop singing
    if (i < n - 1) delay(ms);
  }
}
