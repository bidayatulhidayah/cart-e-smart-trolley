/*
 * ============================================================
 *  CART-E : THE HUMAN-FOLLOWING SMART TROLLEY  —  FULL SYSTEM
 * ============================================================
 *  Board  : NodeMCU ESP32 (30-pin) on the CYTRON ROBO ESP32
 *
 *  This combines our two TESTED programs:
 *    MOVEMENT TEST v2  (following + smart turning + bodyguards)
 *    RFID SHOP TEST    (scan, OLED, buzzer, dashboard, pay)
 *
 *  MOVEMENT (calibrated):
 *      closer than 27 cm  -> REVERSE
 *      27 to 33 cm        -> STOP (the sweet spot)
 *      33 to 150 cm       -> FORWARD (follow)
 *      beyond 150 cm      -> LOST: stop + buzzer
 *    Turning chases THE PERSON, not empty space: they vanish from
 *    the front eye, appear in a corner eye -> turn that way until
 *    the front eye finds them again.
 *
 *  SHOPPING:
 *    - Tap a sticker: "boop!", OLED shows the item + price 20 s,
 *      then back to the running total.
 *    - SCAN-ONCE RULE: each sticker only once in the cart. Tap
 *      again = refused, unless cancelled from the dashboard first.
 *    - Dashboard (phone browser): total, live trolley status,
 *      scanned list with cancel buttons, price list, PAY NOW.
 *    - PAY NOW: QR screen -> "I have paid" -> happy melody, OLED
 *      thank-you, cart resets for the next shopper.
 *    - Shopping keeps working even when the trolley is parked
 *      (button off) — perfect for the checkout moment.
 *
 *  WIFI DASHBOARD:
 *      WiFi name : CartE-Dashboard   password: carte1234
 *      Address   : http://192.168.4.1
 *
 *  SOUND LANGUAGE (buzzer D23 — MUTE switch ON!):
 *      1 beep = awake / item added     2 beeps = refused/unknown
 *      3 beeps = emergency stop        long beep = lost you
 *      melody = payment complete
 *
 *  LIGHT LANGUAGE (NeoPixels D15):
 *      GREEN = following   YELLOW = wait/confused   RED = stopped
 *
 *  PHYSICAL SETUP REMINDERS:
 *    - U2 and U3 angled OUTWARD 30-45 degrees (fan \ | /)
 *    - PN532: only 4 wires (VCC, GND, SDA=D21, SCL=D22),
 *      DIP switches SW1=ON SW2=OFF. IRQ/RESET are NOT wired —
 *      33/25 below are just declarations the library wants.
 *    - OLED shares the same I2C wires (address 0x3C).
 *    - ESP32 has NO pin 24 — buzzer is D23!
 *
 *  LIBRARIES: Adafruit PN532, Adafruit SSD1306, Adafruit GFX
 *  Library, Adafruit NeoPixel. (WiFi/WebServer are built in.)
 * ============================================================
 */

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>

/* ============================================================
 *  PIN MAP  (identical to both tested programs)
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

const int BUZZER     = 23;  // built-in + external in parallel (NOT 24!)
const int NEOPIXELS  = 15;  // onboard 2 + external stick in parallel
const int NUM_PIXELS = 8;

const int SW_POWER = 35;    // latching button (LOW = ON)
                            // onboard button "1" shares this pin

// PN532 I2C wiring (only these 2 signal wires are physical!)
#define SDA_PIN     21
#define SCL_PIN     22
// Declarations only — nothing is wired to 33/25. The library
// touches them ONCE at startup; we init the RFID FIRST in setup()
// and then hand these pins to ultrasonic sensors U3/U2.
#define PN532_IRQ   33
#define PN532_RESET 25

/* ============================================================
 *  DISTANCE RULES (cm) — the calibrated numbers
 *      0 ......27 : REVERSE     27 ......33 : STOP
 *     33 .....150 : FORWARD    150 ......   : LOST
 * ============================================================ */
const int FOLLOW_DIST   = 30;   // middle of the STOP sweet spot
const int DEAD_ZONE     = 3;    // sweet spot = 27 to 33
const int LOST_DIST     = 150;  // beyond this = person is gone
const int PERSON_RANGE  = 60;   // corner eye sees the person within this
const int GUARD_DIST    = 10;   // bodyguards: closer = DANGER
const int SQUEEZE_DIST  = 5;    // sides really scraping something

