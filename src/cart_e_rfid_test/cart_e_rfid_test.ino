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
 *   - Known sticker  -> "boop!" on the buzzer, the OLED shows the
 *     item name + price for 20 seconds, then goes back to the
 *     running TOTAL — exactly how the real trolley will behave.
 *   - Unknown sticker-> "beep-beep", OLED says "Unknown item", and
 *     the Serial Monitor prints a ready-to-paste line to register it.
 *
 *  SOUNDS (built-in buzzer on D23 — MUTE switch must be ON!):
 *    1 solid beep  = item added
 *    2 quick beeps = unknown sticker
 *    tiny blip     = item cancelled from the dashboard
 *
 *  OLED: 0.96" 128x64 blue I2C display (SSD1306). It shares the
 *  SAME two I2C wires as the PN532 (SDA=D21, SCL=D22) — plug it
 *  into the Maker Port / Grove Port 2, or parallel the wires.
 *  PN532 answers at address 0x24, OLED at 0x3C — no clash.
 *
 *  MINI WEB DASHBOARD:
 *  The ESP32 broadcasts its own WiFi network. Connect a phone or
 *  laptop to it and open the fixed address in a browser:
 *      WiFi name : CartE-Dashboard   password: carte1234
 *      Address   : http://192.168.4.1
 *  The page shows the full price list, the scanned items with a
 *  cancel button, the last scanned item and the running total —
 *  it refreshes itself every 2 seconds.
 *  (Prefer joining your own router with a static IP instead?
 *   Set USE_ACCESS_POINT to false and fill in the WiFi section.)
 *
 *  Open Serial Monitor at 115200.
 *  LIBRARIES NEEDED: Adafruit PN532, Adafruit SSD1306,
 *                    Adafruit GFX Library
 *  (WiFi + WebServer are built into the ESP32 core — free!)
 * ============================================================
 */

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>          // the ESP32's built-in WiFi
#include <WebServer.h>     // a tiny web server, also built in

#define SDA_PIN     21
#define SCL_PIN     22
#define PN532_IRQ   33
#define PN532_RESET 25
#define BUZZER      23     // built-in buzzer on the Robo ESP32
                           // (its MUTE switch must be ON!)

// Real IRQ + RESET pins — the way that actually works on our module.
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// The 0.96" blue OLED (128x64) on the same I2C bus, address 0x3C.
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// Timer for showing a scanned item for 20 seconds, then the total.
// millis() = a stopwatch, so nothing freezes while we wait.
unsigned long itemShownAt = 0;
bool          showingItem = false;
const unsigned long SHOW_ITEM_MS    = 20000;  // scanned item: 20 s
const unsigned long SHOW_REMOVED_MS = 5000;   // removed item: 5 s
unsigned long showForMs = SHOW_ITEM_MS;       // how long the current screen stays

/* ============================================================
 *  WIFI + DASHBOARD SETTINGS
 * ============================================================ */

// true  = the trolley makes ITS OWN WiFi network (best for demos —
//         works anywhere, address is always http://192.168.4.1)
// false = join your router below with a static IP of your choice
#define USE_ACCESS_POINT true

// Access Point mode (trolley's own network):
const char *AP_NAME     = "CartE-Dashboard";
const char *AP_PASSWORD = "carte1234";        // min 8 characters

// Router mode (only used if USE_ACCESS_POINT is false):
const char *WIFI_NAME     = "YourWifiName";
const char *WIFI_PASSWORD = "YourWifiPassword";
IPAddress staticIP(192, 168, 1, 50);   // pick a free IP on your network
IPAddress gateway (192, 168, 1, 1);    // usually your router's address
IPAddress subnet  (255, 255, 255, 0);

WebServer server(80);        // the little web server, on normal port 80

// What the dashboard shows about the last scan:
String lastItemName  = "-";
float  lastItemPrice = 0.0;

/* ============================================================
 *  THE CART — the list of scanned items
 *  Every scan becomes an entry with its own ID number, so the
 *  dashboard's cancel button always removes exactly the right
 *  one (even if the same item was scanned three times).
 * ============================================================ */
const int MAX_CART = 30;       // plenty for a demo shop
int cartItem[MAX_CART];        // which shopItems[] index was scanned
int cartId[MAX_CART];          // unique ID for each entry
int cartCount = 0;             // how many entries in the cart now
int nextId = 1;                // ID counter (1, 2, 3, ...)

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

