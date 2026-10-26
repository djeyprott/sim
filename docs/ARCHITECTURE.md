# Architecture

histsim models human history as the emergent result of geography, climate,
population, culture, economy and technology on an Earth-scale planet. This page
covers how the code is organized and the rules that keep runs reproducible.

## Principles

1. **Emergence over scripting.** Systems expose mechanisms (rates, costs,
   probabilities), never outcomes. There is no historical seeding: every run
   grows from a generic forager start.
2. **Determinism.** Same seed and inputs give a bit-identical run everywhere.
3. **Conservation.** People, goods, carbon and money are stocks and flows with
   ledgers, so imbalances are visible and testable.
4. **Multi-scale time.** The base tick is one month; each system runs at its
   own period.
5. **Hybrid granularity.** Cohorts carry the statistics; individual agents exist
   only where individuals matter (from M3).
6. **Data-driven content.** Goods, techs, diseases and institutions will live
   in versioned data files, not C++.
7. **Sim/view separation.** The core is a headless library. The front end reads
   state and sends commands through the Sim API only.
8. **Explainability.** Notable changes are logged as events with their causes.

## Components

```
cli (histsim) ──────────┐
                        ├── Sim API (simulation.hpp) ── Scheduler ── Systems
godot viewer ── view ───┘                                   │
                                                   ├── SimState (dynamic, saved, hashed)
                                                   └── World (static terrain + climate)
                                                         └── Grid (geodesic cells)
```

| File | Role |
| --- | --- |
| `grid.hpp` | Geodesic grid as plain data: cell centers (unit vectors), areas, CCW neighbor lists. `make_icosahedral_grid(level)` builds it. |
| `world.hpp` | Static world: terrain, baseline climate, starting forager groups, and the coarse climate grid with per-cell interpolation weights. `generate_test_world()` is the stand-in generator until M4. |
| `state.hpp` | Everything that changes during a run: cohorts, climate state, this year's environment, the ledger, the event log and yearly history. |
| `src/state_io.hpp` | One `serialize()` walk over `SimState`, used for hashing, saving and loading, so all three always agree. |
| `rng.hpp` | Counter-based RNG: (seed, tick, stream, entity) is hashed with Philox4x32-10 into a start state, then draws come from SplitMix64. |
| `portable_math.hpp` | `sin`, `cos`, `asin` and normal draws built from arithmetic only, identical on every platform. |
| `scheduler.hpp` | Phases, the `System` interface, and the per-tick loop. |
| `simulation.hpp` | The Sim API: create, step, save, load, read state, hash. |
| `systems/climate.hpp` | Climate (global energy balance, regional anomalies) and environment (yearly capacity, game). |
| `systems/people.hpp` | Cohort demography, migration and the yearly history record. |
| `view/` (separate library) | Map modes, color scales, legends, pixel-to-cell rasterizing and PNG output. Display only: it reads state and never writes it, so the determinism rules do not apply to it. |
| `godot/extension/` | GDExtension class `HistSimWorld`: the Sim API plus `view/`, exposed to GDScript. |

### Map modes

A map mode (view/src/map_modes.cpp) reads one value per cell and maps it to a
color, with a legend. Magnitudes use one hue from light to dark; signed
quantities (temperature) use a blue-red diverging scale with a neutral
midpoint; ocean and empty land are neutral grays so they never read as data.
Every mode added there shows up in the viewer and `histsim map` automatically.

### Systems

A system is a stateless class with a phase, a period in ticks, and a `step()`.
All changing data lives in `SimState`, so a snapshot captures the whole run.
Within a tick, phases run in this order:

climate → environment → production → markets → demography → migration →
culture and technology → polities → agents → events

Systems in the same phase run in registration order. Within a system, each
cohort or cell is updated from values computed before the pass (cell
populations, capacity) and with its own random stream, and cross-cell moves go
to an outbox merged afterwards. So processing order does not change results,
which leaves room to parallelize later. After any change to cohorts,
`SimState::normalize_cohorts()` re-sorts and merges them and rebuilds the
derived per-cell totals.

### People and environment (M1)

People live in cohorts: one per (cell, culture, language), each with an
age-sex pyramid in 5-year bins up to 100+. Counts are integers; every birth,
death and move goes through the ledger, so `population == initial + births -
deaths` holds exactly.

