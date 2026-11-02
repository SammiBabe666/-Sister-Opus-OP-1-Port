# sammi.sister — Hundekraft's SISTER as a native Opus 2 plugin for the original OP-1

SISTER (Hundekraft, OP-1 Workshop 3.1.1) is a stereo cathedral reverb whose tail sings (CHOIR: drifting vowel formants),
rings in tune (PIPES: root/fifth/octave combs) or swells backwards (VESPER). This repo ports it to a standalone Opus 2
`.opmod` (`sammi.sister`), using Opus's own effect service — no Workshop runtime, no bridge.

**Download:** `dist/sammi.sister.opmod` (or the release ZIP in `dist/`).

## Install
1. OP-1 with Opus 2 (core 2.0 or later). Developer mode on for the first tries (Opus menu: synth+drum+tape+mixer, T1).
2. Disk mode (shift + COM, T3), copy `sammi.sister.opmod` into the disk's `mods` folder, eject, restart.
3. Opus menu → T2 (mods list): the `sister` row should say "loaded".
4. Pick **sister** in the synth, drum or master effect list like any effect.

## Controls (as SISTER)
| Encoder | Macro | 0 → 99 |
|---|---|---|
| Blue | SPACE | chapel (RT60 1.8 s) → cathedral (13 s) |
| Green | VOICE | CHOIR → blend → PIPES |
| White | SWELL | forward tail → fully reversed (VESPER) |
| Orange | MIX | exactly dry → wet only |

- **Shift + green**: PIPES key, one semitone per detent (±24 around C3; lowest C#1 as in the original).
- **Green push** on SISTER's page: toggles shift + green between semitones and cents (the original used a press page).
- Shift + blue/white/orange: the OS's fine steps. The key shows on the green label while shift is held.
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
`OP1_BFIN_TOOLCHAIN=/path/to/bfin ./build.sh` (see `toolchain/README.md` for how the toolchain here was built).

## Test results — what was and was not done
| Level | Result |
|---|---|
| Compilation | PASS: Opus kit `plugin_tool.py build` with `-Wall -Wextra -Werror`, GCC 13.3.0; `show` reads it back (15 imports, 47,444-byte image). SVG passes the kit's `svg` check. |
| SISTER's own host tests (`sister_test.c`, ASan/UBSan) | PASS |
| SISTER's `check.py` audio checks on the shipped DSP (clicks, pipe tuning, VESPER, levels, stereo) | PASS, 63/63 |
| Opus glue host test (`tests/opus_host_test.c`, mock core, ASan/UBSan) | PASS: glue output equals SISTER's `si_process` sample-for-sample over 6000 mono + 6000 master blocks with poisoned fast memory; MIX 0 exactly dry; init; key word law; page draws only on its own page, all strokes in bounds |
| Page preview (`tests/page_preview.*`, browser render of the recorded strokes) | Matches the Workshop manual's native capture; on and off (`*-OFF.png`) states in `docs/preview/` |
| Off/on (host test 7, mock core) | PASS: with no process calls every FX stroke and the art are grey and the tail motion stops; colours return within a frame of blocks resuming; 6 toggles, knob turned while off, no stale state; art re-kept once per colour |
| Blackfin simulator (GNU `bfin-elf-run`) | `si_k_mix` asm = C reference after fixing a simulator bug; the full 9-regime asm/C/host hash comparison was started but **not completed** before this release |
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
