<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Browser smoke test

The optional Chromium gate runs this checkout's emulator page and real
WebAssembly/AudioWorklet output. It complements the existing Node/Wasm and
native comparisons; it does not replace them or assess sound quality, physical
FM-1 behavior, USB/MIDI transport or device deadlines.

Install the [build prerequisites](../BUILDING.md), Node 22 or newer and Emscripten,
then run from the repository root with the Emscripten environment active:

```sh
npm ci --prefix web/emu --ignore-scripts --no-audit --no-fund
npm exec --prefix web/emu -- playwright install --with-deps --only-shell chromium
./build.sh
sh web/emu/build.sh
npm test --prefix web/emu
```

`./build.sh` generates the headers and assets needed by the emulator; a fresh
emulator build is required after source changes. The gate hashes the served
page, worklet and Wasm in its report, but does not detect stale builds.
`--with-deps` needs system package installation permissions. If Chromium and its
system dependencies are already installed, skip its download and run
`PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH=/usr/bin/chromium npm test --prefix web/emu`.
The lockfile pins Playwright and its managed browser revision; using the system
browser overrides that browser revision, which is recorded in the summary.

The runner serves only `build/emu/` on an ephemeral loopback port, opens a fresh
browser context and closes its browser and server on completion or failure.
It checks boot, a running 44.1 kHz AudioContext, a nonblank LCD, keyboard note
stereo PCM and quiet release, ENV navigation and encoder redraw, a playable
portrait layout, and panel save/reload. The persistence check loads USER slot 1
through the panel and compares the restored envelope graph with the edited one.
It also rejects browser, network, console and worklet errors. A test-only probe
observes worklet messages and taps real audio through a separate AudioWorklet;
it is never included in the shipped emulator.

The command prints `browser: PASS (7 checks); build/browser/run-*/summary.json`
and exits zero, or prints FAIL and exits nonzero. Read the summary first: it has
named checks, observations, errors, browser versions and hashes of served files.
On failure, available LCD snapshots, a page screenshot and up to eight seconds
of stereo PCM are saved beside it. Early setup failures may have no media.
To retain media on success too:

```sh
npm test --prefix web/emu -- --artifacts=always
```

The WAV is a clipped PCM16 diagnostic export; checks use original float PCM.
Scheduling uses bounded waits and signal thresholds rather than exact hashes
of audio or timing claims. All evidence stays under ignored `build/browser/`;
`web/emu/node_modules/` is ignored too. The runner makes no model calls, has no
automatic retries and does not update baselines, publish a page or touch a device.