enum Move { STOP, FORWARD, REVERSE, TURN_LEFT, TURN_RIGHT, LOST, WAIT };

// Names for printing + the dashboard, SAME order as the enum
const char* MOVE_NAME[] =
  { "STOP", "FORWARD", "REVERSE", "TURN LEFT", "TURN RIGHT",
    "LOST - wait for me!", "WAIT - confused" };

/* ============================================================
 *  WIFI + DASHBOARD SETTINGS
 * ============================================================ */

// true  = trolley makes ITS OWN WiFi (best for demos, always
//         http://192.168.4.1). false = join your router below.
#define USE_ACCESS_POINT true

const char *AP_NAME     = "CartE-Dashboard";
const char *AP_PASSWORD = "carte1234";        // min 8 characters

const char *WIFI_NAME     = "YourWifiName";
const char *WIFI_PASSWORD = "YourWifiPassword";
IPAddress staticIP(192, 168, 1, 50);
IPAddress gateway (192, 168, 1, 1);
IPAddress subnet  (255, 255, 255, 0);

WebServer server(80);

/* ============================================================
 *  THE SHOP DATABASE — our 10 registered items
 * ============================================================ */
struct Item {
  uint8_t uid[7];
  uint8_t uidLen;
  const char *name;
  float price;
};

Item shopItems[] = {
  { {0xF2, 0x64, 0x3A, 0xC7}, 4, "Susu",             3.50 },
  { {0xD2, 0x9B, 0x3A, 0xC7}, 4, "Potato Chip",      4.20 },
  { {0x72, 0xE1, 0x3A, 0xC7}, 4, "Sandwich Cookies", 5.00 },
  { {0xC2, 0x9C, 0x2C, 0xC7}, 4, "Morning Coffee",   8.90 },
  { {0x12, 0x21, 0x3A, 0xC7}, 4, "Orange Juice",     6.50 },
  { {0x6B, 0xA5, 0x3B, 0xCE}, 4, "Oatmeal",         12.90 },
  { {0x6B, 0x9D, 0xBD, 0xCE}, 4, "Snacks",           3.00 },
  { {0x6B, 0xA0, 0xE9, 0xCE}, 4, "Veggies",          7.50 },
  { {0x6B, 0x91, 0xFD, 0xCE}, 4, "Fruits",           9.90 },
  { {0x6B, 0x98, 0xD7, 0xCE}, 4, "Egg",             13.50 },
};
const int NUM_ITEMS = sizeof(shopItems) / sizeof(shopItems[0]);

/* ============================================================
 *  THE CART — every scan gets its own ID so the dashboard's
 *  cancel button always removes exactly the right entry.
 * ============================================================ */
const int MAX_CART = 30;
int cartItem[MAX_CART];
int cartId[MAX_CART];
int cartCount = 0;
int nextId = 1;

/* ============================================================
 *  GLOBAL OBJECTS AND STATE
 * ============================================================ */
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);       // proven constructor
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXELS, NEO_GRB + NEO_KHZ800);

bool   rfidOK        = false;   // did the PN532 wake up properly?
bool   trolleyAwake  = false;   // latching button state
long   lastFrontDist = FOLLOW_DIST;
String currentMove   = "ASLEEP";   // shown live on the dashboard

float  totalPrice    = 0.0;
String lastItemName  = "-";
float  lastItemPrice = 0.0;

// Anti-repeat for a sticker resting on the reader
uint8_t lastUid[7] = {0};
uint8_t lastUidLen = 0;
unsigned long lastSeenAt = 0;

// OLED screen timer (millis stopwatch — never freezes the trolley)
unsigned long itemShownAt = 0;
bool          showingItem = false;
const unsigned long SHOW_ITEM_MS    = 20000;  // scanned item: 20 s
const unsigned long SHOW_REMOVED_MS = 5000;   // removed / refused: 5 s
const unsigned long SHOW_PAID_MS    = 8000;   // thank-you: 8 s
unsigned long showForMs = SHOW_ITEM_MS;

