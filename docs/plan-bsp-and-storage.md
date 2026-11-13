# Plan: BeatStep Pro control, persistent state, sets and kits

Status: planned, not started. The known-good synth (kick + SID, tested on the
norns) is the `main` branch, commit message "Snapshot the working kick and SID
synth". The card image for that build is `backups/kernel8-sid-kick.img`.

## Start here

Work only on branch `bsp-and-storage`. Do not commit on `main`. If this work
breaks the device, check out `main` and copy `backups/kernel8-sid-kick.img`
onto the card.

Read "Current state" and "Key design decisions", then do the tasks in order.
Stop after each one so the user can test it on the hardware.

- [x] 1. Several USB MIDI devices at once (Phase 1). Built, not yet tried on the norns.
- [x] 2. BeatStep Pro follows the OLED: encoders, channel pads, page pads,
      audition (Phase 2). Built, not yet tried on the norns. No pad lights.
- [x] 3. Norns keys and encoders (Phase 3). Built, not yet tried on the norns.
- [x] 4. SD card storage and autosave (Phase 4). Built, not yet tried on the norns.
- [x] 5. Sets, kits, and choosing the machine per channel (Phase 5). Built, not yet tried on the norns.

Pad LED feedback is out of scope. A SysEx blink exists
(`F0 00 20 6B 7F 42 02 00 10 <pad> <value> F7`, pad ids 0x70-0x77 bottom row,
0x78-0x7F top row) but it cannot hold a steady light. Do not send it unless
the user asks later.

## Goal

1. Arturia BeatStep Pro (BSP) as a hands-on controller that follows the OLED:
   - 8 encoders laid out like the display's 2x4 grid: BSP top-row encoders
     1-4 = upper display row, bottom-row encoders 9-12 = lower display row.
   - Top-row pads select the MIDI channel / machine.
   - Three pads select the page (SYN / AMP / FILT), one pad auditions.
   - The OLED is the feedback. Do not light the BSP pads.
2. All USB MIDI devices work at once (Octatrack interface + BSP).
3. The state survives power loss (autosave).
4. Norns keys and encoders save/load **sets** (all channels) and **kits**
   (one channel: machine + its parameter values).

## Current state (read before starting)

- Bare-metal Circle (Step51.1.1) kernel for Pi 3B on the norns shield, in
  `src/pi/`. Portable engine and UI in `src/core/` (no C++ std library: C
  headers, libm, `__atomic` builtins only; `-fno-exceptions -fno-rtti`).
  macOS simulator in `src/desktop/` (miniaudio + RtMidi, terminal OLED).
- `Synth` (`src/core/synth.*`): one `Voice*` per MIDI channel, MIDI arrives
  through lock-free `MidiQueue`s (one per producer) and is applied at the start
  of the audio callback, which runs in the I2S DMA IRQ. CC16-39 = parameters
  0-23 of the voice on that channel. `VoiceView` exposes values/lastTouched/
  triggerCount to the UI via atomics.
- `PageView` (`src/core/ui/param_page.*`) shows the focused channel and the page
  of the last-touched parameter (`lastTouched / 8`).
- Voices: `EfmKick` (8 params, ch 1), `SidVoice` (24 params, 3 pages, ch 2).
- `src/pi/kernel.cpp`: `attachUsbMidi()` opens only `"umidi1"` and uses the
  simple packet handler (no device index). The main loop polls USB
  plug-and-play, serial MIDI, and redraws the OLED every 33 ms.
- Circle has what's needed: `addon/SDCard` (`CEMMCDevice`), `addon/fatfs`
  (FatFs), and `CUSBMIDIDevice::SendEventPackets()` plus a packet-handler
  overload with a device index and user pointer
  (`pi-deps/circle/include/circle/usb/usbmidi.h`).
- Build: `scripts/build-pi.sh` → `dist/sdcard/`; desktop via CMake; offline
  checks in `build/check.cpp` (not in git).

Shield pins (from the schematic; nothing implemented yet):
E1 GPIO 4/27, E2 GPIO 24/23, E3 GPIO 12/25, K1 GPIO 22, K2 GPIO 26,
K3 GPIO 13 (keys active low, enable pull-ups).

## Key design decisions

**BSP traffic is recognised by MIDI channel, not by USB device.** The BSP's
Control Mode is set up (in Arturia MIDI Control Center) to send everything on
one dedicated channel, **16** (decided by the user; `kControllerChannel`,
constant in core). Messages on that channel go to a new `Controller` layer instead of a
voice. This works the same over USB, DIN and in the desktop simulator, and
needs no USB vendor-ID matching. Consequence: channel 16 can't hold a voice.

