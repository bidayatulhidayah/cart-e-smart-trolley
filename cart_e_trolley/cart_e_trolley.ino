/*
 * ============================================================
 *  CART-E : THE HUMAN-FOLLOWING SMART TROLLEY
 * ============================================================
 *  Board    : ESP32 Dev Module
 *  Made for : Innovation competition (explained for Year 6!)
 *
 *  HOW TO THINK ABOUT THIS ROBOT:
 *  The trolley has 6 "eyes" (ultrasonic sensors):
 *    - 1 front eye (U1)      -> watches the person in front
 *    - 2 turning eyes (U2,U3)-> see gaps on the left and right
 *    - 3 bodyguard eyes      -> U4 (back-left), U5 (back-right),
 *                               U6 (back) stop us hitting things
 *
 *  Every loop the trolley does 4 things, in this order:
 *    1. Is the emergency button pressed?  -> STOP EVERYTHING
 *    2. Where is my friend?               -> read U1, U2, U3
 *    3. What move do I want to make?      -> forward/stop/turn/back
 *    4. Do my bodyguards say it is safe?  -> read U4, U5, U6
 *  Only if the bodyguards say YES do the wheels move!
 *
 *  LIBRARIES YOU NEED (install from Library Manager):
 *    - Adafruit PN532        (for the RFID reader)
 *    - Adafruit SSD1306      (for the OLED screen)
 *    - Adafruit GFX Library  (helper for the screen)
 *    (Adafruit BusIO installs automatically as a dependency —
 *     say YES when the Library Manager asks!)
 * ============================================================
 */

#include <Wire.h>              // Lets the ESP32 talk on the I2C "party line"
#include <Adafruit_PN532.h>    // The RFID reader
#include <Adafruit_GFX.h>      // Drawing helper for the screen
#include <Adafruit_SSD1306.h>  // The OLED screen

/* ============================================================
 *  PIN MAP  (which wire goes to which leg of the ESP32)
 *  Tip: if a pin gives you trouble, you can change the number
 *  here and re-wire — everything else in the code still works.
 * ============================================================ */

// --- The 6 ultrasonic sensors (each one has TRIG and ECHO) ---
// TRIG = the mouth (sends a click), ECHO = the ear (hears it back)
const int TRIG_PIN[6] = {  4,  5, 13, 14, 16, 17 };
const int ECHO_PIN[6] = { 34, 35, 36, 39, 32, 33 };

// Friendly names so we never mix the sensors up.
// (These are just positions 0-5 inside the arrays above.)
const int U1_FRONT      = 0;  // front eye  - watches the person
const int U2_FRONT_LEFT = 1;  // turning eye - left
const int U3_FRONT_RIGHT= 2;  // turning eye - right
const int U4_BACK_LEFT  = 3;  // bodyguard  - back-left corner
const int U5_BACK_RIGHT = 4;  // bodyguard  - back-right corner
const int U6_BACK       = 5;  // bodyguard  - straight behind us

// --- Motors (through an L298N driver board) ---
// M1 = LEFT wheel, M2 = RIGHT wheel
const int M1_IN1 = 18;   // M1 spins forward when IN1=HIGH, IN2=LOW
const int M1_IN2 = 19;
const int M2_IN3 = 23;   // M2 spins forward when IN3=HIGH, IN4=LOW
const int M2_IN4 = 25;

// --- Lights, buzzer and buttons ---
const int LED_GREEN  = 26;  // green  = "I'm happily following you"
const int LED_RED    = 27;  // red    = "I stopped! Something is wrong"
const int LED_YELLOW = 12;  // yellow = "Please wait for me!"
const int BUZZER     = 15;  // beeps to get attention

const int BTN_START     = 0;   // press once to wake the trolley up
const int BTN_EMERGENCY = 2;   // press to STOP EVERYTHING instantly

// (The RFID reader and OLED both share the I2C pins:
//  SDA = GPIO21, SCL = GPIO22. Like two friends on one phone line.)

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

bool  trolleyAwake   = false;  // has the start button been pressed?
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
  Serial.println("Cart-E is starting up...");

  // Tell every pin what job it has (INPUT = ear, OUTPUT = mouth)
  for (int i = 0; i < 6; i++) {
    pinMode(TRIG_PIN[i], OUTPUT);
    pinMode(ECHO_PIN[i], INPUT);
    digitalWrite(TRIG_PIN[i], LOW);
  }
  pinMode(M1_IN1, OUTPUT);  pinMode(M1_IN2, OUTPUT);
  pinMode(M2_IN3, OUTPUT);  pinMode(M2_IN4, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  // INPUT_PULLUP means: the pin reads HIGH normally,
  // and LOW when the button is pressed. (Wire button to GND.)
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_EMERGENCY, INPUT_PULLUP);

  stopMotors();                   // always start standing still!

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
  oled.println("Press START to begin");
  oled.display();

  // --- Wake up the RFID reader ---
  rfid.begin();
  if (!rfid.getFirmwareVersion()) {
    Serial.println("PN532 not found - check the wiring!");
  } else {
    rfid.SAMConfig();             // "SAM config" = get ready to read cards
    Serial.println("RFID reader ready!");
  }

  digitalWrite(LED_RED, HIGH);    // red while sleeping
}

