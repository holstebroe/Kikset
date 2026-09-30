# Kikset — Project Plan

*Status as of 30 Sep 2026 (updated with GUI and kick retune) · companion to [`Kickset_design.md`](../Kickset_design.md) (v0.1)*

## Summary

The audio engine, CLAP plugin shell, render CLI and test suite are built. Milestones 1 to 5
of the design are implemented (the GUI only on Linux/X11). Presets, Windows/macOS windows, cross-platform CI verification and
listening tests are still open. The plugin loads through the CLAP entry point, reports 48 parameters,
round-trips state, and renders a K‑B‑B‑B groove from host transport or from MIDI.

| Milestone (design §13) | Status |
|---|---|
| 1 Skeleton (CMake, CLAP glue, Params, BeatClock, KBBB plays) | **Done** |
| 2 Solver (analytic phase, Reset/Follow, anchor tests, render CLI) | **Done** |
| 3 Voices (filters, envelopes, saturation + oversampler, bass drive, sub, clicks) | **Done**, see deviations |
| 4 Flow (auto gain, emphasis, roll, push, meters) | **Done** (audio side). The meters need the GUI |
| 5 GUI (panel, step buttons, wide scope, handoff zoom) | **Done on Linux/X11**; Win32 and Cocoa windows remain |
| 6 Polish (presets, CI, docs, listening tests) | **Partly**: CI workflow and README written, not yet run |
| 7 v1.1 | Not started |

## Completed

### Framework
- [x] CMake project (C++20, CMake ≥ 3.20, IPO in Release), CLAP headers as the only dependency (git submodule `clap/`)
- [x] `cmake/EmbedResources.cmake` (pure-CMake WAV → `ClickData.cpp`), `cmake/DeployClap.cmake` (`$CLAPTEST`)
- [x] Layering as designed: `src/core` has no CLAP or GUI includes; `src/clap` is thin glue
- [x] `Params.hpp` is the single parameter table (48 params: id, name, module, range, default, stepped/log, unit). CLAP glue, state and tools read from it
- [x] GitHub Actions matrix (Linux / Windows / macOS) that builds, runs ctest, uploads `Kikset.clap`

### Engine (`src/core`)
- [x] **KickPhase**: closed-form kick phase, never accumulated. A cumulative-Simpson table in `v = √u` (fine near the click) with cubic Hermite interpolation
- [x] **Anchor solver**: minimum-weighted-norm Newton step on four adjusters (trigger nudge, sweep depth, curve, start phase), with numeric Jacobian, active-set clamping, ±N cycle candidates and **Lock Style** weights. Results cached by input. Converges in ≤ 5 iterations, about 1.7 ms per solve
- [x] **Reset / Follow** phase modes. Reset locks the first active bass anchor and gates the kick tail (Gel). Follow continues the kick's own phase into every bass note (`ψ + ratio·(Φ(tₙ) − Φ(tₐ))`)
- [x] **BeatPlan**: per-beat analytic plan (T16, f0, solved kick shape, tail gate, per-note phase/gain/filter, kick RMS per step)
- [x] **Flow**: first-after-kick ducking and kick-overlap term, emphasis (±6 dB) plus accent (env mod and cutoff), roll, push. Gains exposed in `BeatPlan::flowGainDb` for the UI meter
- [x] **Kick voice**: analytic sine, attack, fishtail dip, `(1−v)^c` body, tail gate, embedded clicks (HP at 1 kHz)
- [x] **Kick saturation**: Soft, Tube, Diode, Tape shapers; bias; env-follow; matched pre/de-emphasis pair; own 4× half-band oversampler with measured group delay compensated by evaluating the analytic generator ahead in time; analytic DC removal (no high-pass, so the fundamental phase stays intact); level compensation
- [x] **Bass voice**: PolyBLEP falling saw ↔ pulse, analytic phase from the anchor, sub sine, ZDF ladder 24 and OTA/SVF 12, exponential envelopes in T16 units, post-VCA drive, 3rd-harmonic dip, seeded per-step analog variation that never touches pitch or phase
- [x] **Engine**: fractional-sample beat scheduling from host beat position with a 12 ms lookahead (so negative Push works), jump/loop detection with 3 ms fade-out, stop handling, Host and MIDI Gate play modes (internal clock when stopped), held-note root override, output sanitising
- [x] `renderBeatPreview()` pure function with separate kick and bass stems for the future scope
- [x] Embedded click bank: 8 deterministic clicks from `tools/make_clicks.py`, windowed-sinc resampled to the host rate

### Plugin and tools
- [x] CLAP entry, factory, descriptor (`com.holstebroe.kikset`), extensions `params`, `state`, `audio-ports`, `note-ports` (CLAP + MIDI), `latency` (0)
- [x] Transport events handled mid-block; events split the block at their sample time
- [x] Versioned text state (`KIKSET1`), unknown ids ignored, values sanitised on load
- [x] `kikset_render` CLI (`--bpm --key --bars --sr --out --stems --param id=val`)
- [x] `clap_smoke` test: loads the built `.clap` through `dlopen` and drives it as a minimal host (entry, factory, params, state round trip incl. junk input, processing, and the X11 GUI under Xvfb)