**One set of values; everything is saved, including Octatrack CCs.** Each
channel keeps a single parameter array, as today. Changes from the BSP, the
norns, kit/set loading and the Octatrack (including p-locks) all update it and
all count for autosave. After a power cut the sound comes back as it last
was; if the cut happened during a p-locked step, that p-lock value is what
gets restored (the OT resends its own values as soon as it plays again).

**Encoders are relative.** The BSP encoders are set to a relative mode in MCC,
so turning applies a delta to the current value of whatever parameter is on
screen. Nothing jumps when switching page/channel or after the Octatrack
p-locks a value. Arturia offers several relative encodings; support them in
one decode function selected by a constant and confirm the actual bytes with
a MIDI monitor (see Phase 2).

**Explicit focus state.** Today the page is derived from `lastTouched`. Add an
explicit focused channel + page owned by the audio side (atomics, like
`VoiceView`), changed by: pad presses, Octatrack CCs/notes (as now), norns
encoders. `PageView` reads it instead of computing `lastTouched / 8`.

**All state changes go through the audio side.** UI code (main loop, norns
keys, file loading) never calls `Voice::setParam` directly. It pushes
messages into its own `MidiQueue`/command queue that `Synth::process` drains,
keeping the single-writer rule. Loading a kit = push the machine-change
command + a CC per parameter.

**Machine per channel becomes switchable** (needed for kits). Preallocate a
fixed pool: for each usable channel, one instance of every machine type
(memory is tiny). A "set machine" command swaps the channel's `Voice*` on the
audio side; the newly selected instance gets its parameters reset/loaded.
Machine types get stable ids (`"EFMBD"`, `"SID"`) used in files.

**Storage is crash-safe.** Power can be cut any time:
- FAT on the boot partition, files under `/drumbox/`.
- Every file has: magic, format version, payload length, CRC32.
- Autosave uses two alternating files (`state_a.bin`, `state_b.bin`) with a
  sequence number; boot loads the newest one whose CRC is valid. A torn write
  only loses the latest change.
- Writes happen in the main loop (never in IRQ); audio keeps running since it
  is interrupt-driven. Call `f_sync` and close after every write.
- Autosave is debounced: write when values have been unchanged for ~3 s, and
  at most once every ~30 s while values keep changing (the Octatrack can stream
  p-lock CCs constantly while playing).

**File format records parameters by index with the machine id and count.**
Rule for future voices: never reorder parameters of an existing machine, only
append. On load, missing trailing parameters take their defaults.

## Phases

### Phase 1: Multiple USB MIDI devices

- `src/pi/kernel.*`: replace the single `usbMidi_` with an array (e.g. 4).
  On each `UpdatePlugAndPlay()` true, scan `"umidi1"`..`"umidi4"` and attach
  new ones. Use the packet-handler overload that receives the device index
  /user pointer; give each device its own `MidiQueue` (SPSC: one producer per
  queue) added to the synth at init. Handle removal per device.
- No MIDI output to the BSP. Pad lights are out of scope.
- Test: Octatrack interface + BSP both plugged in, both drive the synth (BSP
  in its default mode sending notes on some channel is enough).

### Phase 2: Controller layer (BSP follows the screen)

- New `src/core/controller.{h,cpp}`, called from `Synth::handle` for messages
  on `kControllerChannel`. Pure core code; testable on desktop.
- Focus state in `Synth`: `focusedChannel_`, `focusedPage_` (atomics) plus
  getters; set by pads, by incoming notes/CCs on voice channels (as today,
  page = page of the touched param), and later by norns encoders.
- Mapping table (one place, constants, so it can be changed after the MIDI
  monitor check). Proposed defaults, configured identically in MCC:
  - Encoders: CC 1-4 (top row, BSP encoders 1-4) → display slots 0-3;
    CC 9-12 (bottom row, BSP encoders 9-12) → slots 4-7. Relative mode.
  - Top-row pads → notes 36-43 → select channel 1-8 (only if a voice exists
    there, otherwise ignore).
  - Bottom-row pads 1-3 → notes 44-46 → page 1-3 (ignored past the voice's
    page count). Pad 8 → note 51 → audition (note-on/-off of the last note
    received on that channel, default 60, forwarded to the focused voice).
  - Note: which physical row is "top" and the exact note numbers must be
    verified on the unit; MCC lets you assign each pad's note.
- Relative decode: `int decodeRelative(uint8_t value)` supporting the
  "64 = no change" (offset binary) and two's complement encodings; delta is
  applied with clamping 0..127. Consider acceleration later.
