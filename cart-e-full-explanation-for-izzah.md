# CART-E: The Human-Following Smart Trolley
## Complete Project Explanation for Izzah
### (Everything needed for slides, poster & competition script)

Hi Izzah! This document explains the whole Cart-E project from top to bottom.
Everything here is accurate to the real robot we built and tested — you can
lift any part of it directly into the slides, poster, or script. I've marked
suggestions for each at the end. Latest code and guides are on our GitHub:
https://github.com/bidayatulhidayah/cart-e-smart-trolley

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
1. **Follows its shopper automatically** like a duckling follows its mother —
   no pushing needed, hands stay free
2. **Scans items as you shop** — tap an item's RFID sticker on the trolley
   and the price appears instantly; the screen always shows your running total
3. **Protects everyone around it** — 6 ultrasonic "eyes" make sure it never
   bumps into people or shelves

## SDG alignment (from our planning board)
- **SDG 3 (Good Health & Well-being):** reduces physical strain for elderly,
  pregnant, and disabled shoppers
- **SDG 9 (Industry, Innovation & Infrastructure):** modernising retail
  with affordable, accessible robotics
- **SDG 10 (Reduced Inequalities):** shopping independence for people who
  cannot push a heavy trolley

## One-sentence pitch (memorise this!)
*"Cart-E is a shopping trolley that follows you like a duckling, scans your
shopping as you go, and never bumps into anything — hands-free shopping
for everyone."*

---

# PART 2 — HOW THE ROBOT WORKS

## 2.1 The brain
An **ESP32 microcontroller** (a small computer the size of a thumb) sitting
on a **Cytron Robo ESP32 board** — a Malaysian-made robotics board with the
motor driver, buzzer, and rainbow LEDs built in. The program is written in
**Arduino (C++)** by the students.

## 2.2 The six eyes (ultrasonic sensors)
Each sensor works like a bat: it shouts a click too high for humans to hear,
then times how long the echo takes to bounce back. Sound travels about
0.034 cm every microsecond, so time = distance.

| Eye | Position | Job |
|---|---|---|
| U1 | Front centre | Watches the shopper, measures distance |
| U2 | Front-left, angled outward | Catches the shopper if they turn left |
| U3 | Front-right, angled outward | Catches the shopper if they turn right |
| U4 | Back-left, pointing sideways | Bodyguard: stops it swinging into racks |
| U5 | Back-right, pointing sideways | Bodyguard: other side |
| U6 | Back centre, pointing backward | Bodyguard: safe reversing |

The front three are angled like a fan `\ | /` so there are no blind spots.

## 2.3 The following rules (our calibrated numbers)
Every fraction of a second, the trolley checks the distance to the shopper:

| Shopper's distance | Trolley does |
|---|---|
| Under 27 cm | Reverses politely (personal space!) |
| 27–33 cm | Stops — the "sweet spot" |
| 33–150 cm | Drives forward to follow |
| Over 150 cm | Stops, beeps, waits ("you left me behind!") |

## 2.4 The clever turning (our proudest feature)
When the shopper turns a corner, they **disappear** from the front eye and
**appear** in a corner eye. The trolley turns toward wherever the shopper
appeared, and keeps turning until the front eye finds them again. Key idea:
**it chases the person, not empty space.**

## 2.5 The bodyguards (safety system)
The three back sensors never choose where to go — they only have **veto
power**. Every loop, after the brain picks a move, the bodyguards check it:

