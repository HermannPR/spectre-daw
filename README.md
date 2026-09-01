# SpectreDAW v0.3

<p><img src="https://img.shields.io/badge/C-A8B9CC?style=flat-square&logo=c&logoColor=white" height="20" alt="C"> <img src="https://img.shields.io/badge/3DS-0A0A0A?style=flat-square" height="20" alt="3DS"></p>

Nintendo 3DS homebrew synthesizer + piano roll. 3 drum tracks × 16 steps + 1 synth track with real-time subtractive synthesis and 3-note chord piano roll.

## Prerequisites

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with `3ds-dev` package
- Environment variables set:
  - `DEVKITPRO` — e.g. `C:\devkitpro` or `/opt/devkitpro`
  - `DEVKITARM` — e.g. `%DEVKITPRO%\devkitARM`

## Build

```sh
make
```

Produces `SpectreDAW.3dsx` and `SpectreDAW.elf`.

To clean: `make clean`

## Run

**Citra emulator (recommended for development):**
- File → Load File → `SpectreDAW.3dsx`

**Hardware (Luma3DS + Homebrew Launcher):**
1. Copy `SpectreDAW.3dsx` to `/3ds/SpectreDAW/SpectreDAW.3dsx` on SD card.
2. Launch via Homebrew Launcher.

## Saved data

Patches and patterns are saved to:
```
sdmc:/3ds/SpectreDAW/patches.bin
sdmc:/3ds/SpectreDAW/pattern.bin
```
The directory is created automatically on first save (START key).

## Controls

### Sequencer view (default)

| Input            | Action                              |
|------------------|-------------------------------------|
| Touch drum cell  | Toggle step on/off                  |
| Touch synth cell | Open note picker (add pitched notes)|
| A                | Play / Stop                         |
| Y                | Enter Synth Edit                    |
| L / R            | Previous / Next patch slot (1–4)    |
| D-Pad Up/Down    | BPM + 5 / BPM - 5                   |
| START            | Save to SD card + Exit              |

### Note picker view

| Input    | Action                              |
|----------|-------------------------------------|
| Touch key| Add / remove note from chord (max 3)|
| CLR      | Clear chord for this step           |
| DONE / B | Confirm and return to Sequencer     |

### Synth edit view

| Input    | Action                              |
|----------|-------------------------------------|
| B        | Return to Sequencer                 |
| L / R    | Previous / Next tab (OSC / ENV / FLT)|
| Touch    | Tap osc buttons, drag sliders       |
| A        | Play / Stop                         |
| D-Pad    | BPM adjust                          |

## Synth engine

Each of 4 patch slots has:

| Parameter    | Range          | Description                               |
|--------------|----------------|-------------------------------------------|
| OSC1 type    | SIN/SAW/SQR/TRI/NOI | Oscillator waveform                  |
| OSC2 type    | SIN/SAW/SQR/TRI/NOI | Second oscillator                    |
| Detune       | -50..+50 cents | OSC2 pitch offset                        |
| Mix          | OSC1 → OSC2    | Crossfade between oscillators            |
| A / D / S / R| 0–100%         | ADSR envelope                            |
| Cutoff       | 200–14000 Hz   | Low-pass filter cutoff                   |
| Resonance    | 0–97%          | Filter resonance (SVF)                   |
| LFO Rate     | 0.1–10 Hz      | LFO speed (modulates cutoff)             |
| LFO Depth    | 0–6000 Hz      | LFO modulation amount                    |

## Tracks

| Row    | Sound         |
|--------|---------------|
| 0      | Kick (sine sweep) |
| 1      | Snare (noise + body) |
| 2      | Hi-Hat (detuned squares) |
| 3      | Synth (piano roll, real-time synthesis) |
