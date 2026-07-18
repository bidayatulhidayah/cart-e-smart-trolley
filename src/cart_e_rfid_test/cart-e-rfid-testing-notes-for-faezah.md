# Cart-E RFID Testing Guide — Notes for Faezah

Hi Faezah! This one is for testing the **RFID scanning system** with the students.
The code file is **cart_e_rfid_test.ino** (on the GitHub repo). It's a small
helper tool — no motors involved this time, so this test can be done safely
on a table with just the board, the PN532 reader, and the laptop.

---

## 1. What we are testing

Every RFID sticker has a **UID** — a unique ID number burned in at the
factory, like a fingerprint. The trolley's "shop database" works by matching
UIDs to item names and prices. Today's job:

1. Check the PN532 reader is wired and talking properly.
2. Tap every sticker and collect its UID.
3. Build the price list for the full program.

Kid-friendly version: *"every sticker has a secret name only the reader can
hear. Today we are writing down everyone's secret name in our shop book."*

## 2. Hardware setup (2 minutes)

- Plug the **PN532 into Grove Port 2** (or the Maker Port — same thing,
  they share the I2C line, SDA = D21, SCL = D22).
- **Check the two tiny DIP switches on the PN532 module itself:**
  **switch 1 = ON, switch 2 = OFF** — this puts it in I2C mode.
  This is the #1 cause of "reader not found," so check it first!
- Power the board by USB from the laptop. Motors/battery not needed.

## 3. Running the test

1. Open **cart_e_rfid_test.ino** in Arduino IDE, upload it.
2. Open Serial Monitor, set **115200** baud.
3. You should see:
   - `Found PN5...` and a firmware version → the reader is alive!
   - `Ready! Tap a sticker on the reader...`
4. Tap a sticker flat on the reader. Two things print:

```
Sticker found! UID (7 bytes): 04 A3 2F 8B 5C 11 80
Copy this line into shopItems (fill in name & price):
  { {0x04, 0xA3, 0x2F, 0x8B, 0x5C, 0x11, 0x80}, 7, "ITEM NAME HERE", 0.00 },
```

That second line is the treasure — it's pre-formatted for the full
Cart-E program. **Copy it into a text file / Notepad as you go.**

## 4. The registration workflow (great as a 2-student job)

- **Student A** = the Scanner: taps stickers one at a time, sticks each
  scanned sticker onto its real item (juice box, biscuit packet...)
  immediately so stickers and items never get mixed up.
- **Student B** = the Shopkeeper: copies each printed line into Notepad,
  replaces `ITEM NAME HERE` with the item name and `0.00` with the price.

Example of a finished line:
```
  { {0x04, 0xA3, 0x2F, 0x8B, 0x5C, 0x11, 0x80}, 7, "Apple juice", 3.50 },
```

When all items are done, paste all the lines into the `shopItems` list in
the full program (**cart_e_trolley.ino**), replacing my 3 example lines,
and upload. The shop is now live — tapping a sticker on the trolley will
show its price on the OLED for 20 seconds, then the running total.

## 5. Troubleshooting cheat-sheet

| Problem | Fix |
|---|---|
| "PN532 NOT FOUND" at startup | DIP switches wrong (must be 1=ON, 2=OFF for I2C). Then check it's in Grove Port 2 / Maker Port, and try swapping the two signal wires (SDA/SCL reversed) |
| Nothing prints when tapping | Hold the sticker FLAT against the reader, right over the antenna area, for a second. Range is only ~1–3 cm |
| Sticker works but reads slowly | Metal surfaces behind the sticker kill the signal — don't test stickers while they're on cans/foil packets |
| Same sticker prints only once | That's on purpose! It skips repeats while a sticker sits on the reader. Lift it off and tap again if you want a re-read |
| Weird characters in Serial Monitor | Baud rate wrong — must be 115200 |
| Two stickers give the SAME UID | Almost impossible with genuine tags — more likely the same sticker was tapped twice. Re-check |

## 6. Things worth knowing (and telling the students)

- Our stickers are **NTAG213 (13.56 MHz)** — the same technology as
  Touch 'n Go cards and hotel key cards. Students can try tapping their
  own cards on the reader: they will show UIDs too! (Fun demo — but
  don't add bus cards to the shop list 😄)
- UIDs are usually **7 bytes** for NTAG213 stickers and **4 bytes** for
  many keychain fobs/cards. Both are fine; the printed line handles it.
- The sticker doesn't store the price — the price lives in OUR code.
  The sticker only says its name; our program looks the name up in the
  shop book. (Nice discussion point: that's how real shop barcodes
  work too!)
- Avoid sticking tags directly onto metal items for the demo — pick
  boxes, bottles, and packets instead.

## 7. What "done" looks like today

- [ ] Reader detected at startup, firmware version printed
- [ ] Every sticker tapped, each stuck on its item
- [ ] A Notepad file with one completed line per item (name + price filled)
- [ ] (Bonus) Lines pasted into cart_e_trolley.ino, uploaded, and one
      item tap shows the price on the OLED

If anything strange happens, copy the Serial Monitor output into WhatsApp
and we'll sort it out together. Thank you Faezah!