| System | Model |
| --- | --- |
| Climate | One-box energy balance, C dT/dt = F - λT, with C = 8 W·yr·m⁻²·K⁻¹ and λ = 1.3 W·m⁻²·K⁻¹. Forcing: four Milankovitch terms (small in the global mean), solar wander, and volcanic eruptions (about one large eruption per 60 years, -1 to -4 W/m², clearing in about a year). Regional AR(1) temperature and rainfall anomalies on a 162-cell coarse grid, interpolated to every cell. |
| Environment | Capacity = forager density for this year's temperature and rainfall × area × (0.4 + 0.6 × game). Game regrows logistically and is depleted when people exceed 60% of the climate's capacity. |
| Demography | Average hunter-gatherer life table (Siler fit, Gurven & Kaplan 2007: life expectancy 31 at birth, 38 more at 15) and a natural-fertility age pattern with total fertility 5.4 (Marlowe 2005), precomputed by `tools/vital_rates.py`. Fertility falls linearly to replacement (total fertility 4.4) as a cell reaches capacity; past capacity, famine raises mortality. Empty land grows about 0.7% a year. |
| Migration | 0.5% of people a year explore; crowded cells send more (rising with crowding squared); a cell with no food empties. Each cohort's movers travel as one band to a neighbor picked by spare capacity over climbing cost. Young adults move most. Groups under 20 people or 10% of their cell join the cell's largest group. |
| History | One row per year: population, cumulative births, deaths and migrants, inhabited cells, global temperature anomaly, total capacity. |

Typical behavior: a world starting at half capacity grows for two or three
centuries, then holds near 90% of capacity, dipping after eruptions and in cold
spells.

### Snapshots

`Simulation::save()` writes a little-endian binary file: magic, format version,
world parameters, world hash, then `SimState` as described by `serialize()` in
`src/state_io.hpp`, then a checksum. `load()` regenerates the world from the parameters and refuses the
file if the regenerated world's hash differs. Bump `kSnapshotVersion` whenever
the layout changes.

## Determinism rules

A run must be bit-identical on every compiler, OS and CPU we support. The CI
`determinism` job and the `sim_golden_hash` unit test enforce this.

**Floating point**

- Use only `+ - * /`, `sqrt`, `floor`, `fabs`, comparisons and casts on any
  value that feeds simulation state. These are exactly specified by IEEE 754.
- Do not call `sin`, `cos`, `exp`, `log`, `pow`, `atan2`, `asin` and similar in
  simulation code: their results differ between C libraries. If you need one,
  use `portable_math.hpp`, write a portable version from arithmetic, or
  precompute a table offline (as `tools/vital_rates.py` does for the life
  table).
- z on the unit sphere is the sine of latitude, so most latitude logic needs no
  trig at all.
- Never enable `-ffast-math` or FMA contraction. The build sets
  `-ffp-contract=off` and `/fp:precise`.
- Display helpers like `Grid::latitude_deg_display()` may use libm, but their
  results must never flow back into state.

**Randomness**

- All random numbers come from `RngStream(seed, tick, stream, entity)`. Never
  use `std::rand`, `std::random_device` or `<random>` distributions (their
  algorithms differ between standard libraries).
- Give each system its own `Stream` value. Append new ones; never renumber.
- Use `stochastic_round()` to turn an expected count into an integer.

**Ordering**

- Never iterate an `unordered_map` or `unordered_set` in a way that affects
  state. Use them for lookup only.
- `std::sort` is not stable, and equal elements may end up in different orders
  on different standard libraries. Give comparators a tie-break, or use
  `std::stable_sort`.
- Sum conserved quantities (people, goods, money) as integers.

**When behavior changes on purpose**

Update the constant in `sim_golden_hash` (tests/test_simulation.cpp) with the
output of `histsim run --seed 1 --level 5 --years 200 --hash-only`, and say so
in the commit message. CI then confirms every platform agrees on the new value.

## Grid notes

The icosahedral grid has 10 · 4^level + 2 cells: 12 pentagons, the rest
hexagons. Cell areas vary by up to about 1.8× (comparable to H3), so systems
must always use `area_km2()`. A true equal-area layout (Snyder's ISEA
projection) is a possible later upgrade; because `Grid` is plain data, any
generator can fill it without changing systems.

## Roadmap

| Milestone | Simulation | Front end | Gate |
| --- | --- | --- | --- |
| M0 Foundations (done) | Grid, test-world generator, scheduler, RNG, snapshots, headless runner | Sim API, map viewer | Identical replays on Windows, Linux and macOS |
| M1 Living planet (done) | Climate and environment systems, cohort demography, migration, foragers | Globe, climate and population map modes, pyramid, charts | Forager populations settle near carrying capacity (`m1_gate_populations_settle_near_capacity`) |
| **M2 Neolithic** (next)  | Agriculture, herding, disease, culture and language drift, early tech | Culture and language map modes, language tree | Farming arises in several plausible regions |
| M3 States and trade | Goods, markets, transport, polities, war, religion, notable agents, forced labor and the slave trade | Trade flows, inspectors, explain view, timeline | Polity sizes track Seshat ranges by era |
| M4 Modern era | Energy, carbon cycle, demographic transition, Earth importer, procedural generator | God mode, branch comparison, modding tools | Full runs to the near future on Earth and generated worlds |

M0 to M3 run on the generated test world, so the foundation is solid before any
real data is involved.
