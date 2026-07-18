# Cart-E Testing Guide — Notes for Faezah

Hi Faezah! This explains everything you need to test the Cart-E movement code
with the students without me around. The code file is **cart_e_movement_test.ino**
(movement only — no RFID or screen yet, that comes later).

---

## 1. What the robot is supposed to do

Cart-E is a trolley that follows a person like a **duckling follows its mother**.
It has 6 ultrasonic "eyes":

- **U1 (front center)** — watches the person and measures distance
- **U2 (front-left) & U3 (front-right)** — angled outward like a fan `\ | /`,
  they catch the person when he/she turns a corner
- **U4 (back-left) & U5 (back-right)** — point SIDEWAYS, they are "bodyguards"
  that stop the trolley swinging into racks when it turns
- **U6 (back center)** — points backward, stops it reversing into things

**Important:** the robot cannot tell a human from a rack — ultrasonic only
measures distance. It follows *whatever was in front of it when it started*,
like a duckling. That's why testing needs open space (see section 4).

## 2. The rules the robot follows (distance from U1, the front eye)

| Person's distance | Robot does | Light |
|---|---|---|
| Under 27 cm | REVERSE (backs away politely) | Green |
| 27 – 33 cm | STOP (the "sweet spot") | Green |
| 33 – 150 cm | FORWARD (follows) | Green |
| Over 150 cm | LOST — stop + one long beep | Yellow |
| Vanishes from front, appears in LEFT fan | TURN LEFT to chase | Green |
| Vanishes from front, appears in RIGHT fan | TURN RIGHT to chase | Green |
| Something in BOTH side fans, nothing in front | WAIT (confused, stands still) | Yellow |

And the bodyguards can VETO any move (robot stops + double beep):
- Wants to reverse but something behind (U6 under 10 cm) → refuses
- Wants to turn but something beside it (U4/U5 under 10 cm) → refuses
  (when a trolley rotates, its back corner swings outward — that's the danger)
- Driving forward while scraping something (U4/U5 under 5 cm) → stops

## 3. Starting and stopping

- **The one latching button = power AND emergency stop.**
  Press once → latches ON → lights turn GREEN, one happy beep, robot follows.
  Press again → releases OFF → robot stops instantly, RED light, three beeps.
- Light colours: **Green** = happy/following · **Yellow** = wait for me /
  confused · **Red** = stopped/asleep.
- The small onboard button labelled "1" on the purple board shares the same
  pin — holding it down is a quick hands-free test of on/off.
- Also flip the Robo ESP32 board's own power switch on first, and make sure
  the little buzzer MUTE switch is ON (otherwise no sounds).

## 4. Test procedure (do it in this order!)

**A. Wheels-in-the-air first.** Put the trolley on a box so wheels spin freely.
   Use your hand as the "person":
   1. Hand ~40 cm in front → wheels spin FORWARD
   2. Hand ~30 cm → wheels STOP
   3. Hand ~15 cm → wheels spin BACKWARD
   4. Remove hand from front, place it ~40 cm at the front-LEFT sensor →
      wheels spin opposite ways (turning left)
   5. Same on the right side
   6. Hand behind the back sensor while triggering reverse → robot refuses
      + double beep (this is the bodyguard working — it's a feature!)

**B. Floor test in OPEN SPACE.** At least 1.5 m clear all around. Walls,
   chairs, and spectators closer than ~60 cm to the side sensors look like
   "humans" to the robot and confuse it. If students crowd around in a
   circle, ask them to stand back.

**C. Corner-turn test.** Walk in front, then turn sharply left or right.
   The robot should rotate to chase you, then follow normally again.

## 5. The Serial Monitor — your best friend

Connect the laptop, open Arduino IDE → Serial Monitor → **115200 baud**.
You'll see the robot's "thoughts" live, like:

```
U1:35 U2:44 U3:52 U4:88 U5:91 U6:120  ->  FORWARD
U1:14 U2:45 U3:51 U4:86 U5:89 U6:8    ->  STOP  (bodyguards vetoed: REVERSE)
```

U1–U6 are the six distances in cm (999 = sees nothing). The word after
the arrow is the decision. "Bodyguards vetoed" means the safety check
blocked a dangerous move — show the students this line, they love it.

## 6. Troubleshooting cheat-sheet

| Problem | Likely cause | Fix |
|---|---|---|
| A sensor always reads 999 | Trig/Echo wires swapped | Swap that sensor's two signal wires (or its two numbers in the pin arrays) |
| A wheel spins the wrong direction | Motor wired backward | Swap that motor's 2 wires at the green terminal — do NOT change code |
| Robot turns the wrong way when person turns | U2 and U3 cables swapped left/right | Swap the two sensors' plugs |
| No sounds at all | Buzzer MUTE switch off | Flip the small switch near the buzzer on the purple board |
| Robot randomly turns toward shelves | Side sensors seeing furniture | Move to more open space, or lower PERSON_RANGE (see below) |
| Robot won't wake up | Board power switch off, or button wiring | Check board switch, then try holding onboard button "1" |
| Lights don't come on | NeoPixel brightness/wiring | Check D15 wiring; onboard 2 pixels should always work |

## 7. Tuning knobs (top of the code file, all in cm)

If behaviour needs adjusting, change ONE number, re-upload, retest:

- `FOLLOW_DIST = 30` and `DEAD_ZONE = 3` → the stop sweet spot (27–33)
- `LOST_DIST = 150` → how far before "person is gone" (crowded room: lower to ~100)
- `PERSON_RANGE = 60` → side-fan sensitivity for turning
  (turns at shelves it shouldn't: lower to ~40; misses turns: raise to ~80)
- `GUARD_DIST = 10` → how paranoid the bodyguards are

## 8. One physical thing to check before testing

**U2 and U3 must be angled OUTWARD about 30–45 degrees** (like a fan `\ | /`).
If they point straight ahead, the corner-turning feature cannot work — the
person can never "appear" in a side fan. U4/U5 point sideways, U6 backward.

## 9. Explaining it to the students (Year 6 friendly)

- The 6 sensors are the robot's **eyes**; each one shouts a tiny click too
  high to hear and times the echo — like a bat.
- The front 3 eyes are the **brain's helpers** (where is my friend?),
  the back 3 are **bodyguards** (is this move safe?).
- Every loop: *find the friend → choose a move → ask the bodyguards →
  only then move.* Safety always wins over wanting.
- The robot is a **duckling**: it follows whoever it saw first, and yes,
  it can be fooled — that's an honest engineering limitation, and knowing
  it is part of good engineering.

Have fun testing — and jot down anything weird (with the Serial Monitor
line if possible) so we can fix it together after. Thank you Faezah!
