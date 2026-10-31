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
| `world.hpp` | Static world: terrain, baseline climate and seasons, biomes, rivers, soils, wild species ranges, starting forager groups, and the coarse climate grid with per-cell interpolation weights. `generate_test_world()` is the stand-in generator until M4. |
| `state.hpp` | Everything that changes during a run: cohorts, climate state, this year's environment, the ledger, the event log and yearly history. |
| `src/state_io.hpp` | One `serialize()` walk over `SimState`, used for hashing, saving and loading, so all three always agree. |
| `content.hpp`, `json.hpp` | Content packs: the JSON files in `data/`, compiled into the library, parsed and validated at startup. |
| `rng.hpp` | Counter-based RNG: (seed, tick, stream, entity) is hashed with Philox4x32-10 into a start state, then draws come from SplitMix64. |
| `portable_math.hpp` | `sin`, `cos`, `asin` and normal draws built from arithmetic only, identical on every platform. |
| `scheduler.hpp` | Phases, the `System` interface, and the per-tick loop. |
| `simulation.hpp` | The Sim API: create, step, save, load, read state, hash. |
| `systems/climate.hpp` | Climate (global energy balance, regional anomalies) and environment (yearly capacity, game). |
| `systems/people.hpp` | Cohort demography, migration and the yearly history record. |
| `language.hpp` | Languages as objects: proto-language generation, regular sound changes, word replacement, lexical similarity, names. |
| `systems/disease.hpp` | Endemic disease burdens and epidemics: emergence, outbreaks, immunity, spread. |
| `systems/technology.hpp` | Techniques known per cell: invention, borrowing, forgetting, and their effects. |
| `systems/farming.hpp` | Domestication, knowledge of domesticates, farming share, farmland capacity and harvests. |
| `systems/culture.hpp` | Culture and dialect drift, contact, adaptation, language shift, assimilation, splits, extinctions; founding new peoples. |
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
| Climate | One-box energy balance, C dT/dt = F - λT, with C = 8 W·yr·m⁻²·K⁻¹ and λ = 1.3 W·m⁻²·K⁻¹. Forcing: four Milankovitch terms (small in the global mean), solar wander, and volcanic eruptions (about one large eruption per 60 years, -1 to -4 W/m², clearing in about a year). Regional AR(1) temperature and rainfall anomalies on a 162-cell coarse grid, interpolated to every cell. Bond events (below). |
| Environment | Forager capacity = forager density for this year's temperature and rainfall × area × (0.4 + 0.6 × game + aquatic bonus); a cell's capacity mixes it with farmland by its farming share (below). Game regrows logistically and is depleted when people exceed 60% of the climate's capacity. |
| Demography | Average hunter-gatherer life table (Siler fit, Gurven & Kaplan 2007: life expectancy 31 at birth, 38 more at 15) and a natural-fertility age pattern with total fertility 5.4 (Marlowe 2005), precomputed by `tools/vital_rates.py`. Fertility falls linearly to replacement (total fertility 4.4) as a cell reaches capacity; past capacity, famine raises mortality. Empty land grows about 0.7% a year. |
| Migration | 0.5% of people a year explore; crowded cells send more (rising with crowding squared); a cell with no food empties. Each cohort's movers travel as one band to a neighbor picked by spare capacity over climbing cost. Young adults move most. Groups under 20 people or 10% of their cell join the cell's largest group. |
| History | One row per year: population, cumulative births, deaths and migrants, inhabited cells, global temperature anomaly, total capacity. |

Typical behavior: a world starting at half capacity grows for two or three
centuries, then holds near 90% of capacity, dipping after eruptions and in cold
spells.

### World geography (M2)

`generate_test_world()` builds, in order:

