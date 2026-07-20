/*
 * ============================================================
 *  CART-E RFID SHOP TEST — based on the PROVEN working code
 * ============================================================
 *  Board  : NodeMCU ESP32 (30-pin) on the CYTRON ROBO ESP32
 *
 *  This uses the exact init sequence that WORKS on our PN532:
 *  real IRQ and RESET pins wired, so the library can give the
 *  chip a proper hardware reset (the -1,-1 shortcut failed).
 *
 *  BENCH WIRING — only 4 wires are physically connected:
 *    PN532 VCC  -> 3.3V
 *    PN532 GND  -> GND
 *    PN532 SDA  -> GPIO21
 *    PN532 SCL  -> GPIO22
 *    DIP switches: SW1 = ON, SW2 = OFF (I2C mode)
 *
 *  IRQ and RESET are NOT wired. The numbers 33/25 below are only
 *  software declarations the library wants — in I2C mode it never
 *  actually reads IRQ, and the reset pulse goes to an empty pin.
 *
 *  WHAT THIS DOES:
 *  All 10 shop items are already registered below. Tap an item:
 *   - Known sticker  -> prints the item name + price ("the shop works!")
 *   - Unknown sticker-> prints the UID + a ready-to-paste line so
 *                       you can add new items easily.
 *
 *  Open Serial Monitor at 115200.
 *  LIBRARY NEEDED: Adafruit PN532
 * ============================================================
 */

#include <Wire.h>
#include <Adafruit_PN532.h>

#define SDA_PIN     21
#define SCL_PIN     22
#define PN532_IRQ   33
#define PN532_RESET 25

// Real IRQ + RESET pins — the way that actually works on our module.
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// Some ESP32 boards don't define LED_BUILTIN — use GPIO2 as fallback.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

/* ============================================================
 *  THE SHOP DATABASE — our 10 registered items
 *  (EDIT THE PRICES to the real ones you want for the demo!)
 * ============================================================ */
struct Item {
  uint8_t uid[7];    // the sticker's fingerprint
  uint8_t uidLen;    // our stickers all have 4-byte UIDs
  const char *name;  // what the item is
  float price;       // price in RM  <-- EDIT THESE!
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

float totalPrice = 0.0;   // running total, just like the real trolley

// Remember the last card so holding a sticker on the reader
// doesn't add the same item ten times in a row.
uint8_t lastUid[7] = {0};
uint8_t lastUidLen = 0;
unsigned long lastSeenAt = 0;

void blinkForever() {
  pinMode(LED_BUILTIN, OUTPUT);
  while (true) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(200);
  }
}

/* ============================================================
 *  SETUP — the proven step-by-step startup with diagnostics
 * ============================================================ */
void setup() {
  Serial.begin(115200);
  delay(1500); // let Serial Monitor attach after reset

  Serial.println();
  Serial.println(F("=== STEP 0: Boot OK, Serial is alive ==="));

  Serial.println(F("=== STEP 1: Initializing I2C bus ==="));
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println(F("OK - Wire.begin(21, 22) done"));

  Serial.println(F("=== STEP 2: Scanning I2C bus for any device ==="));
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("  Found device at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      count++;
    }
  }
  if (count == 0) {
    Serial.println(F("FAIL: no I2C devices found at all. Check:"));
    Serial.println(F("  1) DIP switches - try all 4 combinations"));
    Serial.println(F("  2) SDA/SCL wires fully seated, not swapped"));
    Serial.println(F("  3) VCC is a stable 3.3V at the PN532 pin"));
    Serial.println(F("  4) Full power cycle: unplug 5s, replug, re-run"));
    blinkForever();
  }
  Serial.print(count);
  Serial.println(F(" device(s) found. PN532 default address is 0x24."));

  Serial.println(F("=== STEP 3: Calling nfc.begin() ==="));
  nfc.begin();
  Serial.println(F("OK - nfc.begin() returned without hanging"));

  Serial.println(F("=== STEP 4: Requesting firmware version ==="));
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println(F("FAIL: device answered the scan but not like a"));
    Serial.println(F("PN532. Re-check DIP switches, or wire RESET so"));
    Serial.println(F("the library can hardware-reset the chip."));
    blinkForever();
  }
  Serial.print(F("SUCCESS: Found chip PN5"));
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print(F("Firmware version: "));
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.');
  Serial.println((versiondata >> 8) & 0xFF, DEC);

  Serial.println(F("=== STEP 5: SAMConfig (enable card reading) ==="));
  nfc.SAMConfig();

  Serial.println();
  Serial.print(F("SHOP OPEN! "));
  Serial.print(NUM_ITEMS);
  Serial.println(F(" items registered. Tap an item..."));
  Serial.println(F("--------------------------------------"));
}

/* ============================================================
 *  LOOP — tap items, watch the shop work
 * ============================================================ */
void loop() {
  uint8_t uid[7];
  uint8_t uidLength;

  boolean success =
    nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength, 1000);
  if (!success) return;

  // Same sticker still sitting on the reader? Skip the repeat
  // (otherwise one tap could add an item many times).
  bool sameCard = (uidLength == lastUidLen) &&
                  (memcmp(uid, lastUid, uidLength) == 0) &&
                  (millis() - lastSeenAt < 2000);
  lastSeenAt = millis();
  if (sameCard) return;
  memcpy(lastUid, uid, uidLength);
  lastUidLen = uidLength;

  // --- Look the sticker up in the shop database ---
  for (int i = 0; i < NUM_ITEMS; i++) {
    if (uidLength == shopItems[i].uidLen &&
        memcmp(uid, shopItems[i].uid, uidLength) == 0) {

      totalPrice += shopItems[i].price;

      Serial.println();
      Serial.print(F("ITEM ADDED: "));
      Serial.print(shopItems[i].name);
      Serial.print(F("  -  RM "));
      Serial.println(shopItems[i].price, 2);
      Serial.print(F("Running total: RM "));
      Serial.println(totalPrice, 2);
      Serial.println(F("--------------------------------------"));
      return;
    }
  }

  // --- Not in the database: show UID + a ready-to-paste line ---
  Serial.println();
  Serial.print(F("UNKNOWN sticker! UID: "));
  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
  Serial.println(F("To register it, copy this line into shopItems:"));
  Serial.print(F("  { {"));
  for (uint8_t i = 0; i < uidLength; i++) {
    Serial.print(F("0x"));
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
    if (i < uidLength - 1) Serial.print(F(", "));
  }
  Serial.print(F("}, "));
  Serial.print(uidLength);
  Serial.println(F(", \"ITEM NAME\", 0.00 },"));
  Serial.println(F("--------------------------------------"));
}