### GUI (new)
- [x] `src/gui/Graphics`: software renderer (AA lines and arcs, 5x7 bitmap font, PPM output), no dependencies
- [x] `src/gui/Panel`: 900x560 panel with all ~45 controls: step buttons with playhead, knobs (drag, Shift = fine, wheel, double-click = reset), choice boxes, solver read-outs (nudge, Δpitch, Δphase, f0, lock state, residual)
- [x] **Wide one-beat scope**: kick (orange) and bass (green) drawn from separate stems, step lanes, anchor lines, phase-error badge, per-step Flow gain in dB, pitch-curve overlay on a log Hz axis, settle point, Reset tail-gate marker, playhead
- [x] **Handoff zoom** (±3 ms around the first anchor, kick and bass traces)
- [x] Preview is rendered on the GUI thread by `renderBeatPreview()` whenever a parameter or the tempo changes, so the scope works with the transport stopped
- [x] `X11Window` (embed or top-level) plus `kikset_gui_demo` (`--screenshot`) for trying it without a host
- [x] CLAP `gui` (X11) and `timer-support` extensions; lock-free GUI → audio edit queue with `request_flush`, param and gesture events reported to the host
- [x] `gui_test`: renders frames and checks drag, wheel, double-click reset, step toggle, choice cycling and gesture reporting

### Tests (all pass under `ctest`)

| Test | Result |
|---|---|
| Anchor exactness: 12 keys × 130–180 BPM × Lock {0, .5, 1} × Reset/Follow (3672 cases) | worst residual 9.8e‑11 cycles, all adjusters in range; Follow notes match the kick's own phase to 1e‑6 |
| Audio-domain anchor phase (kick stem, least-squares fit at the anchor) | worst error 1.3°, limit 2° |
| Oversampler delay | measured 21.75 samples, constant matches within 0.05° |
| Saturation phase safety (4 types × bias ±1 × tone ±1 × env-follow {0, .3, 1}, drive 1 vs 0) | ≤ 1° for env-follow ≤ 0.3; worst 4.2° at env-follow 1 (see open issue 4) |
| DC drift with saturation | worst DC/fundamental 0.033 (−29.6 dB) |
| Determinism (beat 2 vs beat 102 at integer samples per beat) | bit-identical (onsets within 1e‑4 samples of a sample boundary snap to it) |
| Reset tail gate / Follow keeps tail | pass |
| Flow (first-after-kick ≈ −2.8 dB, first moves when steps toggle, emphasis +1 = +6.02 dB) | pass |
| Stems sum to mix | pass |
| Transport: jump resync, no mid-beat start, MIDI Gate on stopped transport, root from note | pass |
| Robustness: hostile params, tempo, beat position, events, extreme corners | no NaN/Inf, output ≤ clamp |
| Kick defaults | retuned after research, see below |
| Perf | 2.5 % of one core at 48 kHz (target < 1 %, see open issue 3) |

