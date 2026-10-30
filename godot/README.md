# histsim viewer (Godot 4)

A simple interface for the simulation: a flat map or a globe with nine
switchable map modes and legends, play/pause and speed controls, world charts
of population vs. capacity and global temperature, and a hover inspector with
the cell's age pyramid, and a god-mode panel for meddling with the world.

The viewer is thin by design. All simulation and map rendering happen in C++:
`extension/` builds a GDExtension that exposes one class, `HistSimWorld`,
wrapping the Sim API and the display layer in `view/`. `project/main.gd` only
lays out the UI and forwards input. A new map mode added in
`view/src/map_modes.cpp` appears in the viewer with no GDScript changes.

## Download

Every push to `main` builds a Windows viewer: open
[Actions > Viewer](https://github.com/djeyprott/sim/actions/workflows/viewer.yml),
pick the latest run, and download `histsim-viewer-windows` under **Artifacts**.
Unzip, keep the `.dll` next to `histsim-viewer.exe`, and double-click the exe.

## Building from source

Requires Godot 4.5 or newer (exports are made with 4.7.2) and the toolchain
from the main README. godot-cpp is downloaded automatically.

```sh
# 1. Build the extension into godot/project/bin/
cmake -S . -B build-godot -DCMAKE_BUILD_TYPE=Release -DHISTSIM_BUILD_GODOT=ON \
      -DHISTSIM_BUILD_TESTS=OFF -DHISTSIM_BUILD_CLI=OFF
cmake --build build-godot --config Release --target histsim_godot

# 2. Open godot/project in the Godot editor and press F5,
#    or run the headless smoke test:
godot --headless --path godot/project --script res://tests/smoke.gd
```

## Controls

| Input | Action |
| --- | --- |
| Space | Play / pause |
| 1-9, 0 | Switch map mode (the rest are in the list) |
| Rivers checkbox | Show / hide rivers (line width grows with flow) |
| G | Toggle map / globe (drag to turn, scroll to zoom) |
| . (period) | Step one year while paused |
| F2 | Show / hide god mode |
| Click (god mode) | Apply the selected tool where you click (map or globe) |
| Esc | Back to inspect-only |
| F5 / F9 | Quicksave / quickload |
| Hover | Inspect a cell |
| New world / Random seed | Generate a new planet |

## God mode

Debug tools for meddling with a running world. Every action is recorded in the
run (see "Interventions" in the architecture doc), so saves and replays keep it.

| Tool | Effect |
| --- | --- |
| Erupt now | Volcanic eruption with the chosen forcing (Pinatubo 1991 peaked near 3 W/m²); cools the planet for a few years |
| Set climate forcing | Persistent forcing in W/m² (about +3.7 for doubled CO₂); the planet warms or cools toward forcing ÷ 1.3 °C |
| Drought, Wet years | Cut or add rainfall over an area for a number of years |
| Cold snap, Heat wave | Lower or raise temperature over an area for a number of years |
| Plague | A share of everyone in the area dies |
| Wipe out | Everyone in the area dies |
| Settlers | A band of a new group appears in the clicked cell |
| Deplete game, Restore game | Set wild game in the area to 5% or 100% |

Hovering with a tool selected outlines the cells it will touch. Interventions
show as yellow lines on the population chart, and eruptions on the
temperature chart.

## How it draws

`view/` builds a lookup from every pixel of a 2048 × 1024 equirectangular map
to the grid cell under it, once per world. Each redraw colors cells for the
active map mode and fills pixels through that lookup, so a frame costs a few
milliseconds at any grid level, and hover is a single lookup. The same image
can later be wrapped on a sphere for a globe view.
