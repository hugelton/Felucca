# Compatibility

What Felucca keeps stable between releases, and what can only change in a new major version.

The rule up to 2.0: **an update never changes what you made.** Saved projects, user presets, settings and
the controls keep working and keep sounding the same. Anything that would break this waits for 2.0, and 2.0
will say exactly what it changes.

## Versions

| Part | Form | Meaning |
| --- | --- | --- |
| Firmware version | `vX.Y.Z` (rarely `vX.Y.Z.W` for a hotfix) | `X`: a major version, the only place for breaking changes. `Y`: new features. `Z`: fixes. |
| Package identity | `FM-1_9XY` | The update loader's name for the package (`tools/build.py --release`). It changes with `X.Y` and stays the same for `.Z` and `.Z.W`. |
| Plugin ABI | `FE_ABI` (1) | The plugin interface of Felucca-Engines (`felucca_plugin.h`). It grows within 1 without breaking plugins; 2 is a break. |
| Editor protocol | tagged `INFO` fields | The USB-MIDI SysEx protocol of the web editor ([EDITOR_PROTOCOL.md](../web/EDITOR_PROTOCOL.md)). It grows by additions only. |
| Project format | `FUN1` .. `FUN10` | Every format ever written is still read (see below). |

A release is tagged `vX.Y.Z` on `main`. Its assets are the installable package (`felucca-X.Y.Z.fwsc`), the app
(`felucca-X.Y.Z-app.bin`) and the whole source with the submodules (`felucca-X.Y.Z-source.tar.gz`). The release
notes give their SHA-256 hashes.

## Saved work

- **Projects.** Each format adds to the one before it, and the newest firmware reads every format back to the first
  (`FUN1`). Parameters added later take values that play as before. The format history and the mapping rules are
  at the top of `firmware/src/project.c`.
- **User presets.** A record keeps the parameter count it was stored with, and loads by that count. New
  parameters are only ever added just before the engine's eight, so older records still load. The details are at the
  top of `firmware/src/upreset.c`.
- **Settings** (MENU) keep their meaning. New settings are added with defaults that behave as before.
- **Engine numbers** never move. The engines added since the first release were appended, and a number once given
  is never reused. A plugin that replaces one of Felucca's engines plays as that engine's number, so the sounds
  saved with the engine still play through the plugin.
- **A missing engine or sample** never makes saved work disappear. The track plays a plain sine at the note's
  pitch, and the screen says what is missing (NO PLUGIN, NO SAMPLE). The saved values stay as they were.

An older firmware does not always read what a newer one wrote. When it cannot, the slot shows EMPTY and the data
stays in the flash until that slot is saved over. Whether a given firmware reads a format is listed in `project.c`.

## Plugins

- A plugin built for ABI 1 loads in every Felucca release with ABI 1. The interface grows only by adding fields at
  the end of its structures, and `0` in a new field means what the interface did before
  (Felucca-Engines `src/felucca_plugin.h`, "Stability").
- On the FM-1, plugins live in the flash outside the firmware's area. A firmware update leaves them in place.
- A plugin of its own (one with a new engine number) gets its number once, from the device or the desktop build. The
  number is kept for the plugin's id from then on. Numbers 32 to 94 are given; 95 is never given (it is where a track of
  a plugin the device has never had waits, keeping the plugin's id).
- A project keeps the plugin's id beside the number, so it plays the same plugin on another device. Firmware before 1.9
  does not open a project with a track on a plugin of its own.
- The host checks every plugin before it uses it: its magic, ABI, sample rate, block size, voices, parts, memory
  and functions. On the FM-1 it also checks the blob's header and CRC. If a crash happens inside a plugin, the
  next power-on starts without plugins.

## The controls

A control keeps what it does. A new function goes to a place that was unused: a new MENU row, a held-button layer
or a long press with nothing on it before. 1.9 changes no existing control.

## What waits for 2.0

These are changes that cannot keep the rules above. They are collected for 2.0 and not made before it:

- moving most of Felucca's built-in engines out of the firmware, so that only ANALOG and DRUM are built in, with
  the others as a standard plugin pack installed by default;
- anything that changes the sound of a saved project or preset, removes a setting, or moves a control.
