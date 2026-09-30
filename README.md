# Kikset

Kikset is a combo kick and bass synthesizer for creating fast phase synchronized trance beats.
One engine on one timeline renders a finished K‑B‑B‑B kick+bass groove at any tempo and key, with the
kick's phase solved analytically so the bass notes start on it. CLAP only, modern C++, CMake, no
dependencies except the CLAP headers.

- Design: [`Kickset_design.md`](Kickset_design.md)
- Status, completed and remaining work: [`docs/PROJECT_PLAN.md`](docs/PROJECT_PLAN.md)

## Build

```sh
git submodule update --init          # CLAP headers
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build               # anchor, dsp, robustness, perf
```

The plugin is `build/Kikset.clap`. Set `CLAPTEST` to a directory to have each build copied there.

## Render from the command line

```sh
build/kikset_render --bpm 145 --key 5 --bars 4 --out groove.wav --stems --param 4=0.8
```

`--param id=value` uses the ids in `src/core/Params.hpp`. `--stems` also writes `.kick.wav` and `.bass.wav`.

## Layout

| Path | Content |
|---|---|
| `src/core` | Pure DSP: anchor solver, kick/bass voices, flow, engine. No CLAP/GUI includes. |
| `src/clap` | CLAP entry, params, state, note and transport glue |
| `src/gui` | Portable panel and software renderer, X11 window |
| `src/tools`, `src/tests` | Render CLI, anchor/DSP/robustness tests, perf bench |
| `tools/make_clicks.py` | Synthesises the click set in `resources/clicks/` |
| `cmake/EmbedResources.cmake` | Embeds the click WAVs into `generated/ClickData.cpp` |

## Editor (Linux / X11)

The plugin embeds a 900x560 editor through the CLAP `gui` extension. To try it without a host:

```sh
build/kikset_gui_demo                       # opens a window
xvfb-run -a build/kikset_gui_demo --screenshot panel.ppm
python3 tools/ppm2png.py panel.ppm panel.png
```
