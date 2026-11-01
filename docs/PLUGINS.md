# Plugin engines

From 1.9, Felucca loads sound engines at run time. Anyone can make an engine and share it, and anyone can play it,
without building or installing a firmware of their own.

- An engine made as a plugin plays like the built-in ones. It is listed on the ENG knob after Felucca's engines,
  with its own name, parameters, presets and HOME knobs.
- A plugin can also take the place of one of Felucca's engines. Sounds saved with that engine then play through
  the plugin.
- A plugin can use any licence (see [Licence](#licence)).

## Using plugins

### On the FM-1

The FM-1 has two plugin slots. They are in the flash outside the firmware, so a firmware update keeps them.

From the command line (`pip3 install mido python-rtmidi`):

```
python3 tools/fm1_plugin.py info                    # the slots, and the numbers given to plugins
python3 tools/fm1_plugin.py install myengine.s0.fep # into slot 0; the FM-1 restarts
python3 tools/fm1_plugin.py erase 0                 # empty slot 0 again
```

A plugin file for the FM-1 is built for one slot: `NAME.s0.fep` goes into slot 0 and `NAME.s1.fep` into slot 1.
Plugins are loaded at power-on.

**If something goes wrong.** If the FM-1 crashes inside a plugin, the next power-on starts without plugins
(`fm1_plugin.py info` says "skipped after a crash"). Erase the slot, or install a fixed version.

### On a desktop (SDL2) and in libretro

- **Desktop:** put the plugin libraries (`.so`, `.dylib` or `.dll`) in a folder and start Felucca with
  `--plugins DIR`. The default folder is `./plugins`.
- **libretro:** use the `Felucca/plugins` folder in the frontend's system folder. In RetroArch this is
  `system/Felucca/plugins`.

A library built for one platform runs only on that platform.

### Plugin engine numbers

A plugin with an engine of its own gets a number the first time it is loaded (32 and up). The number is kept for
the plugin's id, so a sound saved with it finds it again:

- on the FM-1, in a list in the plugin area of the flash;
- on a desktop, in `felucca_plugins.txt` beside the flash file.

A number given once is never given to another plugin.

A project keeps the id of each track's plugin as well as its number. Opened on another FM-1 or desktop, the track plays
the plugin under the number that device has for it. Where the plugin is missing, the track does not disappear: it
plays a plain sine, the screen says NO PLUGIN, and the plugin's id stays in the project when it is saved again. Once
the plugin is installed, the track plays as before.

Two limits:

- A user preset (SAVE > USER) keeps only the engine's number, so a preset of a plugin of its own is for the device it
  was saved on.
- Firmware before 1.9 cannot open a project that has a track on a plugin of its own (the slot shows EMPTY there). A
  track on a plugin that replaces one of Felucca's engines is a track of that engine everywhere.

## Making a plugin

Engines are written in C99 with fixed-point arithmetic, against
[Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (MIT). Its `docs/PLUGINS.md` is the guide:

1. Write the engine as an `engine_t` (`src/felucca_engine.h`): its parameters, presets and the functions per voice
   and per block. The six engines in `src/` are working examples.
2. Wrap it in a plugin file of three lines (`plugins/*.c`): the includes and `FE_PLUGIN(...)` with your engine's id.
3. Build it:
   - `tools/build_plugin.sh` for a desktop library;
   - `tools/build_plugin_fm1.sh` for the FM-1 (it needs the JieLi toolchain, which runs in Docker).
4. Test it: `tests/plugin_test.c` plays a plugin through the same phrases as the built-in engines.

Pick an id with a lower-case letter in it, and list it in Felucca-Engines' `docs/PLUGIN_IDS.md` so that no two engines
share one. Ids of upper-case letters and digits only are Felucca's own.

## Licence

Felucca is GPL-3.0-only. Its copyright holder gives an extra permission for plugins: a plugin engine that talks to
Felucca only through the plugin interface may be under any licence, open or not, and may be shared with Felucca or on
its own. The exact text is in [LICENSING.md](../LICENSING.md#plugin-engines). The plugin interface itself
(`felucca_plugin.h`, `felucca_plugin_fm1.h`, `felucca_engine.h`) is MIT.

Felucca's own repository only holds GPL code. Plugins made by others live in their own repositories, under their own
licences.

## For developers: how it works

- [ARCHITECTURE.md](ARCHITECTURE.md#engines-and-plugins) describes the host side.
- The ABI, its checks and its stability rules are in Felucca-Engines' `src/felucca_plugin.h`.
- The FM-1's blob format is in `src/felucca_plugin_fm1.h` in Felucca-Engines.
- The USB commands that list and install plugins (82..87) are in [EDITOR_PROTOCOL.md](../web/EDITOR_PROTOCOL.md).