// Serial "thoughts" printer (every 300 ms)
unsigned long lastPrintAt = 0;

/* ============================================================
 *  SMALL HELPERS — sounds, lights, switch
 * ============================================================ */

// Beep n times using tone() — the piezo needs a musical note.
void beep(int n, int ms) {
  for (int i = 0; i < n; i++) {
    tone(BUZZER, 2000);
    delay(ms);
    noTone(BUZZER);
    if (i < n - 1) delay(ms);
  }
}

// Three rising notes — the "payment successful!" jingle.
void happyMelody() {
  int notes[3] = { 1319, 1568, 2093 };   // E6, G6, C7
  for (int i = 0; i < 3; i++) {
    tone(BUZZER, notes[i]);
    delay(130);
  }
  noTone(BUZZER);
}

// Paint every NeoPixel (onboard + external) the same colour.
void showColor(int r, int g, int b) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

// Latching switch: true = ON. Double-checks after 30 ms (bounce).
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

/* ============================================================
 *  SETUP — order matters! Motors safe first, RFID before the
 *  ultrasonic pins (the library pokes 33/25 once at startup).
 * ============================================================ */
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println(F("=== CART-E FULL SYSTEM starting up... ==="));

  // Motors first, and STOPPED — safety before anything else.
  pinMode(M1_A, OUTPUT);  pinMode(M1_B, OUTPUT);
  pinMode(M2_A, OUTPUT);  pinMode(M2_B, OUTPUT);
  stopMotors();

  pinMode(BUZZER, OUTPUT);
  pinMode(SW_POWER, INPUT);   // board already has the pull-up

  pixels.begin();
  pixels.setBrightness(60);
  showColor(255, 0, 0);       // red = sleeping

  Wire.begin(SDA_PIN, SCL_PIN);

  // Quick I2C bus scan — we expect 0x24 (PN532) and 0x3C (OLED).
  Serial.println(F("Scanning I2C bus..."));
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  Found device at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
    }
  }

  // --- RFID BEFORE the ultrasonic pins! ---
  // (If it fails we warn and carry on — the trolley still drives.)
  nfc.begin();
  if (nfc.getFirmwareVersion()) {
    nfc.SAMConfig();
    rfidOK = true;
    Serial.println(F("RFID reader ready!"));
  } else {
    Serial.println(F("WARNING: PN532 not found - shopping offline,"));
    Serial.println(F("check DIP switches (SW1=ON SW2=OFF) & wiring."));
    beep(2, 60);
  }

  // --- OLED ---
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("WARNING: OLED not found - screen offline."));
  }
  oled.setTextColor(SSD1306_WHITE);
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("CART-E"));
  oled.println(F("Press button to start"));
  oled.display();

  // --- WiFi + dashboard ---
  if (USE_ACCESS_POINT) {
    WiFi.softAP(AP_NAME, AP_PASSWORD);
    Serial.print(F("WiFi network created: "));
    Serial.println(AP_NAME);
    Serial.print(F("Dashboard: http://"));
    Serial.println(WiFi.softAPIP());     // always 192.168.4.1
  } else {
    WiFi.config(staticIP, gateway, subnet);
    WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
    Serial.print(F("Joining WiFi"));
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
      delay(500); Serial.print('.'); tries++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(F("Dashboard: http://"));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("WARNING: WiFi not found - dashboard offline."));
    }
  }
  server.on("/", handleDashboard);
  server.on("/data", handleData);
  server.on("/cancel", handleCancel);
  server.on("/pay", handlePay);
  server.begin();

  // --- NOW the ultrasonic sensors take their pins (incl. 33 & 25) ---
  for (int i = 0; i < 6; i++) {
    pinMode(TRIG_PIN[i], OUTPUT);
    pinMode(ECHO_PIN[i], INPUT);
    digitalWrite(TRIG_PIN[i], LOW);
  }

  beep(1, 80);                 // "all systems go!" chime
  Serial.println(F("All systems ready! Press the button to follow."));
  Serial.println(F("--------------------------------------"));
}

/* ============================================================
 *  LOOP — dashboard + shopping ALWAYS work; movement only
 *  when the latching button is ON.
 * ============================================================ */
