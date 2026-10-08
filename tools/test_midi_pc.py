#!/usr/bin/env python3
"""
Test MIDI Program Change and notes on FM-1 / Felucca over USB-MIDI.
Requires: pip install mido python-rtmidi
"""

import time
import sys

try:
    import mido
except ImportError:
    print("Error: 'mido' not found. Install it with: pip install mido python-rtmidi")
    sys.exit(1)


def find_fm1_port():
    names = mido.get_output_names()
    print("Available MIDI output ports:")
    for n in names:
        print(f"  - {n}")

    # Search for common FM-1 / Felucca device port names
    for n in names:
        if any(keyword in n.lower() for keyword in ["fm-1", "felucca", "sloop"]):
            return n

    # Fallback if only 1 device is connected or manual selection
    if names:
        print(f"\nNo port named 'FM-1' or 'Felucca' found, selecting first available: {names[0]}")
        return names[0]
    return None


def play_note(outport, ch, note, duration=0.4, vel=100):
    outport.send(mido.Message("note_on", channel=ch, note=note, velocity=vel))
    time.sleep(duration)
    outport.send(mido.Message("note_off", channel=ch, note=note, velocity=0))
    time.sleep(0.1)


def main():
    port_name = find_fm1_port()
    if not port_name:
        print("No MIDI output ports found! Please make sure the FM-1 is plugged in via USB.")
        sys.exit(1)

    print(f"\nOpening MIDI output port: {port_name}")
    with mido.open_output(port_name) as out:
        print("\n" + "=" * 60)
        print("Test 1: Track 1 (MIDI Ch 1) - Synth Presets")
        print("=" * 60)
        for prog in [0, 1, 2, 3]:
            print(f"-> Sending Program Change: {prog} on Channel 1 (Track 1)")
            out.send(mido.Message("program_change", channel=0, program=prog))
            time.sleep(0.15)  # brief pause to let preset load and LCD redraw
            print("   Playing C4 chord arpeggio...")
            play_note(out, ch=0, note=60, duration=0.25)
            play_note(out, ch=0, note=64, duration=0.25)
            play_note(out, ch=0, note=67, duration=0.4)
            time.sleep(0.3)

        print("\n" + "=" * 60)
        print("Test 2: Track 2 (MIDI Ch 2) - Presets / FM6 Patches")
        print("=" * 60)
        for prog in [0, 1, 2]:
            print(f"-> Sending Program Change: {prog} on Channel 2 (Track 2)")
            out.send(mido.Message("program_change", channel=1, program=prog))
            time.sleep(0.15)
            print("   Playing F4 note...")
            play_note(out, ch=1, note=65, duration=0.5)
            time.sleep(0.3)

        print("\n" + "=" * 60)
        print("Test 3: Track 4 (MIDI Ch 4) - Drum Kits (General MIDI notes)")
        print("=" * 60)
        # In ROUT CH1-4, Track 4 responds on MIDI Channel 4 (mido channel=3)
        for kit in [0, 1, 2]:
            print(f"-> Sending Program Change: {kit} on Channel 4 (Drum Kit {kit})")
            out.send(mido.Message("program_change", channel=3, program=kit))
            time.sleep(0.15)
            print("   Playing Kick (36), Snare (38), Hi-Hat (42)...")
            play_note(out, ch=3, note=36, duration=0.2)  # Kick
            play_note(out, ch=3, note=38, duration=0.2)  # Snare
            play_note(out, ch=3, note=42, duration=0.2)  # Closed Hat
            time.sleep(0.3)

        print("\n" + "=" * 60)
        print("MIDI Program Change and note tests finished!")
        print("=" * 60)


if __name__ == "__main__":
    main()
