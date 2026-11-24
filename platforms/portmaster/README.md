# Felucca for PortMaster (research)

The SDL2 frontend ([platforms/sdl](../sdl/README.md)) built for aarch64 Linux handhelds and laid out as a PortMaster
port. It has not been run on a handheld.

## Build

Docker, and the generated headers (`platforms/gen.sh`, Python 3 with Pillow and fontTools):

```
platforms/gen.sh
platforms/portmaster/build.sh     # -> build/portmaster/ports/Felucca.sh and build/portmaster/ports/felucca/
```

Copy both into the device's `ports` folder. The binary is built on Debian bookworm (glibc 2.36; an older system needs
an older image, `PM_IMAGE=...`) and uses the device's own SDL2.

## Running

`Felucca.sh` runs Felucca full screen at the display's size, with the flash kept in `felucca/felucca_flash.bin`.
Felucca reads the gamepad itself (the map is in [platforms/sdl](../sdl/README.md#playing)); gptokeyb runs only for
PortMaster's exit hotkey. No MIDI in.
