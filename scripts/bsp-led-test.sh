#!/usr/bin/env bash
# Tries ways of lighting BeatStep Pro pads from MIDI, one step at a time.
# Usage: scripts/bsp-led-test.sh "Arturia BeatStep Pro"   (name from `sendmidi list`)
set -euo pipefail

dev="${1:-Arturia BeatStep Pro}"
command -v sendmidi >/dev/null || { echo "install: brew install gbevin/tools/sendmidi"; exit 1; }

step=0
send() {  # send <description> <sendmidi args...>
  step=$((step + 1))
  local what="$1"; shift
  printf '\n[%d] %s\n    sendmidi dev "%s" %s\n' "$step" "$what" "$dev" "$*"
  sendmidi dev "$dev" "$@"
  read -r -p "    lit anything? note it, then Enter " _
}

echo "Sending to: $dev"

led() { sendmidi dev "$dev" syx hex 00 20 6B 7F 42 02 00 10 "$1" "$2"; }

# map: one SysEx per pad/value, wait for Enter.
# hold: re-send the same SysEx every 50 ms for 3 s, so a flash becomes a steady light
#       if the BSP needs refreshing. Usage: scripts/bsp-led-test.sh "<port>" hold
mode="${2:-}"
if [[ "$mode" == "map" || "$mode" == "hold" ]]; then
  hold_ms=50
  hold_s=3
  for pad in 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F; do
    step=$((step + 1))
    if [[ "$mode" == "hold" ]]; then
      printf '[%d] pad id 0x%s: refreshing for %ss... ' "$step" "$pad" "$hold_s"
      end=$((SECONDS + hold_s))
      while (( SECONDS < end )); do led "$pad" 7F; sleep 0.05; done
      echo
      read -r -p "    which pad stayed lit / flashed? Enter " _
    else
      led "$pad" 7F
      read -r -p "[$step] pad id 0x$pad on: which pad lights? Does it stay lit? Enter " _
    fi
    led "$pad" 00
  done
  for value in 01 02 04 08 10 11 20 40 7F; do
    step=$((step + 1))
    led 70 00
    sleep 0.2
    if [[ "$mode" == "hold" ]]; then
      printf '[%d] pad 0x70 value 0x%s: refreshing for %ss... ' "$step" "$value" "$hold_s"
      end=$((SECONDS + hold_s))
      while (( SECONDS < end )); do led 70 "$value"; sleep 0.05; done
      echo
      read -r -p "    off, dim, bright, colour, or still only a blink? Enter " _
    else
      led 70 "$value"
      read -r -p "[$step] pad id 0x70 value 0x$value: off, dim, bright, or a colour? Enter " _
    fi
  done
  led 70 00
  echo -e "\nDone. Report the pad for each id, and what each value looked like."
  exit 0
fi

for note in 36 44 51; do
  send "note-on $note vel 127, ch 16" ch 16 on "$note" 127
  sendmidi dev "$dev" ch 16 off "$note" 0
done
send "note-on 36 vel 1 (dim?), ch 16" ch 16 on 36 1
sendmidi dev "$dev" ch 16 off 36 0

for ch in 1 2 10; do
  send "note-on 36 vel 127, ch $ch" ch "$ch" on 36 127
  sendmidi dev "$dev" ch "$ch" off 36 0
done

send "CC 36 = 127, ch 16" ch 16 cc 36 127
sendmidi dev "$dev" ch 16 cc 36 0
send "CC 44 = 127, ch 16" ch 16 cc 44 127
sendmidi dev "$dev" ch 16 cc 44 0

read -r -p $'\nTry the SysEx guesses (original BeatStep LED format)? [y/N] ' yn
if [[ "$yn" == [yY] ]]; then
  for pad in 70 71 78; do
    for colour in 01 10 11 7F; do
      send "SysEx pad 0x$pad colour 0x$colour" syx hex 00 20 6B 7F 42 02 00 10 "$pad" "$colour"
    done
    sendmidi dev "$dev" syx hex 00 20 6B 7F 42 02 00 10 "$pad" 00
  done
fi

echo -e "\nDone. Report which step numbers lit a pad, and in which BSP mode."