void loop() {

  // These three run in every state (driving, parked, asleep):
  server.handleClient();                    // answer phones/laptops
  if (showingItem && millis() - itemShownAt >= showForMs) {
    showingItem = false;                    // screen back to total
    showTotal();
  }
  checkRFID();                              // shopping while parked = OK

  /* ---- The ONE button: power + emergency stop ---- */
  bool switchOn = switchIsOn();

  if (!trolleyAwake) {
    if (switchOn) {                         // latched ON — wake up!
      trolleyAwake = true;
      currentMove  = "STOP";
      showColor(0, 255, 0);
      beep(1, 100);
      showTotal();
      Serial.println(F(">>> AWAKE! Following mode ON"));
    }
    return;                                 // asleep: no driving
  }

  if (!switchOn) {                          // released — EMERGENCY STOP
    emergencyStop();
    return;
  }

  /* ---- Read ALL 6 eyes ---- */
  long dFront     = readDistanceCm(U1_FRONT);
  long dLeft      = readDistanceCm(U2_FRONT_LEFT);
  long dRight     = readDistanceCm(U3_FRONT_RIGHT);
  long dBackLeft  = readDistanceCm(U4_BACK_LEFT);
  long dBackRight = readDistanceCm(U5_BACK_RIGHT);
  long dBack      = readDistanceCm(U6_BACK);

  /* ---- Think, check safety, then move ---- */
  Move wanted = chooseMove(dFront, dLeft, dRight);
  Move safe   = safetyCheck(wanted, dBackLeft, dBackRight, dBack);
  doMove(safe);

  /* ---- The trolley's "thoughts", every 300 ms ---- */
  if (millis() - lastPrintAt > 300) {
    lastPrintAt = millis();
    Serial.print(F("U1:"));  Serial.print(dFront);
    Serial.print(F(" U2:")); Serial.print(dLeft);
    Serial.print(F(" U3:")); Serial.print(dRight);
    Serial.print(F(" U4:")); Serial.print(dBackLeft);
    Serial.print(F(" U5:")); Serial.print(dBackRight);
    Serial.print(F(" U6:")); Serial.print(dBack);
    Serial.print(F("  ->  "));
    Serial.print(MOVE_NAME[safe]);
    if (safe != wanted) {
      Serial.print(F("  (bodyguards vetoed: "));
      Serial.print(MOVE_NAME[wanted]);
      Serial.print(F(")"));
    }
    Serial.println();
  }

  lastFrontDist = dFront;
  delay(30);                  // tiny rest so sensors don't argue
}

/* ============================================================
 *  CHOOSE MOVE — the trolley's brain (tested movement v2)
 * ============================================================ */
Move chooseMove(long dFront, long dLeft, long dRight) {

  bool personFront = (dFront <= LOST_DIST);
  bool personLeft  = (dLeft  <  PERSON_RANGE);
  bool personRight = (dRight <  PERSON_RANGE);

  // CASE 12: person vanished in ONE step — moved too fast!
  if (lastFrontDist < FOLLOW_DIST + 10 && dFront > LOST_DIST) {
    return LOST;
  }

  // Person in front — normal following (cases 1, 2, 3):
  if (personFront) {
    if (dFront < FOLLOW_DIST - DEAD_ZONE) return REVERSE;  // under 27
    if (dFront > FOLLOW_DIST + DEAD_ZONE) return FORWARD;  // 33 to 150
    return STOP;                                           // sweet spot
  }

  // Front eye sees nobody. Where did they go?
  if (personLeft && personRight) return WAIT;   // CASE 11: confused
  if (personLeft)  return TURN_LEFT;            // CASE 5: chase left
  if (personRight) return TURN_RIGHT;           // CASE 6: chase right
  return LOST;                                  // CASE 4: abandoned
}

/* ============================================================
 *  SAFETY CHECK — bodyguards veto dangerous moves (cases 7-10)
 * ============================================================ */
Move safetyCheck(Move wanted, long dBackLeft, long dBackRight, long dBack) {

  if (wanted == REVERSE && dBack < GUARD_DIST) {          // CASE 7
    beep(2, 80);
    return STOP;
  }
  if (wanted == TURN_LEFT && dBackLeft < GUARD_DIST) {    // CASE 8
    beep(2, 80);
    return STOP;
  }
  if (wanted == TURN_RIGHT && dBackRight < GUARD_DIST) {  // CASE 9
    beep(2, 80);
    return STOP;
  }
  if (wanted == FORWARD &&                                // CASE 10
      (dBackLeft < SQUEEZE_DIST || dBackRight < SQUEEZE_DIST)) {
    beep(3, 60);
    return STOP;
  }
  return wanted;
}

