/*
  PN532 I2C Diagnostic Sketch — Cytron Robo ESP32
  ------------------------------------------------
  PN532 DIP switches: try SW1 = ON, SW2 = OFF for I2C mode first.
  If Step 2 (bus scan) finds nothing, try the other 3 switch combinations
  before assuming anything else is wrong - clone boards often mislabel these.

  Wiring:
    PN532 VCC  -> 3.3V
    PN532 GND  -> GND
    PN532 SDA  -> GPIO21
    PN532 SCL  -> GPIO22
    PN532 IRQ  -> GPIO33   (optional - only if your board breaks this pin out)
    PN532 RSTPDN/RESET -> GPIO25   (optional - only if broken out)

  IRQ/RESET are not required for basic polling reads, but wiring RESET in
  particular makes nfc.begin() more reliable, since it lets the library
  issue a real hardware reset instead of relying on the chip's power-on state.
*/

#include <Wire.h>
#include <Adafruit_PN532.h>

#define SDA_PIN     21
#define SCL_PIN     22
#define PN532_IRQ   33
#define PN532_RESET 25

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

void blinkForever() {
  pinMode(LED_BUILTIN, OUTPUT);
  while (true) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(200);
  }
}

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
    Serial.println(F("FAIL: no I2C devices found at all."));
    Serial.println(F("This means the PN532 is not electrically visible on"));
    Serial.println(F("the bus yet. Before going further, check:"));
    Serial.println(F("  1) DIP switches - try all 4 combinations, re-run"));
    Serial.println(F("     after each one"));
    Serial.println(F("  2) SDA/SCL wires fully seated, no swap between them"));
    Serial.println(F("  3) VCC is a stable 3.3V measured at the PN532 pin"));
    Serial.println(F("     while powered (multimeter, under load)"));
    Serial.println(F("  4) Try a full power cycle: unplug USB, wait 5s,"));
    Serial.println(F("     replug, then re-run this sketch"));
    blinkForever();
  }
  Serial.print(count);
  Serial.println(F(" device(s) found. PN532 default I2C address is 0x24."));

  Serial.println(F("=== STEP 3: Calling nfc.begin() ==="));
  nfc.begin();
  Serial.println(F("OK - nfc.begin() returned without hanging"));

  Serial.println(F("=== STEP 4: Requesting firmware version ==="));
  uint32_t versiondata = nfc.getFirmwareVersion();

  if (!versiondata) {
    Serial.println(F("FAIL: getFirmwareVersion() returned 0, even though"));
    Serial.println(F("a device answered the bus scan. This usually means"));
    Serial.println(F("the DIP switches are NOT actually in I2C mode (the"));
    Serial.println(F("scan found *something* at that address, but it isn't"));
    Serial.println(F("responding like a PN532), or the chip needs a real"));
    Serial.println(F("hardware reset pulse. If RESET is wired, that should"));
    Serial.println(F("already be handled by nfc.begin() - if not wired,"));
    Serial.println(F("try connecting it."));
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
  Serial.println(F("Ready. Waiting for an NFC/RFID card..."));
}

void loop() {
  uint8_t uid[7];
  uint8_t uidLength;
  boolean success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength, 1000);
  if (success) {
    Serial.print(F("Card UID: "));
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(uid[i] < 0x10 ? "0" : "");
      Serial.print(uid[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
  }
}
