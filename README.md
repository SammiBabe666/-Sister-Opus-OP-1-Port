# Dark Sister (darksister.opmod) — Hundekraft's SISTER as a native Opus 2 plugin for the original OP-1, with DARK

SISTER (Hundekraft, OP-1 Workshop 3.1.1) is a stereo cathedral reverb whose tail sings (CHOIR: drifting vowel formants),
rings in tune (PIPES: root/fifth/octave combs) or swells backwards (VESPER). This repo ports it to a standalone Opus 2
`.opmod` (`sammi.sister`), using Opus's own effect service — no Workshop runtime, no bridge.

**Demo:** `docs/demo/darksister-demo.mp4` (rendered on a computer from the plugin's own code and DSP, not an OP-1 recording).

**Download:** `dist/darksister.opmod` (or the release ZIP in `dist/`).

## Install
1. OP-1 with Opus 2 (core 2.0 or later). Developer mode on for the first tries (Opus menu: synth+drum+tape+mixer, T1).
2. Disk mode (shift + COM, T3), copy `darksister.opmod` into the disk's `mods` folder, eject, restart.
3. Opus menu → T2 (mods list): the `dark sister` row should say "loaded".
4. Pick **dark sister** (shown as DARK SISTER) in the synth, drum or master effect list like any effect.

## Controls (as SISTER)
| Encoder | Macro | 0 → 99 |
|---|---|---|
| Blue | SPACE | chapel (RT60 1.8 s) → cathedral (13 s) |
| Green | VOICE | CHOIR → blend → PIPES |
| White | SWELL | forward tail → fully reversed (VESPER) |
| Orange | MIX | exactly dry → wet only |

- **SWELL on the page (this port's):** the label shows its value (`swell 0`..`swell 99`), the candle flames grow to
  three times their height at 99, and a glow ring round each flame widens and brightens with it.
- **Shift + orange: DARK — exclusive to this Opus version.** Darkens the reverb: lowers the tank's damping corner and the
  pipes' brightness (up to 70 %) and closes a low-pass on the wet down to ~430 Hz. At 0 it is SISTER exactly as
  Hundekraft made it (byte-identical output). On the page night falls: the nave dims, the flames burn low and amber, a
  crescent moon rises and dust motes drift down the nave; `dark NN` shows under the mix bar.
- **Shift + green**: PIPES key, one semitone per detent (±24 around C3; lowest C#1 as in the original).
- **Green push** on SISTER's page: toggles shift + green between semitones and cents (the original used a press page).
- Shift + blue/white: the OS's fine steps. The key shows on the green label while shift is held.
- Screen: SISTER's own nave/rose/nun/candles page, ported line for line onto Opus drawing calls.
- **Effect off:** the art, nave, petals/pipes, candle flames and mix bar turn grey (each at its own brightness, 80 %),
  and the tail-driven motion (petals singing, flame flicker) settles at the still sketch; labels keep their colours and
  the OS's own "off" overlay still dims the frame as on every effect page. On again: colours and motion return.

### How on/off is detected
Opus has no call that reports an effect's on/off state, and no documented firmware flag exists for the synth and drum
effects (only the master's). The OS runs an effect's process call only while it is on, so each instance counts its
process calls (`sister_inst.blocks`, written by the audio callback), and the page reads the count each frame: no new
block for 4 frames (~0.13 s; ~23 blocks arrive per frame when on) means off. It is the effect's real run state, not
volume, animation or parameters. It also greys when Opus's CPU budget switches the plugin off (it is not processing
then either). **Assumption, not verified on hardware:** that the OS stops calling a switched-off effect's process.

## What was reused / changed
- **Unchanged from SISTER:** `sister_dsp.c/h`, `sister_kern.c/h`, `sister_asm.S` (Blackfin kernels), `sister_tables.h`,
  `sister_geom.h`, `sister.svg`. Unmodified originals and SISTER's tools/tests are in `upstream/sister/`.
- **New (the port):** `sister_opus.c` (Opus effect glue replacing kennel's `sister.c`: `need/init/mono/stereo/view/
  shift_knob`, state in the instance's bulk memory, the L1 pool in Opus fast memory), `sister_page.c` (port of
  `sister_screen.c` drawn under the effect page), `manifest.txt`, `cpu.txt`.
- Synth and drum share one SISTER state (Opus gives their instances one bulk; R246 runs one at a time). Master is separate.

## Build
Uses the shared OP-1 Blackfin toolchain (GCC 13.3.0 + binutils 2.38, the Opus kit's specification); this project
contains no compiler or compiler build scripts.

```
. ~/mnt/Documents/op1-toolchain/env.sh      # sets OP1_BFIN_TOOLCHAIN (or point it at any bfin-elf GCC 13.3.0 toolchain)
./build.sh [OPUS_KIT_REPO]                  # the Opus repo (plugin-kit/ inside) at d7f28b0; cloned if not given
```

Target: Opus 2.0 beta 3.2 (plugin kit `d7f28b0`, API up to core 2.13). The manifest's `core 2.0` is deliberate: it is
the lowest core whose services Sister calls, so every Opus 2 beta loads it.

## Test results — what was and was not done
| Level | Result |
|---|---|
| Compilation | PASS on the shared toolchain (GCC 13.3.0 + binutils 2.38, the kit's spec): `plugin_tool.py build` with `-Wall -Wextra -Werror`; `show` reads it back (17 imports, 48,416-byte image). Reproducible: two builds give SHA-256 `a1043dc0…6a0101`. SVG passes the kit's `svg` check. |
| SISTER's own host tests (`sister_test.c`, ASan/UBSan) | PASS |
| SISTER's `check.py` audio checks on the shipped DSP (clicks, pipe tuning, VESPER, levels, stereo) | PASS, 63/63 |
| Opus glue host test (`tests/opus_host_test.c`, mock core, ASan/UBSan) | PASS: glue output equals SISTER's `si_process` sample-for-sample over 6000 mono + 6000 master blocks with poisoned fast memory; MIX 0 exactly dry; init; key word law; page draws only on its own page, all strokes in bounds |
| DARK (host test 8; render comparison) | PASS: at DARK 0 the DSP output is byte-identical to Hundekraft's original (3 settings, `sister_render`); at DARK 99 the wet's high-band share drops ~10x and tails still end in exact silence; shift + orange sets it |
| Page preview (`tests/page_preview.*`, browser render of the recorded strokes) | Matches the Workshop manual's native capture; on and off (`*-OFF.png`) states in `docs/preview/` |
| Off/on (host test 7, mock core) | PASS: with no process calls every FX stroke and the art are grey and the tail motion stops; colours return within a frame of blocks resuming; 6 toggles, knob turned while off, no stale state; art re-kept once per colour |
| Blackfin simulator (GNU `bfin-elf-run`) | `si_k_mix` asm = C reference once the simulator handles two half-register loads into one register in a parallel instruction (GDB's Blackfin simulator, including gdb-14.2, keeps only the second, so on it the master's left channel differs from the C reference; a simulator limitation, not Sister's). The full 9-regime asm/C/host comparison (`tests/bfin_sim/run_sim.sh`) has not been completed |
| Opus emulator | **Not performed** (not publicly available) |
| Physical OP-1 | **Not performed.** No `checked` line in the manifest. |

## Known limitations
- `cpu.txt` (96,000 cycles) is an **estimate** from Hundekraft's unit model, not a measurement. Measure on a device
  (mods list → orange push → "audio: worst N"), put it in `cpu.txt`, rebuild.
- Memory: ~173 KB bulk per SISTER state (one for synth/drum, one for master) + 5.4 KB / 1.9 KB fast memory + 48 KB for the page cache.
- Switching between synth and drum on SISTER shares the tail; an init (sound/slot change) restarts it, as in R246.
- The page relies on Opus calling `view` each frame of the effect page and on `opus_on_frame_under`; if a core draws its
  effect page differently, the sound is unaffected.
- Sounds saved with Workshop SISTER (kennel's filter-slot mark) do not load as this plugin; it's a new Opus effect type.
- Open questions inherited from SISTER (FX sample rate on the unit, level balance by ear) still apply.

## Licence
GPL-3.0-or-later with the OP-1 firmware additional permission (see `NOTICE`, `LICENSE`). SISTER © 2026 Hundekraft;
Opus plugin kit © LizardMan (GPL-3.0); port © 2026 SammiBabe666. Not affiliated with Teenage Engineering.