/* ============================================================
 *  DO MOVE — wheels + lights (calibrated turn directions!)
 * ============================================================ */
void doMove(Move m) {
  currentMove = MOVE_NAME[m];        // live status for the dashboard

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
 *  EMERGENCY STOP — button released. Total is kept safe, and
 *  shopping/dashboard keep working while parked.
 * ============================================================ */
void emergencyStop() {
  stopMotors();
  currentMove = "STOPPED";
  showColor(255, 0, 0);
  beep(3, 200);
  trolleyAwake = false;
  Serial.println(F(">>> EMERGENCY STOP (button released)"));

  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(2);
  oled.println(F("STOPPED!"));
  oled.setTextSize(1);
  oled.println(F("Scanning still works"));
  oled.println(F("Press button to"));
  oled.println(F("follow again"));
  oled.display();
}

/* ============================================================
 *  MOTOR HELPERS — Cytron truth table
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
 *  READ DISTANCE — bat-style echo timing
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
 *  RFID — scanning the shopping (scan-once rule included)
 * ============================================================ */
void checkRFID() {
  if (!rfidOK) return;         // reader offline: skip quietly

  uint8_t uid[7];
  uint8_t uidLength;

  // 20 ms timeout: quick enough that driving never gets sluggish,
  // and the loop asks again ~30x per second — taps still register.
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0],
                               &uidLength, 20)) {
    return;
  }

  // Same sticker resting on the reader? Skip the repeat.
  bool sameCard = (uidLength == lastUidLen) &&
                  (memcmp(uid, lastUid, uidLength) == 0) &&
                  (millis() - lastSeenAt < 2000);
  lastSeenAt = millis();
  if (sameCard) return;
  memcpy(lastUid, uid, uidLength);
  lastUidLen = uidLength;

  // --- Look it up in the shop database ---
  for (int i = 0; i < NUM_ITEMS; i++) {
    if (uidLength == shopItems[i].uidLen &&
        memcmp(uid, shopItems[i].uid, uidLength) == 0) {

      // SCAN-ONCE RULE: already in the cart? Refuse politely.
      for (int c = 0; c < cartCount; c++) {
        if (cartItem[c] == i) {
          beep(2, 50);
          Serial.print(F("ALREADY IN CART: "));
          Serial.println(shopItems[i].name);
          showAlreadyInCart(shopItems[i].name);
          return;
        }
      }

      if (cartCount >= MAX_CART) {
        Serial.println(F("CART FULL! Cancel something first."));
        beep(3, 60);
        return;
      }

      totalPrice += shopItems[i].price;
      lastItemName  = shopItems[i].name;
      lastItemPrice = shopItems[i].price;
      cartItem[cartCount] = i;
      cartId[cartCount]   = nextId++;
      cartCount++;

      beep(1, 100);                        // "boop!" = item added

      Serial.print(F("ITEM ADDED: "));
      Serial.print(shopItems[i].name);
      Serial.print(F("  RM "));
      Serial.print(shopItems[i].price, 2);
      Serial.print(F("  | total RM "));
      Serial.println(totalPrice, 2);

      showItem(shopItems[i].name, shopItems[i].price);
      return;
    }
  }

  // --- Unknown sticker: print a ready-to-paste line ---
  beep(2, 50);
  Serial.print(F("UNKNOWN sticker! Add to shopItems: { {"));
  for (uint8_t i = 0; i < uidLength; i++) {
    Serial.print(F("0x"));
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
    if (i < uidLength - 1) Serial.print(F(", "));
  }
  Serial.print(F("}, "));
  Serial.print(uidLength);
  Serial.println(F(", \"ITEM NAME\", 0.00 },"));
  showUnknown();
}

/* ============================================================
 *  OLED SCREENS
 * ============================================================ */