/* ============================================================
 *  LOOP — runs again and again, forever (very fast!)
 * ============================================================ */
void loop() {

  /* ---- STEP 0: Is the trolley awake yet? ---- */
  if (!trolleyAwake) {
    if (buttonPressed(BTN_START)) {
      trolleyAwake = true;
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_GREEN, HIGH);      // green = following mode!
      beep(1, 100);                       // happy little beep
      showTotal();
      Serial.println("Cart-E is awake and following!");
    }
    return;  // still asleep — skip everything below and check again
  }

  /* ---- STEP 1: EMERGENCY always comes first! ---- */
  if (buttonPressed(BTN_EMERGENCY)) {
    emergencyStop();
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
      setLights(true, false, false);    // green
      goForward();
      break;

    case REVERSE:
      setLights(true, false, false);    // green
      goBackward();
      break;

    case TURN_LEFT:                     // left wheel stops,
      setLights(true, false, false);    // right wheel pushes -> we swing left
      motorM1(0); motorM2(+1);
      break;

    case TURN_RIGHT:                    // right wheel stops,
      setLights(true, false, false);    // left wheel pushes -> we swing right
      motorM1(+1); motorM2(0);
      break;

    case LOST:                          // person gone or too fast:
      setLights(false, false, true);    // yellow = "wait for me!"
      stopMotors();
      beep(1, 300);                     // one long beep
      blinkLed(LED_RED);                // red blinking too
      break;

    case WAIT:                          // confused — just hold still
      setLights(false, false, true);    // yellow
      stopMotors();
      break;

    case STOP:
    default:
      setLights(true, false, false);    // green (happy, just resting)
      stopMotors();
      break;
  }
}

/* ============================================================
 *  MOTOR HELPERS
 *  motorM1 / motorM2 take: +1 = forward, -1 = backward, 0 = stop
 * ============================================================ */
void motorM1(int dir) {
  digitalWrite(M1_IN1, dir > 0 ? HIGH : LOW);
  digitalWrite(M1_IN2, dir < 0 ? HIGH : LOW);
}

void motorM2(int dir) {
  digitalWrite(M2_IN3, dir > 0 ? HIGH : LOW);
  digitalWrite(M2_IN4, dir < 0 ? HIGH : LOW);
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
 * ============================================================ */
void emergencyStop() {
  stopMotors();
  setLights(false, true, false);       // red on
  beep(3, 200);
  Serial.println("EMERGENCY STOP!");

  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(2);
  oled.println("STOPPED!");
  oled.setTextSize(1);
  oled.println("Press START to");
  oled.println("continue shopping");
  oled.display();

  trolleyAwake = false;                // go back to sleep until START
}

/* ============================================================
 *  SMALL HELPERS — buttons, lights, beeps
 * ============================================================ */

// Returns true ONCE when a button is pressed (with debounce —
// real buttons "bounce" like a ball, so we wait 30 ms to be sure)
bool buttonPressed(int pin) {
  if (digitalRead(pin) == LOW) {       // LOW = pressed (pull-up wiring)
    delay(30);
    if (digitalRead(pin) == LOW) {
      while (digitalRead(pin) == LOW) { delay(5); } // wait for release
      return true;
    }
  }
  return false;
}

// Turn the three status lights on/off in one tidy line
void setLights(bool green, bool red, bool yellow) {
  digitalWrite(LED_GREEN,  green  ? HIGH : LOW);
  digitalWrite(LED_RED,    red    ? HIGH : LOW);
  digitalWrite(LED_YELLOW, yellow ? HIGH : LOW);
}

// Beep n times, each lasting ms milliseconds
void beep(int n, int ms) {
  for (int i = 0; i < n; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(ms);
    digitalWrite(BUZZER, LOW);
    if (i < n - 1) delay(ms);
  }
}

// One quick blink of any LED (used for the red "lost" blink)
void blinkLed(int pin) {
  digitalWrite(pin, HIGH);
  delay(100);
  digitalWrite(pin, LOW);
}
