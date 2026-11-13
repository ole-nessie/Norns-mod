# BeatStep Pro setup

The BeatStep Pro (BSP) is used in its **Control Mode** as the hands-on
controller. Everything it sends is on **MIDI channel 16**, which the synth
treats as the controller channel. The machines are on MIDI channels 1-15.
Channel 16 cannot also hold a machine, because the knobs and buttons share it.

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

Every encoder: **Mode: Control (CC)**, **Channel: 16**, **Option: Relative #1**.
The synth reads that as 64 = no change, 65 = +1, 63 = -1. If a turn goes the
wrong way or jumps, it is Relative #2 instead (1 = +1, 127 = -1); say so and
the one constant `kRelativeEncoding` gets switched.

| Encoder | CC | Use |
|---|---|---|
| Top row 1-4 | 1, 2, 3, 4 | upper row of the OLED page (slots 1-4) |
| Top row 5 | 5 | machine on the selected channel |
| Top row 6 | 6 | kit on the selected channel |
| Top row 7-8 | 7, 8 | reserved |
| Bottom row 9-12 (left four) | 9, 10, 11, 12 | lower row of the OLED page (slots 5-8) |
| Bottom row 13-16 | 13, 14, 15, 16 | reserved |

Encoder 5 walks the same machine list as the MACHINE menu: EFM DR, SID, METAL, 808, FM, FM4, 303, CHORD, then empty, and wraps. The channel's parameters reset when the machine actually changes.

Encoder 6 walks the kits that belong to the machine already on the channel, the same list KIT LOAD shows. An empty channel sees every kit, and loading one puts that kit's machine on the channel. The header shows the kit name until a parameter or the machine encoder is turned. A machine with no saved kits of its own (SID, FM, FM4, 303, CHORD, unless you have saved some) does not change. Metal starts at the first Metal kit, 808 at the first 808 kit, and EFM drums at slot 29.

If the device settings have **Knob acceleration**, start with *Slow* or
*Medium*; it only changes how fast values move.

### Number buttons (the row labelled 1-16)

Buttons 1-15 select the machine. Button 16 releases every held note. Each of
1-16: **Mode: Note**, **Channel: 16**, **Play mode: Gate**.

MCC shows note names with middle C (60) = C3. If a name lands an octave
higher than the table, enter every name one octave lower.

| Button | Note name | Number | Selects |
|---|---|---|---|
| 1-8 | E2 F2 F#2 G2 G#2 A2 A#2 B2 | 52-59 | machines 1-8 |
| 9-15 | C3 C#3 D3 D#3 E3 F3 F#3 | 60-66 | machines 9-15 |
| 16 | G3 | 67 | panic: note-off on every held note, all channels |

CC 120 and CC 123 do the same, on channel 16 or on any machine channel. Drums finish their hit, and delay, reverb, chorus, and drive keep their tails.

An empty machine can be selected, then given a machine from the menu.

### Pads

The numbered buttons select the machine. The pads are not channel buttons.
Page and audition stay on the bottom row: **Mode: Note**, **Channel: 16**,
**Play mode: Gate**.

| Pad | Note name | Number | Use |
|---|---|---|---|
| Top row 1 | C1 | 36 | master delay |
| Top row 2 | C#1 | 37 | master reverb |
| Top row 3 | D1 | 38 | master chorus |
| Top row 4 | D#1 | 39 | master drive |
| Bottom row 1 | G#1 | 44 | machine page 1 |
| Bottom row 2 | A1 | 45 | machine page 2 |
| Bottom row 3 | A#1 | 46 | machine page 3 |
| Bottom row 4 | B1 | 47 | MIX on every machine |
| Bottom row 8 | D#2 | 51 | audition (plays the selected machine while held) |

Click the physical pad position in the MCC picture rather than going by pad
numbers, since the BSP numbers its pads from the bottom row. Top-row pads
1-4 and the bottom-row pads above are **Mode: Note**, **Channel: 16**,
**Play mode: Gate**. The other top-row pads can keep their current notes.

Top-row pads 1-4 open the master effect pages: delay (TIME, FDBK, DTNE,
DLEV), reverb (SIZE, DECA, RTNE, RLEV), chorus (MODE, DPTH, CLEV) and drive
(DRV, SAG, TONE, LEV). The same encoders edit the page. A bottom-row page pad
or a number button leaves the effect and shows the machine again. Bottom-row
pad 4 is MIX on every machine. Its top encoders are the delay, reverb, chorus
and drive sends, and the first bottom encoder is the channel volume. Pads 1-3
are that machine's sound pages, and a pad past the last sound page does
nothing. The FX menu item
still opens the delay page.

A CC on MIDI channels 1-15 switches the channel shown on the display,
including the channel number while an effect page is up, and opens the page
that holds the parameter. Notes do not switch the channel. Number buttons
1-15 still select a channel directly.

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
- Press number buttons 1, 15 and 16, top pads 1-4, and bottom pads 1-4 and 8:
  channel 16, notes 52, 66, 67, 36, 37, 38, 39, 44, 45, 46, 47 and 51, each with
  a note-off on release.

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