void showTotal() {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Your total:"));
  oled.setCursor(0, 24);
  oled.setTextSize(2);
  oled.print(F("RM "));
  oled.println(totalPrice, 2);
  oled.display();
}

void showItem(const char *name, float price) {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Item added:"));
  oled.setCursor(0, 16);
  oled.println(name);
  oled.setCursor(0, 34);
  oled.setTextSize(2);
  oled.print(F("RM "));
  oled.println(price, 2);
  oled.display();

  showingItem = true;
  showForMs   = SHOW_ITEM_MS;
  itemShownAt = millis();
}

void showRemoved(const char *name, float price) {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Item removed:"));
  oled.setCursor(0, 16);
  oled.println(name);
  oled.setCursor(0, 34);
  oled.setTextSize(2);
  oled.print(F("-RM "));
  oled.println(price, 2);
  oled.display();

  showingItem = true;
  showForMs   = SHOW_REMOVED_MS;
  itemShownAt = millis();
}

void showAlreadyInCart(const char *name) {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Already in cart:"));
  oled.setCursor(0, 16);
  oled.println(name);
  oled.setCursor(0, 40);
  oled.println(F("Cancel on dashboard"));
  oled.println(F("to scan again"));
  oled.display();

  showingItem = true;
  showForMs   = SHOW_REMOVED_MS;
  itemShownAt = millis();
}

