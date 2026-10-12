<!-- SPDX-License-Identifier: GPL-3.0-only -->
# MIDI NRPN parameter control

NRPN input provides 90 parameter destinations without using the 16 MIDI LEARN
slots: 82 common parameters and eight controls of the current engine. USB and
TRS input each keep a separate selector for every MIDI channel. Existing CC,
MIDI LEARN, RPN pitch-bend sensitivity and Program Change behavior is preserved.

## Selection and values

Send CC99 for the parameter MSB, CC98 for its LSB, then CC6 for a value from
0 to 127. Either selector order is accepted. Both components are required after
a reset; subsequent messages can reuse the selection or change one component.
The address is `(MSB << 7) | LSB`, unrelated to MIDI Bank Select.

Values use the existing CC parameter scaling and current engine descriptor:
`min + floor((value * (max - min) + 63) / 127)`. This also applies to enums.
For example, Pan values 0/64/127 produce -64/0/63, and Analog Filter Type
0/64/127 selects LP/BP/HP. Time, level and frequency indices are not necessarily
linear milliseconds, dB or Hz. With ENV SYNC, attack/decay/release still receive
0..127, which is mapped onto the 25 musical times.

Example: ANALOG cutoff (engine E5), on channel 1, value 96 (hex bytes):

```text
B0 63 01  # CC99 = 1
B0 62 04  # CC98 = 4
B0 06 60  # CC6  = 96
```

NRPN Null (CC99=127, CC98=127) clears that input/channel's selection. An unknown
address replaces the previous selection and ignores data entry. CC38 is ignored
for NRPN; CC96/97 remain ignored. Values are seven-bit: there is no 14-bit NRPN,
increment/decrement support, additional smoothing, or individual FM6 operator
access.

## Routing and compatibility

MENU > MIDI > MIDI IN determines the destination exactly as it does for notes:
CH1-4 maps channels 1..4 to tracks 1..4; CH5-8, CH9-12 and CH13-16 map their
respective blocks. Other channels are ignored. SEL resolves the selected track
when CC6 arrives, even if the selector was sent before the panel selection changed.

CC101 or CC100 cancels NRPN on both inputs of that channel and retains existing
RPN behavior. Selecting NRPN cancels the shared channel RPN selection. RPN0
still uses CC6 for semitones and CC38 for cents. RPN Null (CC101=127, CC100=127)
also closes NRPN selection. CC121 clears both input selectors for the channel;
ROUT changes, queue overflow and UART input loss clear all selectors.
CC120/123 keep their sound/note-off behavior. Program Change keeps the selector;
E1-E8 always refer to the current engine.

USB bus reset, suspend or detach alone does not clear selectors. Send both
selector components at the beginning of a session and after reconnecting.

Learned CCs keep priority over fixed CCs. NRPN bypasses learning and does not
consume or arm a LEARN slot. If CC and NRPN target the same parameter, the last
message wins. Motion recording follows the existing CC rules: changed values
of the selected track are captured while recording and playing. Data entry does
not write flash; parameters use the existing save/load formats and mechanisms.

## Stable address map

`firmware/src/midi_nrpn_map.h` assigns explicit addresses to C parameter symbols,
independent of enum ordering. Do not reindex published addresses. Reserved:
0:8, 0:61..80 (also with DIGITAL enabled), 0:103..127, 1:8..127, and pages
2..126. 127:127 is Null. Unknown addresses are ignored.

