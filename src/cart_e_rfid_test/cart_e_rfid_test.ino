/*
 * ============================================================
 *  CART-E RFID READER TEST — read sticker UIDs with the PN532
 * ============================================================
 *  Board  : NodeMCU ESP32 (30-pin) on the CYTRON ROBO ESP32
 *
 *  WHAT THIS DOES:
 *  A little helper tool for registering your shop items.
 *  Tap any RFID sticker/card on the PN532 and the Serial
 *  Monitor shows its UID — PLUS a ready-made line you can
 *  copy-paste straight into the shopItems list of the full
 *  Cart-E program. Just fill in the name and price!
 *
 *  WIRING (same as the full program):
 *   - PN532 into GROVE PORT 2 (or the Maker Port)
 *     -> that is I2C: SDA = D21, SCL = D22
 *   - Set the two DIP switches ON THE PN532 MODULE to I2C mode:
 *       switch 1 = ON (1),  switch 2 = OFF (0)
 *
 *  HOW TO USE:
 *   1. Upload, open Serial Monitor at 115200.
 *   2. Tap each sticker one by one.
 *   3. Copy each printed line into shopItems in the full code.
 *
 *  LIBRARY NEEDED: Adafruit PN532
 * ============================================================
 */

#include <Wire.h>
#include <Adafruit_PN532.h>

// I2C mode, IRQ and RESET not wired (-1, -1 is the official way)
Adafruit_PN532 rfid(-1, -1, &Wire);

// Remember the last card so holding a sticker on the reader
// doesn't flood the screen with the same UID again and again.
uint8_t lastUid[7] = {0};
uint8_t lastUidLen = 0;
unsigned long lastSeenAt = 0;

void setup() {
  Serial.begin(115200);
  delay(500);                 // give the Serial Monitor a moment
  Serial.println();
  Serial.println("=== CART-E RFID UID READER ===");

  Wire.begin(21, 22);         // I2C on the Robo ESP32 Grove/Maker port

  rfid.begin();
  uint32_t version = rfid.getFirmwareVersion();
  if (!version) {
    Serial.println("PN532 NOT FOUND! Check:");
    Serial.println(" - Is it plugged into Grove Port 2 / Maker Port?");
    Serial.println(" - Are the DIP switches set to I2C (1=ON, 2=OFF)?");
    Serial.println(" - Are SDA/SCL wires swapped?");
    while (true) { delay(1000); }   // stop here until wiring is fixed
  }

  // Say hello — print the chip and firmware we found
  Serial.print("Found PN5");
  Serial.println((version >> 24) & 0xFF, HEX);
  Serial.print("Firmware version: ");
  Serial.print((version >> 16) & 0xFF);
  Serial.print(".");
  Serial.println((version >> 8) & 0xFF);

  rfid.SAMConfig();           // get ready to read cards
  Serial.println();
  Serial.println("Ready! Tap a sticker on the reader...");
  Serial.println("--------------------------------------");
}

void loop() {
  uint8_t uid[7];
  uint8_t uidLen;

  // Wait up to 100 ms for a card, then loop again
  if (!rfid.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100)) {
    return;
  }

  // Same card still sitting on the reader? Skip the repeat.
  bool sameCard = (uidLen == lastUidLen) &&
                  (memcmp(uid, lastUid, uidLen) == 0) &&
                  (millis() - lastSeenAt < 2000);
  lastSeenAt = millis();
  if (sameCard) return;

  memcpy(lastUid, uid, uidLen);
  lastUidLen = uidLen;

  // --- Show the UID in plain form ---
  Serial.println();
  Serial.print("Sticker found! UID (");
  Serial.print(uidLen);
  Serial.print(" bytes): ");
  for (int i = 0; i < uidLen; i++) {
    if (uid[i] < 0x10) Serial.print("0");   // keep 2 digits, e.g. 0A
    Serial.print(uid[i], HEX);
    if (i < uidLen - 1) Serial.print(" ");
  }
  Serial.println();

  // --- The magic part: a ready-to-paste shopItems line! ---
  Serial.println("Copy this line into shopItems (fill in name & price):");
  Serial.print("  { {");
  for (int i = 0; i < uidLen; i++) {
    Serial.print("0x");
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    if (i < uidLen - 1) Serial.print(", ");
  }
  Serial.print("}, ");
  Serial.print(uidLen);
  Serial.println(", \"ITEM NAME HERE\", 0.00 },");
  Serial.println("--------------------------------------");

  // A tiny pause so one tap = one print
  delay(300);
}