void showUnknown() {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Unknown item :("));
  oled.println(F(""));
  oled.println(F("Ask staff for help"));
  oled.display();

  showingItem = true;
  showForMs   = SHOW_ITEM_MS;
  itemShownAt = millis();
}

void showPaid(float amount) {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Payment received"));
  oled.setCursor(0, 16);
  oled.setTextSize(2);
  oled.print(F("RM "));
  oled.println(amount, 2);
  oled.setCursor(0, 44);
  oled.setTextSize(1);
  oled.println(F("Thank you! :)"));
  oled.display();

  showingItem = true;
  showForMs   = SHOW_PAID_MS;
  itemShownAt = millis();
}

/* ============================================================
 *  THE WEB DASHBOARD
 * ============================================================ */

void handleDashboard() {
  String html;
  html.reserve(5500);
  html += F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Cart-E Dashboard</title><style>"
    "body{font-family:Arial,sans-serif;background:#f4f1ec;margin:0;padding:16px;color:#222}"
    "h1{font-size:1.3em;margin:0 0 12px}"
    ".card{background:#fff;border-radius:12px;padding:16px;margin-bottom:12px;"
    "box-shadow:0 1px 4px rgba(0,0,0,.12)}"
    ".total{font-size:2.2em;font-weight:bold;color:#1a7f37}"
    ".item{font-size:1.2em}"
    "table{width:100%;border-collapse:collapse}"
    "td,th{text-align:left;padding:6px 4px;border-bottom:1px solid #eee}"
    "th{color:#666;font-weight:normal;font-size:.9em}"
    "td.p,th.p{text-align:right}"
    ".muted{color:#888;font-size:.85em}"
    ".x{background:#e74c3c;color:#fff;border:none;border-radius:6px;"
    "padding:4px 10px;font-size:1em;cursor:pointer}"
    ".empty{color:#aaa;padding:8px 0}"
    ".pay{background:#1a7f37;color:#fff;border:none;border-radius:8px;"
    "padding:10px 20px;font-size:1.05em;cursor:pointer;margin-top:10px;width:100%}"
    ".pay2{background:#1a7f37;color:#fff;border:none;border-radius:8px;"
    "padding:10px 16px;font-size:1em;cursor:pointer}"
    ".back{background:#999;color:#fff;border:none;border-radius:8px;"
    "padding:10px 16px;font-size:1em;cursor:pointer}"
    "</style></head><body>"
    "<h1 style='text-align:center;margin-bottom:0'>&#128722; Cart-E</h1>"
    "<div style='text-align:center;font-style:italic;color:#666;"
    "font-size:.9em;margin-bottom:12px'>Smart Trolley</div>"
    "<div class='card'><div class='muted'>TOTAL</div>"
    "<div class='total' id='total'>RM 0.00</div>"
    "<div class='muted'><span id='count'>0</span> item(s) in cart"
    " &middot; Trolley: <span id='st'>-</span></div>"
    "<button class='pay' id='payBtn' style='display:none' "
    "onclick='startPay()'>PAY NOW</button></div>"

    // DEMO QR below. For a REAL DuitNow QR: replace the whole
    // <svg>...</svg> block with <img src='data:image/png;base64,...'>
    "<div class='card' id='payCard' style='display:none;text-align:center'>"
    "<div class='muted'>SCAN TO PAY</div>"
    "<div style='font-size:1.5em;font-weight:bold;margin:6px 0' id='payAmt'></div>"
    "<svg width='140' height='140' viewBox='0 0 29 29' style='background:#fff;margin:8px auto;display:block'>"
    "<rect x='1' y='1' width='7' height='7' fill='#000'/><rect x='2.5' y='2.5' width='4' height='4' fill='#fff'/><rect x='3.5' y='3.5' width='2' height='2' fill='#000'/>"
    "<rect x='21' y='1' width='7' height='7' fill='#000'/><rect x='22.5' y='2.5' width='4' height='4' fill='#fff'/><rect x='23.5' y='3.5' width='2' height='2' fill='#000'/>"
    "<rect x='1' y='21' width='7' height='7' fill='#000'/><rect x='2.5' y='22.5' width='4' height='4' fill='#fff'/><rect x='3.5' y='23.5' width='2' height='2' fill='#000'/>"
    "<g fill='#000'><rect x='10' y='2' width='2' height='2'/><rect x='14' y='1' width='2' height='2'/><rect x='17' y='4' width='2' height='2'/>"
    "<rect x='10' y='7' width='2' height='2'/><rect x='13' y='9' width='2' height='2'/><rect x='2' y='10' width='2' height='2'/>"
    "<rect x='6' y='12' width='2' height='2'/><rect x='9' y='13' width='3' height='3'/><rect x='15' y='12' width='2' height='2'/>"
    "<rect x='19' y='10' width='2' height='2'/><rect x='23' y='11' width='2' height='2'/><rect x='26' y='14' width='2' height='2'/>"
    "<rect x='3' y='15' width='2' height='2'/><rect x='13' y='17' width='2' height='2'/><rect x='17' y='16' width='3' height='2'/>"
    "<rect x='22' y='18' width='2' height='2'/><rect x='10' y='20' width='2' height='2'/><rect x='14' y='22' width='2' height='2'/>"
    "<rect x='18' y='21' width='2' height='2'/><rect x='21' y='24' width='3' height='2'/><rect x='12' y='25' width='2' height='2'/>"
    "<rect x='16' y='26' width='2' height='2'/><rect x='25' y='26' width='2' height='2'/></g></svg>"
    "<div class='muted'>Demo QR - DuitNow style</div>"
    "<div style='margin-top:12px'>"
    "<button class='pay2' onclick='confirmPay()'>I have paid</button> "
    "<button class='back' onclick='hidePay()'>Back</button></div></div>"

    "<div class='card'><div class='muted'>LAST SCANNED</div>"
    "<div class='item' id='item'>-</div></div>"
    "<div class='card'><div class='muted'>SCANNED ITEMS</div>"
    "<table id='cart'><tr class='empty'><td>Nothing scanned yet</td></tr></table></div>"
    "<div class='card'><div class='muted'>PRICE LIST</div>"
    "<table><tr><th>Item</th><th class='p'>Price (RM)</th></tr>");

  for (int i = 0; i < NUM_ITEMS; i++) {
    html += F("<tr><td>");
    html += shopItems[i].name;
    html += F("</td><td class='p'>");
    html += String(shopItems[i].price, 2);
    html += F("</td></tr>");
  }

  html += F(
    "</table></div>"
    "<div class='muted' style='text-align:center'>Updates every 2 seconds"
    "<br><i style='font-size:.8em'>Cart-E</i></div>"
    "<script>"
    "let curTotal=0;"
    "async function tick(){try{"
    "let r=await fetch('/data');let d=await r.json();"
    "curTotal=d.total;"
    "document.getElementById('total').textContent='RM '+d.total.toFixed(2);"
    "document.getElementById('count').textContent=d.count;"
    "document.getElementById('st').textContent=d.st;"
    "document.getElementById('payBtn').style.display=d.count>0?'block':'none';"
    "document.getElementById('payAmt').textContent='RM '+d.total.toFixed(2);"
    "document.getElementById('item').textContent="
    "d.item=='-'?'-':d.item+' \\u2014 RM '+d.price.toFixed(2);"
    "let t=document.getElementById('cart');"
    "if(d.cart.length==0){t.innerHTML=\"<tr class='empty'><td>Nothing scanned yet</td></tr>\";}"
    "else{let h='';for(let e of d.cart){"
    "h+=`<tr><td>${e.n}</td><td class='p'>${e.p.toFixed(2)}</td>"
    "<td class='p'><button class='x' onclick='cancelItem(${e.id})'>&#10005;</button></td></tr>`;}"
    "t.innerHTML=h;}"
    "}catch(e){}}"
    "async function cancelItem(id){"
    "if(!confirm('Remove this item?'))return;"
    "try{await fetch('/cancel?id='+id);}catch(e){}"
    "tick();}"
    "function startPay(){"
    "if(!confirm('Pay RM '+curTotal.toFixed(2)+'?'))return;"
    "document.getElementById('payCard').style.display='block';"
    "document.getElementById('payCard').scrollIntoView({behavior:'smooth'});}"
    "function hidePay(){document.getElementById('payCard').style.display='none';}"
    "async function confirmPay(){"
    "try{await fetch('/pay');}catch(e){}"
    "hidePay();alert('Payment successful! Thank you for shopping with Cart-E');"
    "tick();}"
    "tick();setInterval(tick,2000);"
    "</script></body></html>");

  server.send(200, "text/html", html);
}