| MSB:LSB | Parameter | C symbol | Stored range |
|---|---|---|---|
| 0:0 | Level | `P_LEVEL` | 0…127 |
| 0:1 | ENV Attack | `P_ATK` | 0…127 |
| 0:2 | ENV Decay | `P_DEC` | 0…127 |
| 0:3 | ENV Sustain | `P_SUS` | 0…127 |
| 0:4 | ENV Release | `P_REL` | 0…127 |
| 0:5 | ENV Filter | `P_ED_FLT` | -64…63 |
| 0:6 | ENV Pitch | `P_ED_PIT` | -64…63 |
| 0:7 | ENV Shape | `P_ED_SHP` | -64…63 |
| 0:9 | LFO Rate | `P_LRATE` | 0…127 |
| 0:10 | LFO Wave | `P_LWAVE` | 0…4 (enum) |
| 0:11 | LFO Phase | `P_LPHASE` | 0…127 |
| 0:12 | LFO Fade | `P_LFADE` | 0…127 |
| 0:13 | LFO Pitch | `P_LD_PIT` | -64…63 |
| 0:14 | LFO Filter | `P_LD_FLT` | -64…63 |
| 0:15 | LFO Shape | `P_LD_SHP` | -64…63 |
| 0:16 | LFO Amplitude | `P_LD_AMP` | 0…127 |
| 0:17 | Arp Mode | `P_AMODE` | 0…14 (enum) |
| 0:18 | Arp Rate | `P_ARATE` | 0…9 (enum) |
| 0:19 | Arp Octaves | `P_AOCT` | 1…4 |
| 0:20 | Arp Gate | `P_AGATE` | 1…127 |
| 0:21 | Arp Swing | `P_ASWING` | 0…100 |
| 0:22 | Arp Probability | `P_APROB` | 0…127 |
| 0:23 | Arp Hold | `P_AHOLD` | 0…1 (enum) |
| 0:24 | Arp Order | `P_AORDER` | 0…1 (enum) |
| 0:25 | Scale Root | `P_ROOT` | 0…11 |
| 0:26 | Scale | `P_SCALE` | 0…15 (enum) |
| 0:27 | Quantize | `P_QUANT` | 0…3 (enum) |
| 0:28 | Transpose | `P_TRANS` | -24…24 |
| 0:29 | Sequence Length | `P_SLEN` | 1…64 |
| 0:30 | Sequence Division | `P_SDIV` | 0…9 (enum) |
| 0:31 | Sequence Swing | `P_SSWING` | 0…100 |
| 0:32 | Sequence Gate | `P_SGATE` | 1…127 |
| 0:33 | Distortion | `P_DIST` | 0…127 |
| 0:34 | Chorus Send | `P_CHOR` | 0…127 |
| 0:35 | Delay Send | `P_DLY` | 0…127 |
| 0:36 | Reverb Send | `P_REV` | 0…127 |
| 0:37 | Voice Mode | `P_VOICE` | 0…3 (enum) |
| 0:38 | Glide | `P_GLIDE` | 0…127 |
| 0:39 | Pan | `P_PAN` | -64…63 |
| 0:40 | Mute | `P_MUTE` | 0…1 (enum) |
| 0:41 | Glide Mode | `P_GLMODE` | 0…1 (enum) |
| 0:42 | Voice Priority | `P_PRIO` | 0…2 (enum) |
| 0:43 | Voice Allocation | `P_ALLOC` | 0…1 (enum) |
| 0:44 | Detune | `P_DETUNE` | 0…127 |
| 0:45 | Slicer Mode | `P_SLCR` | 0…2 (enum) |
| 0:46 | Slicer Pattern | `P_SLPAT` | 1…16 |
| 0:47 | Slicer Rate | `P_SLRATE` | 0…5 (enum) |
| 0:48 | Slicer Depth | `P_SLDEPTH` | 0…127 |
| 0:49 | Matrix1 Source | `P_M1SRC` | 0…10 (enum) |
| 0:50 | Matrix1 Destination | `P_M1DST` | 0…20 (enum) |
| 0:51 | Matrix1 Amount | `P_M1AMT` | −64…63 |
| 0:52 | Matrix2 Source | `P_M2SRC` | 0…10 (enum) |
| 0:53 | Matrix2 Destination | `P_M2DST` | 0…20 (enum) |
| 0:54 | Matrix2 Amount | `P_M2AMT` | −64…63 |
| 0:55 | Matrix3 Source | `P_M3SRC` | 0…10 (enum) |
| 0:56 | Matrix3 Destination | `P_M3DST` | 0…20 (enum) |
| 0:57 | Matrix3 Amount | `P_M3AMT` | −64…63 |
| 0:58 | Matrix4 Source | `P_M4SRC` | 0…10 (enum) |
| 0:59 | Matrix4 Destination | `P_M4DST` | 0…20 (enum) |
| 0:60 | Matrix4 Amount | `P_M4AMT` | −64…63 |
| 0:81 | Chord | `P_CHRD` | 0…9 (enum) |
| 0:82 | Chord Voicing | `P_VOIC` | 0…4 (enum) |
| 0:83 | Drum Kick | `P_LN0` | 0…127 |
| 0:84 | Drum Snare | `P_LN1` | 0…127 |
| 0:85 | Drum Clap | `P_LN2` | 0…127 |
| 0:86 | Drum Closed Hat | `P_LN3` | 0…127 |
| 0:87 | Drum Open Hat | `P_LN4` | 0…127 |
| 0:88 | Drum Tom | `P_LN5` | 0…127 |
| 0:89 | Drum Rim | `P_LN6` | 0…127 |
| 0:90 | Drum Bell | `P_LN7` | 0…127 |
| 0:91 | LFO Sync | `P_LSYNC` | 0…10 (enum) |
| 0:92 | LFO Trigger | `P_LTRIG` | 0…1 (enum) |
| 0:93 | LFO Polarity | `P_LPOL` | 0…1 (enum) |
| 0:94 | Sequence Quantize | `P_SQNT` | 0…1 (enum) |
| 0:95 | Spread | `P_SPRD` | 0…127 |
| 0:96 | Insert Type | `P_ITYPE` | 0…8 (enum) |
| 0:97 | Insert A | `P_IA` | 0…127 |
| 0:98 | Insert B | `P_IB` | 0…127 |
| 0:99 | Insert C | `P_IC` | 0…127 |
| 0:100 | Insert Mix | `P_IMIX` | 0…127 |
| 0:101 | Analog Filter Type | `P_FTYPE` | 0…2 (enum) |
| 0:102 | ENV Sync | `P_ESYNC` | 0…1 (enum) |
| 1:0 | Engine E1 | `P_E0` | Engine-specific; see below |
| 1:1 | Engine E2 | `P_E1` | Engine-specific; see below |
| 1:2 | Engine E3 | `P_E2` | Engine-specific; see below |
| 1:3 | Engine E4 | `P_E3` | Engine-specific; see below |
| 1:4 | Engine E5 | `P_E4` | Engine-specific; see below |
| 1:5 | Engine E6 | `P_E5` | Engine-specific; see below |
| 1:6 | Engine E7 | `P_E6` | Engine-specific; see below |
| 1:7 | Engine E8 | `P_E7` | Engine-specific; see below |