// Beep n times using tone() — the piezo needs a musical note.
// 2000 Hz = a clear "robot beep" pitch.
void beep(int n, int ms) {
  for (int i = 0; i < n; i++) {
    tone(BUZZER, 2000);
    delay(ms);
    noTone(BUZZER);
    if (i < n - 1) delay(ms);
  }
}

/* ============================================================
 *  SETUP — the proven step-by-step startup with diagnostics
 * ============================================================ */
void setup() {
  Serial.begin(115200);
  delay(1500); // let Serial Monitor attach after reset

  pinMode(BUZZER, OUTPUT);  // buzzer ready before anything beeps

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

  Serial.println(F("=== STEP 6: Starting the OLED display ==="));
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("WARNING: OLED not found at 0x3C - check its"));
    Serial.println(F("wiring (it should have appeared in the Step 2"));
    Serial.println(F("bus scan). Continuing without the screen."));
  } else {
    Serial.println(F("OK - OLED alive"));
  }
  oled.setTextColor(SSD1306_WHITE);
  showTotal();                       // screen starts at RM 0.00

  Serial.println(F("=== STEP 7: Starting WiFi + dashboard ==="));
  if (USE_ACCESS_POINT) {
    WiFi.softAP(AP_NAME, AP_PASSWORD);
    Serial.print(F("WiFi network created: "));
    Serial.println(AP_NAME);
    Serial.print(F("Dashboard address: http://"));
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
      Serial.print(F("Dashboard address: http://"));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("WARNING: WiFi not found - dashboard offline,"));
      Serial.println(F("but RFID + OLED keep working normally."));
    }
  }
  server.on("/", handleDashboard);   // the page people open
  server.on("/data", handleData);    // fresh numbers, every 2 s
  server.on("/cancel", handleCancel);// remove one cart entry by ID
  server.begin();

  beep(1, 80);                       // little "shop open!" chime

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
  // Answer any phone/laptop asking for the dashboard.
  server.handleClient();

  // Has the item/removal display finished? Back to the total.
  if (showingItem && millis() - itemShownAt >= showForMs) {
    showingItem = false;
    showTotal();
  }

  uint8_t uid[7];
  uint8_t uidLength;

  // Timeout is 100 ms now (was 1000): with a 1-second wait the web
  // dashboard would feel laggy, since the ESP32 can't answer the
  // browser while it's waiting for a card. 100 ms taps still work
  // perfectly — the loop just asks 10x more often.
  boolean success =
    nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength, 100);
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

      if (cartCount >= MAX_CART) {
        Serial.println(F("CART FULL! Cancel something first."));
        beep(3, 60);                        // grumpy triple beep
        return;
      }

      totalPrice += shopItems[i].price;
      lastItemName  = shopItems[i].name;    // remember for the dashboard
      lastItemPrice = shopItems[i].price;

      // Put it in the cart with its own ID number
      cartItem[cartCount] = i;
      cartId[cartCount]   = nextId++;
      cartCount++;

      beep(1, 100);                         // "boop!" = item added

      Serial.println();
      Serial.print(F("ITEM ADDED: "));
      Serial.print(shopItems[i].name);
      Serial.print(F("  -  RM "));
      Serial.println(shopItems[i].price, 2);
      Serial.print(F("Running total: RM "));
      Serial.println(totalPrice, 2);
      Serial.println(F("--------------------------------------"));

      showItem(shopItems[i].name, shopItems[i].price);
      return;
    }
  }

  // --- Not in the database: show UID + a ready-to-paste line ---
  beep(2, 50);                              // "beep-beep" = not recognised

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

  showUnknown();
}

/* ============================================================
 *  OLED SCREENS — same look as the real trolley will have
 * ============================================================ */

// The everyday screen: the running total, in big numbers.
void showTotal() {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Your total:"));
  oled.setCursor(0, 24);
  oled.setTextSize(2);               // big friendly numbers
  oled.print(F("RM "));
  oled.println(totalPrice, 2);
  oled.display();
}