- Wants to reverse, but something is behind → **refused** + warning beep
- Wants to turn, but something is beside it → **refused** (when a trolley
  rotates, its back corner swings outward — that's the hidden danger!)
- Driving forward while scraping something on the side → **stops**

Golden rule of our design: **safety always wins over wanting.**
The wheels only ever move after the bodyguards say yes.

## 2.6 The shopping system (RFID)
- Every item wears an **NTAG213 RFID sticker** (13.56 MHz — the exact same
  technology as a Touch 'n Go card!)
- Each sticker has a factory-burned **UID** — a fingerprint no other sticker
  shares
- Tap the item on the trolley's **PN532 reader** → "boop!" → the OLED screen
  shows the item and price for 20 seconds, then returns to the running total
- The prices live in the trolley's code (its "shop book"), not on the
  sticker — exactly how real barcodes work
- Unknown sticker? The screen politely says "Ask staff for help"

## 2.7 One button to rule them all
A single **latching push button** is both the power switch and the emergency
stop: press once → latches ON, trolley wakes up (green light + happy beep).
Press again → springs OFF → everything stops instantly (red light + three
beeps). The shopping total is never lost, even after an emergency stop.

## 2.8 The lights and sounds language
- **Green** = happily following
- **Yellow** = "wait for me!" / confused, standing still
- **Red** = stopped / asleep
- **1 happy beep** = waking up · **1 long beep** = lost you ·
  **2 beeps** = bodyguard refused a move · **3 beeps** = emergency stop

## 2.9 The program's heartbeat (how the code thinks)
Thousands of times per second, the program repeats five steps:
1. **Is the button still ON?** (released = emergency stop)
2. **Where is my shopper?** (read the front 3 eyes)
3. **Choose a move** (forward / stop / reverse / turn)
4. **Ask the bodyguards** (read the back 3 eyes — veto if dangerous)
5. **Move!** — then check if any shopping was scanned

---

# PART 3 — HONEST ENGINEERING (judges LOVE this part)

## The duckling limitation
Ultrasonic sensors only measure distance — a human, a rack, and a box all
"sound" identical. So Cart-E cannot truly recognise its shopper. Instead it
works like a **duckling**: it follows whatever was in front of it when it
woke up. If a stranger walks between the shopper and the trolley, it may
follow the stranger!

**Why admitting this is a strength, not a weakness:** knowing exactly where
your system fails, and designing around it, is real engineering. Our
design responds to the limitation in three ways:
1. The start ritual (tap card, stand in front) tells the trolley who to follow
2. The WAIT case — when confused, it stops rather than guessing
3. The bodyguards don't care WHO they see — rack or human, don't hit it

## Version 2 ideas (our upgrade roadmap — great closing slide)
- **A beacon the shopper carries** (Bluetooth/IR tag) — follow the signal,
  not the shape (how commercial "following luggage" works)
- **Thermal sensor (AMG8833)** — humans are warm, racks are not
- **AI camera (HuskyLens)** — actually recognises and tracks a person
- **Speed control (PWM)** — smooth gentle curves instead of spin turns
- **Checkout integration** — pay at the trolley, skip the queue entirely

---

# PART 4 — KEY FACTS & SPECS (for the poster)

| Spec | Value |
|---|---|
| Brain | ESP32 (dual-core, WiFi + Bluetooth capable) |
| Robot board | Cytron Robo ESP32 (Malaysian-made!) |
| Sensors | 6× ultrasonic (HC-SR04 family) |
| Item scanning | PN532 NFC/RFID reader, NTAG213 stickers (13.56 MHz) |
| Display | 0.96" OLED — item price + running total |
| Follow distance | Stops at 27–33 cm, follows up to 150 cm |
| Safety | 3 rear "bodyguard" sensors with veto power + emergency stop |
| Power | 1-cell Li-ion battery (board charges & protects it) |
| Programming | Arduino C++, written and tuned by the students |
| Logic | 14 behaviour cases (6 movement + 5 safety + 3 system) |

Fun facts for the poster:
- The sensors work like **bats** (echolocation)
- The RFID stickers are the same technology as **Touch 'n Go**
- The trolley makes a decision roughly **30 times every second**
- Total behaviour cases: **14** — the students can name them all!

---

# PART 5 — SUGGESTED SLIDE STRUCTURE (8–10 slides)

1. **Title** — Cart-E logo/photo, team, school
2. **The Problem** — trolley struggles: elderly, hands full, checkout queues
3. **Our Solution** — the one-sentence pitch + 3 features (follow / scan / safe)
4. **How It Follows** — the 6-eyes diagram + distance rule table (2.2, 2.3)
5. **The Clever Turning** — chase the person, not empty space (2.4)
6. **The Bodyguards** — veto system, "safety always wins" (2.5)
7. **Smart Shopping** — RFID tap → price → total (2.6)
8. **Honest Engineering** — the duckling limitation + how we designed around it
9. **Future: Cart-E v2** — the upgrade roadmap
10. **Live Demo / Thank You**

## Suggested demo sequence (rehearse this!)
1. Press the button — wake up, green light *(open space, 1.5 m clear)*
2. Walk away → it follows · stand → it stops at the sweet spot ·
   step closer → it politely reverses
3. Turn a corner → it chases you *(the wow moment)*
4. Place a box behind it, step toward it → it REFUSES to reverse and
   beeps *(narrate: "the bodyguard said no!")*
5. Tap two items → prices appear, total updates
6. Press the button → emergency stop, red light — and the total is
   still safe on screen

## Script tips
- Let the STUDENTS say the analogies: bats, ducklings, bodyguards,
  "safety always wins over wanting" — these are their explanations
- Narrate the demo as it happens ("watch the light turn yellow — it's
  asking me to wait!")
- If something misbehaves during the demo, explain WHY calmly — reading
  the robot's mind out loud impresses judges more than a perfect run

---

# PART 6 — JUDGES' Q&A PREPARATION

**Q: How does it tell a human from a rack?**
A: It can't — ultrasonic only measures distance. It follows whoever it saw
first, like a duckling. We designed around this: the start ritual, the WAIT
case when confused, and bodyguards that protect everything regardless.
(Then mention the v2 upgrades — thermal / AI camera.)

**Q: What happens if someone walks between the shopper and the trolley?**
A: Honestly, it may follow that person. That's the duckling limitation.
The shopper just stands in front again to "re-adopt" it.

**Q: Is it safe around children?**
A: Safety is checked before every single movement — the bodyguards veto
any dangerous move, and it stops the moment it's confused. Plus the
emergency button kills all movement instantly. It also moves at gentle
kid-safe speed.

**Q: Why 6 sensors? Why not fewer/more?**
A: Front 3 = a fan that tracks the shopper even through turns. Back 3 =
one for reversing, two for the corner-swing danger when rotating. Every
sensor has one specific job; we actually used every pin the board has!

**Q: What was the hardest part?**
A: (Students answer honestly! Good examples: calibrating the distances,
discovering that turning needed "chase the person, not the gap," finding
that pin 24 doesn't exist on the ESP32...)

**Q: How much would it cost / could shops afford it?**
A: Our prototype is under ~RM300 in parts. A real shop version would cost
more, but far less than self-checkout machines — and it replaces both the
trolley AND part of the checkout.

**Q: What about theft — someone leaving without paying?**
A: Great question — our v2 roadmap includes checkout integration where the
trolley itself becomes the payment point at the exit gate. (It's fine to
say "that's a future version problem we've already thought about.")

---

# PART 7 — GLOSSARY (simple words for the script)

- **Microcontroller** — a tiny computer that runs one program forever
- **Ultrasonic sensor** — measures distance using echoes, like a bat
- **RFID / NFC** — "radio fingerprints"; each sticker has a unique ID
- **UID** — the sticker's fingerprint number
- **I2C** — a shared "party line" that lets the screen and RFID reader
  talk to the brain over the same two wires
- **Calibration** — testing and tuning the numbers until behaviour is right
- **Veto** — the power to say NO and block a decision

---

Anything you need expanded — a specific slide's wording, poster layout
text, or a full word-for-word script — just ask and we'll draft it
together. Thank you Izzah!
