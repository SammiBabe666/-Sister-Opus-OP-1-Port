# Device checklist (OP-1 + Opus)

For every plugin before a release, and again on each new Opus (Beta 4 or later). Steps marked **[OP-1]** need the
hardware; nothing here is marked done until it has been seen on an OP-1. Written for Dark Sister, and the same for
Bloodmoon, Erdtree, Hollow Saint, The Bronze Choir, ROOMS, Reverb and Stutter.

## 0. Before you start (Mac)

- [ ] `. ~/mnt/Documents/op1-toolchain/env.sh` and build: `./build.sh` (here) or the project's own build script
- [ ] `plugin_tool.py show` on the `.opmod`: id, version, `core`, `adds`, `uses` as expected
- [ ] Host tests pass (here: `tests/opus_host_test.c`, 9 checks)
- [ ] New Opus? First `python3 tools/opus_kit_diff.py OLD_KIT NEW_KIT plugin/sammi.sister` and fix only what it flags
- [ ] OP-1: back up `tape`, `synth` and `drum`; note the Opus build (hold **help** on Opus's menu)

## 1. Install **[OP-1]**

- [ ] Developer mode on: Opus menu (hold synth + drum + tape + mixer), **T1**, developer mode
- [ ] Disk mode (shift + COM, **T3**): copy the `.opmod` into `mods/` (remove older copies of the same plugin id), eject, restart
- [ ] Opus menu, **T2** (mods list): the row says loaded, not refused

## 2. Function **[OP-1]**

- [ ] Engine: all six voices, chords, presets load; effect: on the synth, on the drum and on the master
- [ ] Every encoder, and every shift layer and push in the manifest's `uses` lines, does what the README says
      (Dark Sister: blue space, green voice, white swell, orange mix; shift + green key; green push semis/cents;
      shift + orange dark)
- [ ] Save a sound using it, restart, reload: the settings come back
- [ ] Screen: draws, follows the colour theme; effects: turning the FX off greys the page and on restores it
- [ ] 10 minutes of normal playing: no clicks, dropouts, freezes, or a crash log (hold **help** for it)

## 3. CPU **[OP-1]**

- [ ] Play the heaviest case: all voices or a busy part, its heaviest settings (Dark Sister: SPACE 99, VOICE 50,
      SWELL 99, MIX 99, DARK 99, on the master), with the other plugins you expect beside it
- [ ] Mods list, the plugin's row, **orange** push: read **"audio: worst N"**
- [ ] Write it into the plugin's `cpu.txt`, replacing the estimate, and rebuild:

  ```
  cycles N
  measured YYYY-MM-DD on my OP-1: <what you played>
  ```

## 4. Load-together (memory) **[OP-1]**

All four effects and one engine at once, to see the memory pool hold:

- [ ] `mods/` holds Dark Sister, ROOMS, Reverb, Stutter and one engine (start with the heaviest: Hollow Saint)
- [ ] Restart: every row in **T2** says loaded (a refusal for memory names the plugin; write it down)
- [ ] Engine on the synth with ROOMS; Dark Sister on the drum; Reverb on the master; then swap Stutter in for ROOMS
- [ ] Play 5 minutes switching sounds and effects: no refusals, no "switched off", no crash log
- [ ] Repeat with each other engine (Bloodmoon, Erdtree, The Bronze Choir)

## 5. Sign off

- [ ] Add to the plugin's `manifest.txt` (date of the run; up to 60 characters of what was run):

  ```
  checked   device YYYY-MM-DD all checklist steps, Opus beta X.Y
  ```

- [ ] Rebuild, `show` lists the device line, commit with the results below

## Results

| Plugin | Version | Opus build | Date | Install | Function | audio: worst | Load-together | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Dark Sister | 1.2.0 | 2.0 beta 4 RC1 | 2026-11-06 | yes | works (owner's report) | not measured | not run | `checked device` added |
| ROOMS | | | | | | | | |
| Reverb | | | | | | | | |
| Stutter | | | | | | | | |
| Bloodmoon | | | | | | | | |
| Erdtree | | | | | | | | |
| Hollow Saint | | | | | | | | |
| The Bronze Choir | | | | | | | | |
