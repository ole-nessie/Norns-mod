# BeatStep Pro setup

The BeatStep Pro (BSP) is used in its **Control Mode** as the hands-on
controller. Everything it sends is on **MIDI channel 16**, which the synth
treats as the controller channel (voices are on channels 1-8).

Set it up once in Arturia **MIDI Control Center** (MCC), then run the two
checks at the end. Menu names below may differ slightly between MCC versions.

## 1. Back up the current Control Mode setup

1. Connect the BSP to the Mac over USB, open MCC, select the BeatStep Pro.
2. In the template area, use **Recall from BeatStep Pro** (or similar), then
   **Save As** e.g. `BSP original`. You can always send this back.

## 2. Create the template

Start from the recalled template, **Save As** `Drumbox`, then edit each
control by clicking it in the BSP picture and setting the fields on the right.

### Encoders (all 16)

Every encoder: **Mode: Control (CC)**, **Channel: 16**, **Option: Relative**
(pick **Relative #1** if there are several; we confirm the encoding in check A).

| Encoder | CC | Use |
|---|---|---|
| Top row 1-4 | 1, 2, 3, 4 | upper row of the OLED page (slots 1-4) |
| Top row 5-8 | 5, 6, 7, 8 | reserved |
| Bottom row 9-12 (left four) | 9, 10, 11, 12 | lower row of the OLED page (slots 5-8) |
| Bottom row 13-16 | 13, 14, 15, 16 | reserved |

If the device settings have **Knob acceleration**, start with *Slow* or
*Medium*; it only changes how fast values move.

### Pads (all 16)

Every pad: **Mode: Note**, **Channel: 16**, **Play mode: Gate** (note-off on
release), fixed velocity is not needed.

MCC shows note names, with middle C (60) = C3, so 36 = C1. Check A below
confirms it: the top-left pad must show note 36. If it shows 48, enter every
name one octave lower.

| Pad | Note name | Number | Use |
|---|---|---|---|
| Top row 1-8 (left to right) | C1 C#1 D1 D#1 E1 F1 F#1 G1 | 36-43 | select voice / MIDI channel 1-8 |
| Bottom row 1 | G#1 | 44 | page 1 (SYN) |
| Bottom row 2 | A1 | 45 | page 2 (AMP) |
| Bottom row 3 | A#1 | 46 | page 3 (FILT) |
| Bottom row 4-7 | B1 C2 C#2 D2 | 47-50 | reserved |
| Bottom row 8 | D#2 | 51 | audition (plays the selected voice while held) |

Click the physical pad position in the MCC picture rather than going by pad
numbers, since the BSP numbers its pads from the bottom row.

### Store it

**Send to BeatStep Pro** (stores it as the Control Mode setup). On the BSP,
press **CNTRL/SEQ** to switch into Control Mode.

## 3. Checks on the Mac

These need the free command-line tools SendMIDI and ReceiveMIDI:

```sh
brew install gbevin/tools/sendmidi gbevin/tools/receivemidi
sendmidi list     # note the BSP's port name, e.g. "Arturia BeatStep Pro"
```

Close MCC and any DAW first so they don't hold the port.

### Check A: what the BSP sends

```sh
receivemidi dev "Arturia BeatStep Pro"
```

- Turn top encoder 1 slowly right a few clicks, then left. Write down the
  values: `65`/`63` around 64 means *offset binary*; `1`/`127` means *two's
  complement*. Other patterns: copy a few lines.
- Press each top-row pad and bottom pads 1-3 and 8: you should see
  `channel 16 note-on 36 ...` etc. and a note-off on release.

### Check B: can the BSP light its pads from MIDI?

Run the test script from the repo root, with the BSP in Control Mode:

```sh
scripts/bsp-led-test.sh "Arturia BeatStep Pro"
```

It sends one candidate message at a time, prints it, and waits for Enter.
Watch the pads and note which steps light anything (and which pad/colour).
Then run it once more with the BSP in sequencer mode (press CNTRL/SEQ).

The last part sends SysEx in the format the original (non-Pro) BeatStep uses
for its LEDs. That format works on the BSP, but so far only as a brief flash.
After the first run, map the pads and then try a refresh hold:

```sh
scripts/bsp-led-test.sh "Arturia BeatStep Pro" map
scripts/bsp-led-test.sh "Arturia BeatStep Pro" hold
```

`hold` re-sends the same SysEx every 50 ms for 3 seconds per pad. Tested in
Control Mode: pads 0x70-0x77 = bottom 1-8, 0x78-0x7F = top 1-8, but refreshing
only repeats blinks; no steady light and no useful colour/brightness from the
value byte. Steady pad feedback is not usable. The synth may still blink the
focused voice pad on triggers; the OLED is the main feedback.

## 4. Report back

- Check A: encoder values for right and left turns; pad notes if different.
- Check B: which step numbers lit a pad, in which mode.

The synth's mapping constants are then set to match.

## Working with the Octatrack

The Octatrack does not receive CC values back into its MIDI track CTRL pages.
It keeps its own value for each of its 10 assigned CCs and sends it when it
plays (and when a p-locked step ends), overriding what you set on the BSP for
that parameter. Use the OT's 10 CC slots for the parameters you want to
p-lock and watch the norns OLED for names and values while locking. Use the
BSP for everything else.
