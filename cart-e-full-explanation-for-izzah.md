# CART-E: The Human-Following Smart Trolley
## Complete Project Explanation for Izzah — UPDATED (Full System)
### (Everything needed for slides, poster & competition script)

Hi Izzah! Updated version — the robot is now COMPLETE and tested, and it
gained some big new features since the first document: a **live phone
dashboard**, a **cancel button**, a **PAY NOW checkout**, and a
**scan-once rule**. Everything below matches the real working robot.
Latest code and guides: https://github.com/bidayatulhidayah/cart-e-smart-trolley

---

# PART 1 — THE STORY (for the opening of slides/script)

## The problem
Shopping trolleys haven't changed in almost 100 years. Shoppers push heavy
trolleys with both hands while managing children, checking shopping lists,
and carrying bags. For the elderly, pregnant mothers, and people with
disabilities, pushing a heavy trolley is genuinely difficult. And at the
end, everyone queues at the checkout just to find out the total price.

## Our solution
**Cart-E** — a smart trolley that:
1. **Follows its shopper automatically** like a duckling — no pushing,
   hands stay free
2. **Scans items as you shop** — tap an item on the trolley, the price
   appears instantly on the trolley's screen AND on your phone
3. **Lets you pay at the trolley** — a PAY NOW button and QR code on
   your phone; no checkout queue at all
4. **Protects everyone around it** — 6 ultrasonic "eyes" make sure it
   never bumps into people or shelves

## SDG alignment
- **SDG 3 (Good Health & Well-being):** reduces physical strain for elderly,
  pregnant, and disabled shoppers
- **SDG 9 (Industry, Innovation & Infrastructure):** modernising retail
  with affordable, accessible robotics
- **SDG 10 (Reduced Inequalities):** shopping independence for people who
  cannot push a heavy trolley

## One-sentence pitch (memorise this!)
*"Cart-E follows you like a duckling, scans your shopping as you go, and
lets you pay right at the trolley — hands-free shopping with no queues,
for everyone."*

---

# PART 2 — HOW THE ROBOT WORKS

## 2.1 The brain
An **ESP32 microcontroller** (a tiny computer with built-in WiFi) on a
**Cytron Robo ESP32 board** — Malaysian-made, with the motor driver,
buzzer and rainbow LEDs built in. Programmed in **Arduino (C++)** and
calibrated by the students through real testing.

## 2.2 The six eyes (ultrasonic sensors)
Each sensor works like a bat: it shouts a click too high for humans to
hear, then times the echo. Time = distance.

| Eye | Position | Job |
|---|---|---|
| U1 | Front centre | Watches the shopper, measures distance |
| U2 | Front-left, angled outward 30–45° | Catches the shopper if they turn left |
| U3 | Front-right, angled outward 30–45° | Catches the shopper if they turn right |
| U4 | Back-left, pointing sideways | Bodyguard: stops it swinging into racks |
| U5 | Back-right, pointing sideways | Bodyguard: other side |
| U6 | Back centre, pointing backward | Bodyguard: safe reversing |

The front three form a fan `\ | /` so there are no blind spots.

## 2.3 The following rules (calibrated on the real robot)

| Shopper's distance | Trolley does |
|---|---|
| Under 27 cm | Reverses politely (personal space!) |
| 27–33 cm | Stops — the "sweet spot" |
| 33–150 cm | Follows forward |
| Over 150 cm | Stops, beeps, waits ("you left me behind!") |

## 2.4 The clever turning (our proudest movement feature)
When the shopper turns a corner, they **disappear** from the front eye
and **appear** in a corner eye. The trolley turns toward wherever they
appeared, until the front eye finds them again. Key idea: **it chases
the person, not empty space.**

## 2.5 The bodyguards (safety system)
The three back sensors never choose where to go — they only have **veto
power**. They refuse reversing into something, refuse turns when the rear
corner would swing into a rack, and stop the trolley if it's scraping
along something. Golden rule: **safety always wins over wanting.**

## 2.6 The shopping system (RFID) — now with real items!
- 10 real products registered: Susu, Potato Chip, Sandwich Cookies,
  Morning Coffee, Orange Juice, Oatmeal, Snacks, Veggies, Fruits, Egg