- Edits apply via the same path as CCs: `setParam`, update `VoiceView`,
  `lastTouched`, mark state dirty for autosave.
- Desktop: the simulator already opens all MIDI ports, so the BSP can be
  tested on the Mac first. Add a `--monitor` flag that prints incoming MIDI to
  confirm the BSP's relative encoding and pad notes.
- `docs/beatstep-pro-setup.md` has the MCC template the user sets up; keep the
  mapping constants in sync with it, and update it with what the MIDI monitor
  check finds (relative encoding and pad notes).
- Test: turn BSP encoders, the highlighted cell on the OLED changes; pads
  switch channel and page; audition plays.

### Phase 3: Norns keys and encoders

- New `src/pi/panel.{h,cpp}`: GPIO inputs with pull-ups for K1-K3 and the
  three encoders. Poll in the main loop at ~1 kHz (or GPIO interrupts);
  quadrature decode with a state table; debounce keys (~5 ms).
- Portable event type in core (`PanelEvent{key/encoder, index, delta/down}`)
  so the UI logic is testable on desktop; desktop maps computer keys to it.
- Default (play) screen behaviour: E1 = focused channel, E2 = page,
  E3 = value of the highlighted parameter (all via the UI queue). K2/K3 short
  press = previous/next parameter slot.

### Phase 4: Storage and autosave

- `src/core/preset.{h,cpp}`: data structures and (de)serialisation to a byte
  buffer, CRC32. No file I/O in core.
  - `KitData { char machine[8]; char name[12]; uint8_t count; uint8_t values[kMaxParams]; }`
  - `SetData { char name[12]; KitData channels[8]; }` (MIDI channels 1-8; empty machine
    = no voice on that channel).
- Storage interface `class Storage { bool read(path, buf, max, &len); bool write(path, buf, len); }`
  with `src/pi/sd_storage.*` (CEMMCDevice + FatFs; add `addon/SDCard` and
  `addon/fatfs` libs to `src/pi/Makefile` and build them in
  `scripts/setup-pi.sh`) and `src/desktop/file_storage.*` (a directory, e.g.
  `~/.drumbox/`).
- Snapshot: the main loop builds a `SetData` from `VoiceView`s + current
  machine ids (reads atomics; no audio-side locking needed).
- Autosave as described in "Key design decisions". Boot: load newest valid
  state, push it through the UI queue before audio starts (or apply directly
  before `Start()`, when the audio side is not running yet).
- Verify the FAT partition on the norns card is mounted read-write and that
  writing does not disturb booting (files go in a subfolder).

### Phase 5: Sets, kits and machine selection on the norns

- Prerequisite: machine pool + "set machine" command (see decisions).
- K1 toggles between the play screen and a menu (Elektron "function" feel).
  Menu items, chosen with E2, entered with K3, back with K2:
  - **SET SAVE / SET LOAD**: E3 picks slot 1-16 (`/drumbox/set_01.bin` ...);
    confirm with K3. Names optional (default "SET 01").
  - **KIT SAVE / KIT LOAD**: acts on the focused channel; slot 1-64
    (`/drumbox/kit_01.bin` ...); the list shows the stored machine id.
  - **MACHINE**: E3 picks the machine for the focused channel (resets it to
    defaults, or keeps parameters with the same index if they exist).
- Loading a set replaces all channels; loading a kit replaces one channel's
  machine and values. Both go through the UI queue.
- Show a short confirmation on the OLED ("SAVED SET 03").

## Open questions for the user

Decided:
- Controller channel is 16.
- Octatrack CCs are saved like any other edit (see "One set of values").
- 8 voice channels (MIDI 1-8), matching the 8 top-row pads. `SetData` stores 8
  channels; the machine pool is 8 channels x every machine type.
- Sets and kits are saved/loaded only from the norns controls, not the BSP.

Octatrack interaction to document in `docs/beatstep-pro-setup.md`: the OT does
not receive CC values back into its MIDI track CTRL pages, so it keeps its own
base value for each of its 10 assigned CCs and sends it when playing (and when
a p-locked step ends), overriding the BSP value for that parameter. Recommended workflow: use the OT's 10 CC slots for the parameters to
p-lock (watch the norns OLED for names/values while locking), and the BSP for
everything else.

## Verification checklist

- Desktop: simulator with the BSP over USB; offline tests for relative
  decoding, controller mapping, preset serialisation round trip, CRC failure
  falls back to the other autosave file.
- Pi: both USB devices at once; BSP edits visible on OLED; power cut during
  playback then reboot restores the sound; save/load set and kit via the norns
  controls; audio has no dropouts while saving (listen while autosave writes).