### Kick default retune
The first defaults (Sweep 3 oct from ~350 Hz, Length 1.05, Gel 0.1, Drive 0.3, Click −12 dB) gave a short, thin, high-sounding kick. In the default Reset mode the tail gate cut the body at about 1.1 T16 (≈ 114 ms at 145 BPM), and the sweep only reached ~350 Hz. Web sources give these ranges:
- fundamental 45–55 Hz (up to 65 Hz), sine body, exponential pitch and amplitude curves, pitch drop done within tens of ms ([Soundbridge](https://www.soundbridge.io/kick-drum-synthesis-tutorial), [Mode Audio](https://modeaudio.com/magazine/drum-synth-sound-design-kick-snare), [Myloops](https://www.myloops.net/creating-a-clean-trance-low-end))
- psytrance: one sine, a pitch envelope that "starts high and quickly decays", sweep plus a short "thunk", low end around 50–60 Hz ([KVR: Psytrance Kick EQ](https://www.kvraudio.com/forum/viewtopic.php?p=6819098), [Progressive trance kick](https://www.kvraudio.com/forum/viewtopic.php?t=508056))
- a generic example is 150 Hz → 48 Hz over ~50 ms with a ~450 ms body

Several of these pages could not be opened from this environment, so the numbers come from search snippets only. The new defaults: Sweep 4.5 oct (≈ 1 kHz start, ~100 Hz after 5 ms, ~60 Hz after 10 ms, settled by ~20 ms), Curve 14, Punch 0.35, Settle 0.7, Length 1.5, Gel 0.35, Kick Drive 0.2, Click −18 dB. f0 stays in the 38–76 Hz window (F 43.7, G 49, A 55 Hz). These are starting points to judge by ear (item 4 below), not reference values.

### Deviations from the design (decided while building)
- **Sub** is added after the filter and before the VCA, so the low end is not filtered. The design listed it with the oscillator
- **Reset-mode tail gate** ends at `t_anchor + Gel·T16` (capped before the next active anchor). With the default Length 1.05 and Gel 0.1 this matches the natural tail end. Longer kicks are meant for Follow mode
- **Trigger nudge** is limited to 0…+6 ms: the kick cannot start before the beat grid without latency. The other adjusters absorb the sign
- **After a transport jump** both kick and bass wait for the next beat, not the next step
- **Bass filter state** resets at every note onset (the VCA is closed there). This makes every beat render identically
- **Click set** has 8 clicks; the Click Type range is 0–7

## Remaining

Priority: **P1** blocks a usable v1, **P2** needed for release quality, **P3** nice to have or v1.1.

### P1
1. **GUI on Windows and macOS.** Only the X11 window exists. Needs `Win32Window` and `CocoaWindow` (the `Panel` is portable) and the matching CLAP `gui` API branches. Also HiDPI scaling (`set_scale` is accepted but ignored)
   - Acidus' `GuiWindow` sources were not available in this environment, so the window code is new rather than ported
   - Scope shows the preview beat, not the live audio beat. The design's audio → GUI triple buffer is not built
   - Missing from the design: log/dB scope toggle, faint sum trace, scope drag-editing of Curve/Punch (v1.1), Advanced page for the remaining params (Bass Oct, Interval etc. are on the main panel already)
2. **Verify the plugin in a real host and with `clap-validator`.** Only the `clap_smoke` test exists (fake host, X11 under Xvfb). Add it to CI (`xvfb-run`)
3. **Cross-platform build.** The CI workflow has not run yet. MSVC (`/W4`), Apple clang and `strcasecmp` in `KiksetClap.cpp` need checking
4. **Listening A/B** (3 tempos × 3 keys, Reset vs Follow, Lock Style extremes). It also decides the two open design questions: default phase mode, and solver weights

### P2
5. **Performance to < 1 %.** Now 2.5 %. The cost is mainly the per-sample DC quadrature (64 shaper evaluations every 2 samples), the 31-tap FIR stages and `tan()` in the bass filter. Options: cache the DC table per (gain, bias, type) and interpolate, use polyphase half-band taps, update cutoff coefficients every N samples
6. **Env-follow + bias phase/DC accuracy.** Worst case 4.2° and −29.6 dB at env-follow 1 with bias ±1. It is measurement-limited (see the test comment), but the design asks for ±1° and −30 dB. Either tighten by limiting the Bias/Env-Follow ranges as the design suggests, or improve the estimator
7. **Presets per subgenre** (Full-on, Forest, Goa, Prog, Hi-tech) and CLAP preset-load or a factory-state list
8. **Audio-domain analysis script** `tools/analyze_render.py` (design: Hilbert phase in the 30–90 Hz band). The C++ least-squares fit covers the kick today. Extend to bass note starts and per-step RMS
9. **Missing tests from the plan:** handoff continuity (< −60 dB in 30–90 Hz at locked anchors, Follow), scope publish is allocation/lock-free (needs the GUI buffer), perf on CI
10. Replace synthetic clicks with recorded ones if wanted
13. **Docs:** parameter reference, how the solver and Lock Style behave, tuning guide

### P3 / v1.1
14. Bass **2× oversampling** of filter + drive (runtime flag), **Diode filter** port from Acidus
15. **Kick Tone** tilt; sub one octave down; bass-onset-shift as a fifth solver adjuster
16. **Multi-out** (kick, bass) and optional stereo top above 200 Hz
17. **Triplet grid** (3 steps per beat, `T_step = T_beat/3`). The solver is grid-agnostic, so this should be cheap
18. Bar-level variations (octave drop every 4/8 bars, root → 5th); sample import
19. `thread-check`, `render` (offline hint) and `latency` for oversampling options, if added
20. Open design questions still undecided: sweep in ms vs T16 (Q3), Flow loudness measure K-weighted vs RMS (Q4), host-generic module paths (Q6)

## Known risks

| Risk | Mitigation |
|---|---|
| Solver cost on tempo automation (≈ 2 ms per new tempo, once per beat) | Cached by input. Consider a coarser tempo quantisation or an analytic Jacobian if profiling shows spikes |
| Reset-mode hard reset may click on very short gates | Note start sits where the previous note is silent, attack ≥ 50 µs. Check by ear (item 4) |
| Hosts that provide no beat timeline | Host mode idles. MIDI Gate self-clocks at host tempo or 145 BPM |
| Saturation with strong asymmetry moves the fundamental by a few degrees in transitions | Item 6 |

## How to verify the current state

```sh
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure
build/kikset_render --bpm 145 --key 5 --bars 4 --out groove.wav --stems
```