- Each wears an RFID sticker (13.56 MHz — the same technology as a
  **Touch 'n Go card**) with a factory-burned unique fingerprint (UID)
- Tap an item → "boop!" → the trolley's OLED screen shows the item and
  price for 20 seconds, then the running total
- **Scan-once rule:** each item can only be in the cart once. Tap it
  again → politely refused ("Already in cart") — unless it's been
  cancelled first. This prevents accidental double-charging!
- Unknown sticker → "Ask staff for help"
- Prices live in the trolley's code (its "shop book"), not on the
  sticker — exactly how real barcodes work

## 2.7 The live phone dashboard (NEW — a wow feature!)
The trolley broadcasts its **own WiFi network** (no internet needed):
- Connect a phone to **CartE-Dashboard** → open **http://192.168.4.1**
- The page shows, updating live every 2 seconds:
  - The **total** in big green numbers
  - The trolley's **live status** (FORWARD, TURN LEFT, STOPPED...)
  - The **scanned items list**, each with a red ✕ **cancel button**
  - The full **price list**
- **Cancel:** tap ✕ → confirm → item removed, money refunded, the
  trolley blips and its screen shows "Item removed." The sticker can
  now be scanned again.

## 2.8 PAY NOW — checkout at the trolley (NEW!)
When the cart has items, a green **PAY NOW** button appears:
1. Tap → confirm the amount
2. A payment screen appears with a **QR code** (DuitNow-style demo)
3. "I have paid" → the trolley plays a **happy three-note melody**, the
   screen says "Payment received — Thank you!", and the cart resets to
   RM 0.00 for the next shopper
- Honest note for Q&A: the QR is a demo — a production version would
  integrate a certified payment provider (business step, not an
  engineering barrier)

## 2.9 One button + shopping while parked
A single **latching push button** = power AND emergency stop. Press once
→ ON (green light, happy beep). Press again → instant stop (red light,
three beeps). **Clever detail:** scanning, the dashboard and PAY NOW
keep working while the trolley is parked — so the natural checkout is:
stop the trolley, pay on your phone, done. The total is never lost.

## 2.10 The light & sound language
- **Green** = following · **Yellow** = "wait for me!"/confused ·
  **Red** = stopped
- **1 beep** = awake / item added · **2 beeps** = refused or unknown ·
  **3 beeps** = emergency stop · **long beep** = lost you ·
  **melody** = payment complete

## 2.11 The program's heartbeat
About 30 times per second the program repeats: answer the phone
dashboard → check for scanned items → check the button → find the
shopper (front 3 eyes) → choose a move → ask the bodyguards (back 3
eyes) → move. Only if the bodyguards say YES do the wheels turn.

---

# PART 3 — HONEST ENGINEERING (judges LOVE this part)

## The duckling limitation
Ultrasonic sensors only measure distance — a human, a rack, and a box
all "sound" identical. Cart-E follows **whatever it saw first, like a
duckling**, and yes, it can be fooled if a stranger walks in between.
Our design responds honestly: the start ritual tells it who to follow,
the WAIT case stops it guessing when confused, and the bodyguards
protect everything regardless of what it is.

## Real problems we hit and solved (great "journey" slide!)
- The RFID library's shortcut setup failed on our module — the students
  wrote a step-by-step diagnostic program and found the working setup
- Discovered the ESP32 has **no pin 24** (a silent buzzer bug!)
- Discovered neighbouring Grove ports **share pins** — so the wiring
  plan had to skip every other port
- Turning logic v1 failed — the fix was the insight: *chase the person,
  not the empty space*
- Calibrated every distance through real floor testing

## Version 2 ideas (upgrade roadmap — great closing slide)
- **Beacon** the shopper carries (Bluetooth/IR) — follow the signal
- **Thermal sensor** (AMG8833) — humans are warm, racks are not
- **AI camera** (HuskyLens) — truly recognises and tracks a person
- **Speed control (PWM)** — smooth curves instead of spin turns
- **Real payment integration** — certified DuitNow/FPX at the trolley

---

# PART 4 — KEY FACTS & SPECS (for the poster)

| Spec | Value |
|---|---|
| Brain | ESP32 (dual-core, WiFi + Bluetooth) |
| Robot board | Cytron Robo ESP32 (Malaysian-made!) |
| Sensors | 6× ultrasonic (HC-SR04 family) |
| Item scanning | PN532 NFC/RFID reader, 13.56 MHz stickers, 10 items |
| Display | 0.96" OLED + live WiFi phone dashboard |
| Checkout | Cancel button + PAY NOW with QR (demo) |
| Follow distance | Stops at 27–33 cm, follows up to 150 cm |
| Safety | 3 rear "bodyguard" sensors with veto power + emergency stop |
| Power | 1-cell Li-ion (board charges & protects it) |
| Programming | Arduino C++, written and calibrated by the students |
| Logic | 14 behaviour cases + scan-once rule + full checkout flow |

Fun facts for the poster:
- The sensors work like **bats** (echolocation)
- The RFID stickers are the same technology as **Touch 'n Go**
- The trolley makes a decision about **30 times every second**
- The trolley broadcasts its **own WiFi** — no internet needed
- You can **cancel an item and pay from your phone**

---

# PART 5 — SUGGESTED SLIDE STRUCTURE (10–11 slides)

1. **Title** — Cart-E photo, team, school
2. **The Problem** — heavy trolleys, full hands, checkout queues
3. **Our Solution** — the pitch + 4 features (follow / scan / pay / safe)
4. **How It Follows** — 6-eyes diagram + distance table
5. **The Clever Turning** — chase the person, not empty space
6. **The Bodyguards** — veto system, "safety always wins"
7. **Smart Shopping** — RFID tap → OLED → scan-once rule
8. **The Dashboard & PAY NOW** — phone screenshots! cancel + QR checkout
9. **Honest Engineering** — the duckling + real problems we solved
10. **Future: Cart-E v2** — the upgrade roadmap
11. **Live Demo / Thank You**

## The full demo sequence (rehearse this!)
1. Power on → chime → connect a phone to CartE-Dashboard, open
   192.168.4.1 — hand the phone to a judge!
2. Press the button — green light, "AWAKE"
3. Walk away → follows · stand → stops at the sweet spot · step
   closer → reverses politely (judge sees "Trolley: FORWARD/STOP"
   live on the phone)
4. Turn a corner → it chases you *(the movement wow moment)*
5. Box behind it + step toward it → it REFUSES to reverse and beeps
   ("the bodyguard said no!")
6. Scan 3 items → boop, OLED prices, phone list grows
7. Scan the same item again → politely refused *(scan-once rule)*
8. Cancel one item from the phone → blip, "Item removed"
9. Press the button → trolley parks ("Scanning still works")
10. **PAY NOW** → QR → "I have paid" → melody + "Thank you!" →
    cart resets *(the closing wow moment)*

## Script tips
- Let the STUDENTS say the analogies: bats, ducklings, bodyguards,
  "safety always wins over wanting," "it chases the person, not the
  empty space"
- Hand a judge the phone early — a live dashboard in their hands is
  worth three slides
- If something misbehaves, read the robot's mind out loud from the
  Serial Monitor — calm debugging impresses more than a perfect run

---

# PART 6 — JUDGES' Q&A PREPARATION

**Q: How does it tell a human from a rack?**
A: It can't — ultrasonic only measures distance. It follows whoever it
saw first, like a duckling. We designed around it: the start ritual, the
WAIT case, bodyguards that protect everything. V2 would add a thermal
sensor or AI camera.

**Q: What if someone walks between the shopper and the trolley?**
A: Honestly, it may follow that person. The shopper just stands in front
again to "re-adopt" it.

**Q: How do you pay?** *(they WILL ask — and we have a great answer!)*
A: On your phone: PAY NOW → QR code → done, and the cart resets. The QR
is a demo; a real shop would plug in a certified payment provider —
that's a business integration, not an engineering barrier.

**Q: What stops someone scanning an item twice by accident?**
A: The scan-once rule — each sticker can only be in the cart once.
And if you change your mind, the cancel button refunds it instantly.

**Q: What about theft — leaving without paying?**
A: The trolley knows exactly what's in it and whether it's paid — a
production version would only release at an exit gate after PAY NOW.
We've already thought about that flow.

**Q: Is it safe around children?**
A: Every single movement passes a safety check first; the bodyguards
veto anything dangerous, it stops when confused, and one button kills
all movement instantly. It also drives at a gentle kid-safe speed.

**Q: Does the dashboard need internet?**
A: No — the trolley broadcasts its own WiFi network. It works anywhere,
even in a hall with no internet at all.

**Q: What was the hardest part?**
A: (Students answer honestly!) Good examples: the RFID module refusing
the standard setup until we wrote a diagnostic program; discovering
pin 24 doesn't exist; realising the turning logic had to chase the
person, not the gap.

**Q: How much does it cost?**
A: Prototype under ~RM300 in parts — far cheaper than a self-checkout
machine, and it replaces both the trolley AND part of the checkout.

---

# PART 7 — GLOSSARY (simple words for the script)

- **Microcontroller** — a tiny computer that runs one program forever
- **Ultrasonic sensor** — measures distance using echoes, like a bat
- **RFID / NFC** — "radio fingerprints"; each sticker has a unique ID (UID)
- **I2C** — a shared "party line": the screen and RFID reader talk to
  the brain over the same two wires
- **Access Point** — the trolley making its own WiFi network
- **Dashboard** — the live webpage on your phone
- **Calibration** — testing and tuning numbers until behaviour is right
- **Veto** — the power to say NO and block a decision

---

Need a specific slide's wording, poster layout text, phone screenshots
described, or a full word-for-word script? Just ask. Thank you Izzah!