# 🛒 Cart-E — The Human-Following Smart Trolley

*A shopping trolley that follows you like a duckling, scans your shopping
as you go, and lets you pay right at the trolley — hands-free shopping
with no queues, for everyone.*

Built by primary school (Year 6) students for robotics competition, coached
by their teachers. Programmed in Arduino C++ on an ESP32.

---

## ✨ Features

- **Human following** — walks behind its shopper, keeps a polite 27–33 cm
  "sweet spot", reverses if you step too close, follows up to 150 cm
- **Smart turning** — when the shopper turns a corner, the trolley chases
  *the person*, not empty space, until its front eye finds them again
- **Bodyguard safety system** — 3 rear ultrasonic sensors with veto power:
  no reversing into things, no swinging into racks, instant stop when
  squeezed. *Safety always wins over wanting.*
- **RFID shopping** — tap an item, hear a boop, see the price on the OLED
  for 20 s, then the running total. Scan-once rule prevents double charges.
- **Live phone dashboard** — the trolley broadcasts its own WiFi; a phone
  shows the total, live trolley status, the cart with **cancel buttons**,
  and the price list, refreshing every 2 seconds. No internet needed.
- **PAY NOW checkout** — QR payment screen (demo), happy melody, thank-you
  screen, cart resets for the next shopper
- **One-button design** — a single latching push button is both power
  switch and emergency stop; shopping keeps working while parked

## 🔩 Hardware

| Part | Notes |
|---|---|
| NodeMCU ESP32 (30-pin) | The brain (WiFi built in) |
| Cytron Robo ESP32 board | Motor driver, buzzer (D23), 2× NeoPixel (D15) built in |
| 6× HC-SR04 ultrasonic | 3 front (fan `\ | /`), 2 side guards, 1 back guard |
| PN532 RFID reader | I2C (only VCC/GND/SDA/SCL wired), DIP: SW1=ON SW2=OFF |
| RFID stickers 13.56 MHz | Same technology as Touch 'n Go |
| 0.96" OLED SSD1306 | I2C, address 0x3C, shares the bus with the PN532 |
| Latching push button SPST | Power + emergency stop, on D35 |
| 2× DC geared motors | M1 left (D12/D13), M2 right (D14/D27) |
| 1-cell Li-ion battery | Charged & protected by the board |

### Pin map

| Sensor / part | Connection | Pins |
|---|---|---|
| U1 front | Grove 1 | trig D16, echo D17 |
| U2 front-left | Grove 3 | trig D26, echo D25 |
| U3 front-right | Grove 5 | trig D33, echo D32 |
| U4 back-left | Servo S pins | trig D4, echo D18 |
| U5 back-right | Servo S pins | trig D5, echo D19 |
| U6 back | Breakout | trig D2, echo D36 ("VP") |
| RFID + OLED | Grove 2 / Maker Port | I2C: SDA D21, SCL D22 |

⚠️ Hard-won wiring lessons: neighbouring Grove ports **share pins** (use
1, 3, 5 only); Grove 7 is input-only; and the ESP32 has **no pin 24** —
the buzzer is on D23!

## 📁 What's in this repo

| File / folder | What it is |
|---|---|
| `cart_e_trolley/` | ⭐ **The full system** — movement + RFID + OLED + dashboard + PAY NOW. Upload this for the competition. |
| `cart_e_movement_test/` | Movement only (following, turning, bodyguards) with live Serial debugging — for driving tests and tuning |
| `cart_e_rfid_test/` | Shopping only (RFID + OLED + dashboard + pay) — bench-testable on a table, no motors needed |
| `cart-e-flowchart-and-cases.md` | The full logic: flowchart, all 14 behaviour cases, pin map, build notes |
| `cart-e-testing-guide-for-faezah.md` | Step-by-step movement testing guide for coaches |
| `cart-e-rfid-testing-guide-for-faezah.md` | Step-by-step RFID testing guide for coaches |
| `cart-e-full-explanation-for-izzah.md` | Complete project explanation for slides, poster, script & judges' Q&A |

## 🚀 Getting started

1. **Arduino IDE** with the ESP32 board package installed
   (board: *ESP32 Dev Module* or *NodeMCU-32S*)
2. **Libraries** (Library Manager): `Adafruit PN532`, `Adafruit SSD1306`,
   `Adafruit GFX Library`, `Adafruit NeoPixel`
   (say yes when it offers `Adafruit BusIO`)
3. Open `cart_e_trolley/cart_e_trolley.ino`, upload, open Serial Monitor
   at **115200**
4. Flip the board's power switch, make sure the buzzer **MUTE switch is
   ON**, press the latching button — green lights, and it follows!

### The dashboard

- Connect a phone to WiFi **`CartE-Dashboard`** (password `carte1234`)
- Open **http://192.168.4.1**
- Watch the total, cancel items with ✕, and check out with **PAY NOW**

### Registering new items

Tap an unregistered sticker — the Serial Monitor prints a ready-to-paste
line for the `shopItems` list. Fill in the name and price, re-upload.

## 🎛️ Tuning

All behaviour numbers live in one block at the top of the code
(`DISTANCE RULES`) — change one number, re-upload, retest:

- `FOLLOW_DIST` / `DEAD_ZONE` — the stop sweet spot (default 27–33 cm)
- `LOST_DIST` — "person is gone" distance (default 150 cm)
- `PERSON_RANGE` — corner-eye sensitivity for turning (default 60 cm)
- `GUARD_DIST` — how paranoid the bodyguards are (default 10 cm)

## 🦆 Honest engineering

Ultrasonic sensors only measure distance — a human, a rack, and a box all
"sound" identical. Cart-E follows whatever it saw first, **like a
duckling**, and can be fooled if a stranger walks in between. We designed
around it honestly: the start ritual, a WAIT case instead of guessing,
and bodyguards that protect everything regardless of what it is.

**v2 roadmap:** carried beacon · thermal sensor (AMG8833) · AI camera
(HuskyLens) · PWM speed control · real payment integration.

## 🙏 Team

Students of Year 6 (the engineers!), coached by Husna, Faezah, Izzah
and the team. Board and inspiration: Cytron Robo ESP32 🇲🇾