| Step | Model |
| --- | --- |
| Terrain | Fractal noise continents; sea level set so the requested share of cells is ocean. |
| Temperature | Mean annual T = 27 − 30z² − 17z⁴ − 6.5 × altitude in km (z = sine of latitude). |
| Rainfall | A zonal profile (wet equator, dry subtropics near 30°, wet westerlies, dry poles) scaled by moisture from upwind: each cell traces back along its prevailing wind (trades and polar easterlies from the east, westerlies from the west between 30° and 60°) and dries with the distance of land crossed. A ridge more than 800 m above the cell upwind casts a rain shadow; otherwise height lifts rain on the windward side. |
| Seasons | Temperature range grows with latitude and continentality (land upwind); rain season is signed (−1 winter rain, Mediterranean; +1 summer rain, monsoon). Monthly values come from these, peaking in July in the north and January in the south. |
| Biomes | Köppen-style classification from the twelve months: ice, tundra, boreal, temperate, warm temperate, Mediterranean, grassland, shrubland, desert, savanna, rainforest. Growing months = months with T ≥ 5 °C and rain ≥ 35% of potential evaporation. |
| Rivers | Runoff = max(0, rain − 0.8 × potential evaporation). Priority-flood drainage (lakes and pits are filled) gives every land cell one downstream cell, and discharge and basin area accumulate downstream. A cell is a river when its basin is at least 150,000 km² and its flow at least 300 m³/s, so the network looks alike at every grid level. |
| Soils | Base fertility by biome, lower on steep relief, plus alluvium on large rivers. |
| Aquatic foods | Coasts and rivers add up to +80% foraging capacity (fish and shellfish, not depleted by hunting). |
| Wild species | Each species in `data/species.json` has trapezoid niches (coldest and warmest month, rainfall, rain season, elevation) and a river affinity. Suitability is their product. Homelands are picked from suitable cells (the best of 24 tries by spread, far apart from each other) and ranges spread from them up to `range_km`. Stored as a 32-bit presence mask and an abundance byte per cell. |

Content packs are plain JSON with `//` comments. The reader parses numbers
exactly (decimal digits up to 15 and powers of ten up to 22, a single correctly
rounded operation) and rejects anything it cannot read exactly, so content
never depends on the platform's `strtod`. The pack's hash goes into the world
hash: a snapshot made with different content is refused.

### Culture and language (M2)

Culture and language are separate systems linked through cohorts: a cohort is
keyed by (cell, culture, language) and carries its own copy of its culture's
traits (`data/culture.json`, 0–1 each) and a dialect point (8 numbers).

| Process | Model |
| --- | --- |
| Start | Each starting group is a people: a culture with traits suited to its land plus a random character, and a language family that has already been diversifying for 3,000–7,000 years. The proto-language splits again and again along the longest axis of its land until there is about one language per 300,000 km², so a run starts with ~500 languages in ~80 families. Extinct ancestors ("Proto-…") stay in the tree with negative founding and split ticks (years before the run). |
| Traits | Each year: random drift, a pull toward the same culture in this and neighbouring cells, a weaker pull (stronger for open cultures) toward other cultures there, and adaptation toward a target set by the cell (crowding, aquatic foods, seasonality, growing season, game, wild plants). |
| Dialects | Each year: random drift and a pull toward speakers of the same language nearby, so separated groups grow apart. Pull and drift are set per 50,000 km² and scaled by cell area, so levels 5 and 6 settle at similar numbers of languages. |
| Newcomers | When cohorts merge (migrants arriving, people shifting), newcomers move the residents' traits and dialect by the square of their share: a few change little, a crowd nearly everything. |
| Shift | In a cell with several languages, 3% a year times the gap in share move from a smaller language to the largest, keeping their culture; cultures assimilate the same way at 1.5%, keeping their language. Bands under 20 people or 10% of a cell are absorbed at once (M1). |
| Splits | Every 20 years, a language whose farthest dialect is more than 0.5 from its average splits: everyone closer to that dialect than to the average forms a daughter language (both sides need 150 people). The daughter starts as a copy of its parent plus 300 years of change. Cultures split the same way on their traits (threshold 0.3). |
| Language change | Per language per year: a regular sound law from `data/phonology.json` about every 350 years, applied to every word at once (so daughters show systematic correspondences); word replacement weighted by meaning stability, keeping ~85% of the 75-word core vocabulary per thousand years (glottochronology: 81–86%); rare grammar changes. Words carry cognate ids, so lexical similarity is the share of cognates. |
| Names | A language is named by its word for "people" (or its land, river, mountains, or a new word in it) and is unique. A culture split gets a name coined in its main language. |
| Extinction | A language or culture with no one left is marked extinct and logged. |

