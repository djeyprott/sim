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
cli (histsim) ─┐
               ├── Sim API (simulation.hpp) ── Scheduler ── Systems
godot (later) ─┘                                   │
                                                   ├── SimState (dynamic, saved, hashed)
                                                   └── World (static terrain + climate)
                                                         └── Grid (geodesic cells)
```

| File | Role |
| --- | --- |
| `grid.hpp` | Geodesic grid as plain data: cell centers (unit vectors), areas, CCW neighbor lists. `make_icosahedral_grid(level)` builds it. |
| `world.hpp` | Static world fields per cell. `generate_test_world()` is the M0 stand-in generator. |
| `state.hpp` | Everything that changes during a run: double-buffered fields, the ledger, the event log. |
| `rng.hpp` | Philox4x32-10 counter-based RNG keyed by (seed, tick, stream, entity). |
| `scheduler.hpp` | Phases, the `System` interface, and the per-tick loop. |
| `simulation.hpp` | The Sim API: create, step, save, load, read state, hash. |
| `systems/` | Simulation systems. M0 has placeholder forager demography and migration. |

### Systems

A system is a stateless class with a phase, a period in ticks, and a `step()`.
All changing data lives in `SimState`, so a snapshot captures the whole run.
Within a tick, phases run in this order:

climate → environment → production → markets → demography → migration →
culture and technology → polities → agents → events

Systems in the same phase run in registration order. A system reads a field's
previous values with `read()`, writes new ones with `write()`, then `commit()`s.
Because nothing reads what it is writing in the same pass, cell order does not
matter, which leaves room to parallelize later.

### Snapshots

`Simulation::save()` writes a little-endian binary file: magic, format version,
world parameters, world hash, tick, ledger, per-cell state, the event log and a
checksum. `load()` regenerates the world from the parameters and refuses the
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
  write a portable version from arithmetic (see `portable_asin` in `grid.cpp`)
  or precompute a table.
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
| **M0 Foundations** (current) | Grid, test-world generator, scheduler, RNG, snapshots, headless runner | Sim API | Identical replays on Windows, Linux and macOS |
| M1 Living planet | Climate and environment systems, cohort demography, migration, foragers | Godot globe, climate and population map modes | Forager populations settle near carrying capacity |
| M2 Neolithic | Agriculture, herding, disease, culture and language drift, early tech | Culture and language map modes, language tree | Farming arises in several plausible regions |
| M3 States and trade | Goods, markets, transport, polities, war, religion, notable agents, forced labor and the slave trade | Trade flows, inspectors, explain view, timeline | Polity sizes track Seshat ranges by era |
| M4 Modern era | Energy, carbon cycle, demographic transition, Earth importer, procedural generator | God mode, branch comparison, modding tools | Full runs to the near future on Earth and generated worlds |

M0 to M3 run on the generated test world, so the foundation is solid before any
real data is involved.