### Engine controls

| Address | ANALOG | Stored range | FM6 | Stored range |
|---|---|---|---|---|
| 1:0 | WAVE | 0..6 | ALG | 0..32 |
| 1:1 | DTN | 0..127 | FB | -7..7 |
| 1:2 | MIX | 0..127 | MLVL | -64..63 |
| 1:3 | NOIS | 0..127 | MRAT | -16..16 |
| 1:4 | CUT | 0..127 | MEG | -64..63 |
| 1:5 | RES | 0..127 | VMOD | -7..7 |
| 1:6 | DRV | 0..127 | DTUN | 0..127 |
| 1:7 | KTR | 0..127 | SLOT | 0..40 |

FM6 ALG0 uses the patch algorithm; SLOT0..7=F1..F8, SLOT8=OWN,
SLOT9..40=B1..B32. MLVL/MRAT/MEG are patch macros. Common ADSR does not affect
FM6 or DRUM. Drum lane levels apply only to DRUM and Analog Filter Type only to
ANALOG. Exposing a parameter does not enable behavior an engine ignores.

## Validation

`tests/run_tests.sh` checks the frozen wire map, all 16 channels, USB/TRS
interleaving, real input parsers, routing, null/partial/unknown selection,
overflow recovery, CC/LEARN/RPN compatibility, motion and mixed MIDI stress.
The MIDI integration also runs under strict ASan/UBSan without exclusions.
Host and WASM results do not establish physical transport timing or chip load;
physical FM-1 testing remains pending.