void handleData() {
  String json;
  json.reserve(250 + cartCount * 45);
  json += F("{\"item\":\"");
  json += lastItemName;
  json += F("\",\"price\":");
  json += String(lastItemPrice, 2);
  json += F(",\"total\":");
  json += String(totalPrice, 2);
  json += F(",\"count\":");
  json += cartCount;
  json += F(",\"st\":\"");
  json += currentMove;
  json += F("\",\"cart\":[");
  for (int i = 0; i < cartCount; i++) {
    if (i > 0) json += ',';
    json += F("{\"id\":");
    json += cartId[i];
    json += F(",\"n\":\"");
    json += shopItems[cartItem[i]].name;
    json += F("\",\"p\":");
    json += String(shopItems[cartItem[i]].price, 2);
    json += F("}");
  }
  json += F("]}");
  server.send(200, "application/json", json);
}

/* ============================================================
 *  CANCEL — remove one cart entry by its unique ID
 * ============================================================ */
void handleCancel() {
  int id = server.arg("id").toInt();

  for (int i = 0; i < cartCount; i++) {
    if (cartId[i] == id) {
      int idx = cartItem[i];
      totalPrice -= shopItems[idx].price;
      if (totalPrice < 0.005) totalPrice = 0;

      beep(1, 40);                           // tiny blip = removed
      Serial.print(F("CANCELLED: "));
      Serial.print(shopItems[idx].name);
      Serial.print(F("  | new total RM "));
      Serial.println(totalPrice, 2);

      showRemoved(shopItems[idx].name, shopItems[idx].price);

      for (int j = i; j < cartCount - 1; j++) {
        cartItem[j] = cartItem[j + 1];
        cartId[j]   = cartId[j + 1];
      }
      cartCount--;

      server.send(200, "text/plain", "ok");
      return;
    }
  }
  server.send(404, "text/plain", "not found");
}

/* ============================================================
 *  PAY — the demo checkout. Celebrates, resets the cart, and
 *  all stickers become scannable again for the next shopper.
 * ============================================================ */
void handlePay() {
  if (cartCount == 0) {
    server.send(400, "text/plain", "cart empty");
    return;
  }

  float paid = totalPrice;

  Serial.print(F("PAYMENT RECEIVED: RM "));
  Serial.print(paid, 2);
  Serial.print(F(" for "));
  Serial.print(cartCount);
  Serial.println(F(" item(s). Cart reset."));

  cartCount     = 0;
  totalPrice    = 0.0;
  lastItemName  = "-";
  lastItemPrice = 0.0;

  showPaid(paid);
  happyMelody();

  server.send(200, "text/plain", "ok");
}