At level 5 a run settles at about 580 languages (5 cells each) in ~70
families, with splits and extinctions roughly balanced (about 100 of each per
thousand years). `histsim languages` prints the family trees.

### Bond events and farming (M2)

**Bond events** are the Holocene's abrupt cold spells (Bond et al. 1997, 2001;
the 8.2 and 4.2 ka events). The climate system starts one about every 1,470
years (± 500), the first within 1,600 years of the start. Each lasts 150–400
years: intensity rises over the first quarter, holds to 60%, then fades. At its
peak high northern latitudes (north of ~50°) are 0.8–2 °C colder, fading to
the subtropics and 10% of that elsewhere, and summer-rain lands at low
latitudes get up to ~25% less rain (weaker monsoons). The effect on the global
mean is reported with the temperature anomaly. God mode and `--at
YEAR:bond:DEG:YEARS` start one on demand.

**Domestication** (`FarmingSystem`, production phase). Wherever a domesticable
species grows wild, progress toward domesticating it grows each year by 0.005 ×
wild abundance × domesticability × engagement × food pressure, and fades by
0.2% of itself. Engagement comes from the people's ways: plant foods (or, for
animals, 0.6 × (1 − ½ plant foods)) × (0.5 + storage) × (1.2 − mobility) × (0.5
+ innovation); pressure is people ÷ forager capacity (0.3–1.3). Only steady
engagement reaches 1, which takes centuries. A domestication more than 1,000
km from anyone who knows the species is an independent origin and is logged
(event and `SimState::domestications`). Dogs are tamed before the run
(`domesticated_before_start`).

| Process | Model |
| --- | --- |
| Knowledge | Per cell, the domesticates its people know. Learned from any neighbour that knows one (0.15% a year × (0.5 + openness), ~1 km/year as across Neolithic Europe), and carried by farming migrants, if the species can be raised there; lost when a cell empties. |
| Crops and herds | A species can be raised where its wild niche, widened by 4 °C, 30% rainfall and 500 m, still fits (`World::cultivated_suitability`). |
| Farmland | People per km² = 0.6 × best crop (suitability × food value) × soil (0.35 + 0.65 fertility) × floodplain (up to +50% on big rivers) × season (growing months), plus half of 0.15 × best herd; herds alone feed pastoralists. A regional average for early farming; later techniques raise it. Each year × harvest (rain 40–115% of normal, −12% per °C off normal). |
| Farming share | Per cohort. Grows by 1% a year × (0.3 + innovation) × (1.2 − mobility) × (strain − 0.6) × (1 − share), where strain = people ÷ forager capacity: farming spreads when foraging alone stops being enough, and raises the population so it is needed more (the Neolithic ratchet). Falls when farmland yields less than foraging; drops to zero where nothing can be farmed. |
| Capacity | forager capacity × (1 − share) + farmland × share, with the cell's population-weighted share. Hunting pressure on game comes from foragers only. |
| Farmers | 25% more births at full farming; 60% less migration. Culture: farming pushes toward settled, plant-reliant, storing, ranked, clan-based and less sharing ways. |

At level 5 the first crops appear around year 900 (often rice or yams in the
wet tropics), independent origins continue for thousands of years (squash,
lentils, sorghum, maize, sheep and goats, cattle, millets, potatoes...),
farmers are about 20% of people at year 1,500, 45% at 2,000 and two-thirds by
3,000, and population roughly doubles by year 3,000. `histsim farming` lists
the origins, Bond events and the share of farmers.

### Disease and technology (M2)

**Disease** (`data/diseases.json`, `DiseaseSystem`, just before demography).
Each year the system sets each cell's extra chance of death by age group
(infants, children, adults, elders); demography applies it with the life
table in one draw and counts the disease share.