// A scanned item: name + price, stays for 20 seconds.
void showItem(const char *name, float price) {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Item added:"));
  oled.setCursor(0, 16);
  oled.setTextSize(1);
  oled.println(name);
  oled.setCursor(0, 34);
  oled.setTextSize(2);
  oled.print(F("RM "));
  oled.println(price, 2);
  oled.display();

  showingItem = true;
  showForMs   = SHOW_ITEM_MS;        // stays for 20 seconds
  itemShownAt = millis();            // start the stopwatch
}

// An item cancelled from the dashboard: quick 5-second notice.
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
  showForMs   = SHOW_REMOVED_MS;     // shorter: 5 seconds
  itemShownAt = millis();
}

// A sticker we don't know.
void showUnknown() {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println(F("Unknown item :("));
  oled.println(F(""));
  oled.println(F("Ask staff for help"));
  oled.display();

  showingItem = true;                // also returns to total after 20 s
  showForMs   = SHOW_ITEM_MS;
  itemShownAt = millis();
}

/* ============================================================
 *  THE WEB DASHBOARD
 *  handleDashboard = the page itself (sent once when opened)
 *  handleData      = fresh numbers as JSON; the page asks for
 *                    them every 2 seconds and updates itself
 * ============================================================ */

void handleDashboard() {
  // Build the page. The price list table is filled from shopItems,
  // so adding items to the code automatically updates the dashboard.
  String html;
  html.reserve(3500);
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
    "</style></head><body>"
    "<h1 style='text-align:center;margin-bottom:0'>&#128722; Cart-E</h1>"
    "<div style='text-align:center;font-style:italic;color:#666;"
    "font-size:.9em;margin-bottom:12px'>Smart Trolley</div>"
    "<div class='card'><div class='muted'>TOTAL</div>"
    "<div class='total' id='total'>RM 0.00</div>"
    "<div class='muted'><span id='count'>0</span> item(s) in cart</div></div>"
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
    "async function tick(){try{"
    "let r=await fetch('/data');let d=await r.json();"
    "document.getElementById('total').textContent='RM '+d.total.toFixed(2);"
    "document.getElementById('count').textContent=d.count;"
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
    "tick();setInterval(tick,2000);"
    "</script></body></html>");

  server.send(200, "text/html", html);
}

void handleData() {
  // JSON with the cart list, e.g.:
  // {"item":"Susu","price":3.50,"total":7.00,"count":2,
  //  "cart":[{"id":1,"n":"Susu","p":3.50},{"id":2,"n":"Egg","p":13.50}]}
  String json;
  json.reserve(200 + cartCount * 45);
  json += F("{\"item\":\"");
  json += lastItemName;
  json += F("\",\"price\":");
  json += String(lastItemPrice, 2);
  json += F(",\"total\":");
  json += String(totalPrice, 2);
  json += F(",\"count\":");
  json += cartCount;
  json += F(",\"cart\":[");
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
 *  CANCEL — remove one cart entry by its ID
 *  Called by the dashboard's red X button: /cancel?id=7
 *  Using the unique ID (not the row position) means the button
 *  always removes exactly the entry the user clicked, even if
 *  more items were scanned since the page loaded.
 * ============================================================ */
void handleCancel() {
  int id = server.arg("id").toInt();

  for (int i = 0; i < cartCount; i++) {
    if (cartId[i] == id) {
      int idx = cartItem[i];
      totalPrice -= shopItems[idx].price;      // give the money back
      if (totalPrice < 0.005) totalPrice = 0;  // tidy tiny rounding dust

      beep(1, 40);                             // tiny blip = item removed

      Serial.println();
      Serial.print(F("CANCELLED: "));
      Serial.print(shopItems[idx].name);
      Serial.print(F("  -RM "));
      Serial.println(shopItems[idx].price, 2);
      Serial.print(F("New total: RM "));
      Serial.println(totalPrice, 2);
      Serial.println(F("--------------------------------------"));

      showRemoved(shopItems[idx].name, shopItems[idx].price);

      // Close the gap: slide everything after it one step left
      for (int j = i; j < cartCount - 1; j++) {
        cartItem[j] = cartItem[j + 1];
        cartId[j]   = cartId[j + 1];
      }
      cartCount--;

      server.send(200, "text/plain", "ok");
      return;
    }
  }
  server.send(404, "text/plain", "not found"); // already removed
}
