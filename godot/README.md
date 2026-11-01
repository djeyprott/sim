# histsim viewer (Godot 4)

A simple interface for the simulation: a flat map or a globe with switchable
map modes and legends (terrain, biomes, climate, rivers, soils, wild species,
population, languages, language families, cultures, farming, known
domesticates, way of life, disease, technology, polities, realms, war,
stability, towns, trade and resources, any culture trait (from the "Culture
trait" picker), and
more), overlays for coastlines, rivers, polity borders and trade routes, play/pause and
speed controls, world charts of population vs. capacity and global
temperature, a cell inspector (hover to look, click to keep) with the cell's age pyramid and its peoples
(language, lineage, sample words, culture and its ways, farming, crops and
animals, domestication under way, techniques, diseases, deposits, what is on
its market, its trade and trade route, its ruler's overlord, subjects, allies
and wars), and a god-mode panel
for meddling with the world.

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
| Tab / Shift+Tab | Next / previous group of map modes (Land, People, Culture, Economy, Power) |
| 1-9, 0 | Switch among the map modes of the group shown |
| Rivers checkbox | Show / hide rivers (line width grows with flow) |
| Borders checkbox | Show / hide the borders of chiefdoms and states |
| Trade routes checkbox | Show / hide the busiest trade routes |
| Migrations checkbox | Show / hide the last 50 years' folk migrations as arrows |
| Names checkbox | Show / hide the names of states, empires and other large realms (over 400,000 km²), sized to their land, on the map and the globe |
| Scroll, drag (map) | Zoom in and out around the cursor, pan when zoomed in |
| Home | Zoom the map back out to the whole world |
| G | Toggle map / globe (drag to turn, the grabbed point stays under the cursor; scroll to zoom) |
| . (period) | Step one year while paused |
| F2 | Show / hide god mode |
| Click (god mode) | Apply the selected tool where you click (map or globe) |
| Esc | Back to inspect-only; then let go of the kept cell |
| F5 / F9 | Quicksave / quickload |
| Hover | Show a cell in the inspector |
| Click | Keep that cell in the inspector (outlined) so the panel can be scrolled; click it again to let go. In the language, language family, culture, religion, polity, realm and stability modes the map also brings out every cell that shares its language, culture, faith or ruler, fading the rest; cells where its language, family or culture is only a minority are tinted faintly with its color (more strongly the larger the minority) |
| Click a section header | Fold or unfold that part of the inspector (Place, People, Language, Culture, Culture traits, Religion, Food, Rule, Economy, Technology, Health); folds are kept as you move between cells, and culture traits start folded |
| New world / Random seed | Generate a new planet |

## God mode

Debug tools for meddling with a running world. Every action is recorded in the
run (see "Interventions" in the architecture doc), so saves and replays keep it.

| Tool | Effect |
| --- | --- |
| Erupt now | Volcanic eruption with the chosen forcing (Pinatubo 1991 peaked near 3 W/m²); cools the planet for a few years |
| Set climate forcing | Persistent forcing in W/m² (about +3.7 for doubled CO₂); the planet warms or cools toward forcing ÷ 1.3 °C |
| Start a Bond event | A 200-year Holocene cold spell: high northern latitudes cool by up to the chosen amount and monsoons weaken |
| Drought, Wet years | Cut or add rainfall over an area for a number of years |
| Cold snap, Heat wave | Lower or raise temperature over an area for a number of years |
| Plague (instant deaths) | A share of everyone in the area dies |
| Epidemic (bring a disease) | Brings the chosen disease to the area; it strikes the next year and spreads by itself |
| Wipe out | Everyone in the area dies |
| Settlers | A band of a new group appears in the clicked cell |
| Deplete game, Restore game | Set wild game in the area to 5% or 100% |

Hovering with a tool selected outlines the cells it will touch. Interventions
and independent domestications show as yellow lines on the population chart
(which also plots farmers), eruptions as lines and Bond events as blue bands
on the temperature chart.

## How it draws

`view/` builds a lookup from every pixel of a 2048 × 1024 equirectangular map
to the grid cell under it, once per world. Each redraw colors cells for the
active map mode and fills pixels through that lookup, so a frame costs a few
milliseconds at any grid level, and hover is a single lookup. The same image
can later be wrapped on a sphere for a globe view.