| Kind | Model |
| --- | --- |
| Endemic (malaria, worms and dysentery, livestock fevers, tuberculosis) | Extra yearly death chance = rate × age weight × niche fit × (farming weight × farming share + density weight × min(1, density / 2)). The forager life table already holds foragers' diseases, so these are the added costs of settled, farming, crowded life. Malaria and worms are present in their niche from the start; zoonoses emerge where people keep their hosts (sheep and goats, cattle, pigs, camels) and enough people live. They spread slowly to suitable neighbours. |
| Epidemic (plague, influenza, pox, measles) | An outbreak infects a = s (1 − e^(−R a)) of a cell (SIR final size, solved with the portable `exp`), with R = R0 × √density (0.3–1.5) and s the share still susceptible; it kills by age, leaves survivors immune for `immunity_years`, and spreads to each neighbour with chance 4 × a. A disease keeps circulating only in cells over its critical population (measles 250,000); elsewhere it burns out. Susceptibles return at 4% a year (births) plus waning. Crowd diseases therefore wait for cities (M3); god mode and `--at YEAR:outbreak:...` can bring one early, and a virgin population suffers the full wave. |

New origins (no cell within 1,500 km has it, and it never emerged that close
before) are logged as `DiseaseEmerged` events. At level 5, disease is ~0.5% of
deaths among foragers and ~10% where farming has spread by year 3,000.

**Technology** (`data/techniques.json`, `TechnologySystem`, culture phase).
Each cell's people know a set of techniques (a 64-bit mask). A technique can
be invented where its prerequisites (earlier techniques), crops or herds,
species and place (river, coast, dry land, hills, forest) allow: yearly
chance = rate × √(people / 10,000) × (0.5 + innovation) × the culture's
affinity. Neighbours teach (0.4% a year × (0.5 + openness) × (1 + contact));
populations under 500 forget 1% a year, and techniques whose requirements are
gone fade (5%). Effects: crop and herd yields (farmland), foraging and fishing
(forager capacity), harvest buffer (part of a bad harvest made good), famine
mortality, and contact (learning techniques and domesticates). At level 5
forager tools (bow, nets, canoes, pottery) spread early; granaries, irrigation,
milk and wool, polished axes and the plough follow farming over thousands of
years; copper and horse riding come late.

### Interventions

`Simulation::apply(Command)` changes a running world: a volcanic eruption, a
persistent climate forcing, a regional climate event lasting some years, a
plague, a wipe-out, settlers of a new group, or setting game abundance
(`interventions.hpp`). Each command takes effect immediately, uses only
deterministic arithmetic and its own random stream (`Stream::Intervention`,
keyed by its position in the log), and is appended to `SimState::commands`
with the tick it was applied at. Commands, active regional events and the
imposed forcing are saved and hashed like everything else, so a run with
interventions replays exactly: same seed + same commands at the same ticks =
same history. People added by settlers are counted in `Ledger::added`, so
conservation reads `population == initial + births - deaths + added`.

The viewer's god-mode panel and the CLI's `--at YEAR:ACTION:...` option both
go through this one call. CI checks a scripted run using every intervention
type across all platforms.

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
- Floating-point sums and averages depend on the order of their terms. When
  merging cohorts, `normalize_cohorts()` stable-sorts an index array, so
  equal keys merge in the order they were added, on every platform.

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
| M2 Neolithic (done) | Agriculture, herding, disease, culture and language drift, early tech | Culture and language map modes, language tree | Farming arises in several plausible regions |
| **M3 States and trade** (next) | Goods, markets, transport, polities, war, religion, notable agents, forced labor and the slave trade | Trade flows, inspectors, explain view, timeline | Polity sizes track Seshat ranges by era |
| M4 Modern era | Energy, carbon cycle, demographic transition, Earth importer, procedural generator | God mode, branch comparison, modding tools | Full runs to the near future on Earth and generated worlds |

M0 to M3 run on the generated test world, so the foundation is solid before any
real data is involved.
