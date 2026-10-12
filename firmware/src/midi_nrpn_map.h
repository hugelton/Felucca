/* SPDX-License-Identifier: GPL-3.0-only
 * Stable NRPN v1 wire addresses. Never reindex or reuse retired addresses.
 * Names describe the public map; they are not stored in firmware. */
#ifndef FELUCCA_MIDI_NRPN_MAP_H
#define FELUCCA_MIDI_NRPN_MAP_H

#define MIDI_NRPN_COMMON(X) \
    X(0, P_LEVEL, "Level") \
    X(1, P_ATK, "ENV Attack") \
    X(2, P_DEC, "ENV Decay") \
    X(3, P_SUS, "ENV Sustain") \
    X(4, P_REL, "ENV Release") \
    X(5, P_ED_FLT, "ENV Filter") \
    X(6, P_ED_PIT, "ENV Pitch") \
    X(7, P_ED_SHP, "ENV Shape") \
    X(9, P_LRATE, "LFO Rate") \
    X(10, P_LWAVE, "LFO Wave") \
    X(11, P_LPHASE, "LFO Phase") \
    X(12, P_LFADE, "LFO Fade") \
    X(13, P_LD_PIT, "LFO Pitch") \
    X(14, P_LD_FLT, "LFO Filter") \
    X(15, P_LD_SHP, "LFO Shape") \
    X(16, P_LD_AMP, "LFO Amplitude") \
    X(17, P_AMODE, "Arp Mode") \
    X(18, P_ARATE, "Arp Rate") \
    X(19, P_AOCT, "Arp Octaves") \
    X(20, P_AGATE, "Arp Gate") \
    X(21, P_ASWING, "Arp Swing") \
    X(22, P_APROB, "Arp Probability") \
    X(23, P_AHOLD, "Arp Hold") \
    X(24, P_AORDER, "Arp Order") \
    X(25, P_ROOT, "Scale Root") \
    X(26, P_SCALE, "Scale") \
    X(27, P_QUANT, "Quantize") \
    X(28, P_TRANS, "Transpose") \
    X(29, P_SLEN, "Sequence Length") \
    X(30, P_SDIV, "Sequence Division") \
    X(31, P_SSWING, "Sequence Swing") \
    X(32, P_SGATE, "Sequence Gate") \
    X(33, P_DIST, "Distortion") \
    X(34, P_CHOR, "Chorus Send") \
    X(35, P_DLY, "Delay Send") \
    X(36, P_REV, "Reverb Send") \
    X(37, P_VOICE, "Voice Mode") \
    X(38, P_GLIDE, "Glide") \
    X(39, P_PAN, "Pan") \
    X(40, P_MUTE, "Mute") \
    X(41, P_GLMODE, "Glide Mode") \
    X(42, P_PRIO, "Voice Priority") \
    X(43, P_ALLOC, "Voice Allocation") \
    X(44, P_DETUNE, "Detune") \
    X(45, P_SLCR, "Slicer Mode") \
    X(46, P_SLPAT, "Slicer Pattern") \
    X(47, P_SLRATE, "Slicer Rate") \
    X(48, P_SLDEPTH, "Slicer Depth") \
    X(49, P_M1SRC, "Matrix1 Source") \
    X(50, P_M1DST, "Matrix1 Destination") \
    X(51, P_M1AMT, "Matrix1 Amount") \
    X(52, P_M2SRC, "Matrix2 Source") \
    X(53, P_M2DST, "Matrix2 Destination") \
    X(54, P_M2AMT, "Matrix2 Amount") \
    X(55, P_M3SRC, "Matrix3 Source") \
    X(56, P_M3DST, "Matrix3 Destination") \
    X(57, P_M3AMT, "Matrix3 Amount") \
    X(58, P_M4SRC, "Matrix4 Source") \
    X(59, P_M4DST, "Matrix4 Destination") \
    X(60, P_M4AMT, "Matrix4 Amount") \
    X(81, P_CHRD, "Chord") \
    X(82, P_VOIC, "Chord Voicing") \
    X(83, P_LN0, "Drum Kick") \
    X(84, P_LN1, "Drum Snare") \
    X(85, P_LN2, "Drum Clap") \
    X(86, P_LN3, "Drum Closed Hat") \
    X(87, P_LN4, "Drum Open Hat") \
    X(88, P_LN5, "Drum Tom") \
    X(89, P_LN6, "Drum Rim") \
    X(90, P_LN7, "Drum Bell") \
    X(91, P_LSYNC, "LFO Sync") \
    X(92, P_LTRIG, "LFO Trigger") \
    X(93, P_LPOL, "LFO Polarity") \
    X(94, P_SQNT, "Sequence Quantize") \
    X(95, P_SPRD, "Spread") \
    X(96, P_ITYPE, "Insert Type") \
    X(97, P_IA, "Insert A") \
    X(98, P_IB, "Insert B") \
    X(99, P_IC, "Insert C") \
    X(100, P_IMIX, "Insert Mix") \
    X(101, P_FTYPE, "Analog Filter Type") \
    X(102, P_ESYNC, "ENV Sync")

#define MIDI_NRPN_ENGINE(X) \
    X(0, P_E0, "Engine E1") \
    X(1, P_E1, "Engine E2") \
    X(2, P_E2, "Engine E3") \
    X(3, P_E3, "Engine E4") \
    X(4, P_E4, "Engine E5") \
    X(5, P_E5, "Engine E6") \
    X(6, P_E6, "Engine E7") \
    X(7, P_E7, "Engine E8")

#endif
