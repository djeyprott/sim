# histsim viewer (Godot 4)

A simple map interface for the simulation: switchable map modes with legends,
play/pause and speed controls, and a hover inspector for any cell.

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
| 1-5 | Switch map mode |
| . (period) | Step one year while paused |
| Hover | Inspect a cell |
| New world / Random seed | Generate a new planet |

## How it draws

`view/` builds a lookup from every pixel of a 2048 × 1024 equirectangular map
to the grid cell under it, once per world. Each redraw colors cells for the
active map mode and fills pixels through that lookup, so a frame costs a few
milliseconds at any grid level, and hover is a single lookup. The same image
can later be wrapped on a sphere for a globe view.
