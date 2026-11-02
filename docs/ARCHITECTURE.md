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
| `world.hpp` | Static world: terrain, baseline climate and seasons, biomes, rivers, soils, wild species ranges, deposits, trade links, starting forager groups, and the coarse climate grid with per-cell interpolation weights. `generate_test_world()` is the stand-in generator until M4. |
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
| `systems/polity.hpp` | Towns and cities (settlement system), and chiefdoms, states and empires (polity system). |
| `systems/economy.hpp` | Goods and markets: production, prices, trade along links, food brought in, trade routes. |
| `systems/war.hpp` | Armies, wars and their terms, tributaries and vassals, alliances, nomad raids. |
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

### Grid scale

The model is tuned at grid level 5 (cells of about 50,000 km², 225 km
apart), but the same history should unfold at any level. Rules that act cell
by cell therefore scale with the grid (`World::cell_scale()`, exactly
2^(5 − level), so every factor is exactly 1 at level 5 and results there do
not change):

| Rule | Scaled so that |
| --- | --- |
| Things passing to a neighbour (endemic and epidemic disease, preaching; crops, techniques and culture already scale with cell area) | they move the same km a year: chance ÷ scale |
| Wandering bands | people spread (diffuse) as fast: rate ÷ scale², up to 50% a year |
| Colonists | they leapfrog as far as at level 5: on finer grids to land 2^(level − 5) steps away (nearer only if none there has room); on coarser grids less often |
| Coasts and rivers (fishing, alluvium, floodplain farming, irrigation, river-loving species) | the strip along a coast and the corridor along a river are about 225 km wide at any level (`World::coast_share`, `World::river_share`) |
| Founding chances per cell (chiefdoms, nomad confederations, universal faiths, inventions, disease emergence) | as many per area: chance × scale² |
| Town thresholds (cities, writing and other techniques, temple and universal faiths) and population thresholds (disease critical community, forgetting) | they count a hinterland about a level-5 cell in area: the cells within 2^(level − 6) steps (`World::for_hinterland`); a city is named at the hinterland's centre |
| Annexation, conquest | the same area a year: as many attempts or pushes as cells in a level-5 cell |
| Nomad raids | they strike the target's hinterland |
| Thresholds in cells (civil war, schism, annexing a last province) | they mean the same area (`World::cells_for`) |

### People and environment (M1)

People live in cohorts: one per (cell, culture, language), each with an
age-sex pyramid in 5-year bins up to 100+. Counts are integers; every birth,
death and move goes through the ledger, so `population == initial + births -
deaths` holds exactly.

| System | Model |
| --- | --- |
| Climate | One-box energy balance, C dT/dt = F - λT, with C = 8 W·yr·m⁻²·K⁻¹ and λ = 1.3 W·m⁻²·K⁻¹. Forcing: four Milankovitch terms (small in the global mean), solar wander, and volcanic eruptions (about one large eruption per 60 years, -1 to -4 W/m², clearing in about a year). Regional AR(1) temperature and rainfall anomalies on a 162-cell coarse grid, interpolated to every cell. Bond events (below). |
| Environment | Forager capacity = forager density for this year's temperature and rainfall × area × (0.4 + 0.6 × game + aquatic bonus); a cell's capacity mixes it with farmland by its farming share (below). Game regrows logistically and is depleted when people exceed 60% of the climate's capacity. |
| Demography | Average hunter-gatherer life table (Siler fit, Gurven & Kaplan 2007: life expectancy 31 at birth, 38 more at 15) and a natural-fertility age pattern with total fertility 5.4 (Marlowe 2005), precomputed by `tools/vital_rates.py`. Fertility falls linearly to replacement as a cell reaches capacity; past capacity, famine raises mortality. Empty land grows about 0.7% a year. Replacement is worked out each year for the cell's own death rates (the life table, raised by towns, plus disease; famine excluded), so where disease kills more children women bear more (up to total fertility ~7, at most ~8 in all), as in historical high-mortality regimes: disease shortens lives (life expectancy about 30 for foragers, 26 for early farmers, 23 where smallpox is endemic) rather than leaving farmland empty, and only disease beyond what fertility can make up thins a people. |
| Migration | 0.5% of people a year explore; crowded cells send more (rising with crowding squared); a cell with no food empties. Each cohort's movers travel as one band to a neighbor picked by spare capacity over climbing cost. Young adults move most. Groups under 20 people or 10% of their cell join the cell's largest group (unless they farm and it does not: colonists keep apart). |
| Colonists (M3) | Farmers (or herders) at 75% or more of their land's capacity send up to 3% of their people a year (rising with crowding), in a party every five years (at most 15% of the cell, at least 15% of the people where they go), as one group of their largest farming people to the neighbouring land their crops and herds could fill best, where at least 30% of what it could feed is free; by sea too for people with boats, at half the appeal. Colonists keep their language and customs and bring their crops: the farming dispersals. Small parties of ordinary migrants no longer carry crops; they marry in. |
| Folk migrations (M3) | A people leaves in a body: 25% of a cell's largest people moves to the most open land within 700 km (spare capacity ÷ (1 + distance ÷ 300 km)) when its land was taken in war (30% chance that year, fleeing the conqueror's land), raided by nomads (10%), or struck by a lasting dearth: capacity smoothed over a few years below 85% of its fifty-year level while people exceed what it feeds (up to 30%, as the dearth deepens to 60%). One bad harvest is weathered; climate shifts set peoples moving as they begin. Arriving at least 30% as many as the largest people there, and no less warlike, they push some of them on (15% × newcomers ÷ locals, at most 40%: outnumbered locals flee, where the newcomers are few nearly all stay and live under them), and so on up to five links; a people driven out by dearth within 30 years of newcomers' arrival counts as pushed out by them. Movements of 1,000 or more are recorded. |
| Wandering peoples (M3) | A tribe or nomad confederation (a polity without writing; literate states stay put) of 5,000 or more moves as a whole when hard pressed: pressure is the largest of its war shock, famine, a lasting dearth, land hunger (people over what the land feeds) and a stronger neighbour (a realm seated within 600 km with over five times its people). At pressure 0.3 or more it sets out with chance 3% × pressure × how readily its people move (×0.3 for settled farmers to ×1.15 for nomads) a year; peoples more mobile than 0.4 also roam unpressed (up to 0.6% a year). It aims for the most open land (pasture for herders) up to 600 + 1,400 × mobility km away, not held by anyone over half its size (appeal ÷ (1 + distance ÷ 1,000 km)). It goes step by step, keeping as much land around it as it held: 60% of its rulers' people set out, and each year it advances one cell toward the goal with chance (30 + 120 × mobility) km ÷ the cell spacing (so 30–150 km a year), 90% going on at each step and the rest settling along the way. The trek ends at the goal, or where a stronger people blocks the way. Its name, rulers, language and faith go with it; its wars end in its flight. The land left behind is free; locals where it lands are pushed on as in folk migrations and their rulers shaken; the rest stay under the newcomers. At level 5 about 135 tribes (~360 km, from one step to several years on the road) and 30 confederations (~700 km) move in 9,000 years: Celtic- and Germanic-style tribal movements and steppe peoples shifting west. |
| History | One row per year: population, cumulative births, deaths and migrants, inhabited cells, global temperature anomaly, total capacity. |

Typical behavior: a world starting at half capacity grows for two or three
centuries, then holds near 90% of capacity, dipping after eruptions and in cold
spells.

### World geography (M2)

`generate_test_world()` builds, in order:

| Step | Model |
| --- | --- |
| Terrain | Fractal noise continents; sea level set so the requested share of cells is ocean. On Earth worlds (`WorldKind::Earth`, `--earth`), Earth's relief instead (below). |
| Temperature | Mean annual T = 27 − 30z² − 17z⁴ − 6.5 × altitude in km (z = sine of latitude). |
| Rainfall | A zonal profile (wet equator, dry subtropics near 30°, wet westerlies, dry poles) scaled by moisture from upwind: each cell traces back along its prevailing wind (trades and polar easterlies from the east, westerlies from the west between 30° and 60°) and dries with the distance of land crossed. A ridge more than 800 m above the cell upwind casts a rain shadow; otherwise height lifts rain on the windward side. |
| Seasons | Temperature range grows with latitude and continentality (land upwind); rain season is signed (−1 winter rain, Mediterranean; +1 summer rain, monsoon). Monthly values come from these, peaking in July in the north and January in the south. |
| Biomes | Köppen-style classification from the twelve months: ice, tundra, boreal, temperate, warm temperate, Mediterranean, grassland, shrubland, desert, savanna, rainforest. Growing months = months with T ≥ 5 °C and rain ≥ 35% of potential evaporation. |
| Rivers | Runoff = max(0, rain − 0.8 × potential evaporation). Priority-flood drainage (lakes and pits are filled) gives every land cell one downstream cell, and discharge and basin area accumulate downstream. A cell is a river when its basin is at least 150,000 km² and its flow at least 300 m³/s, so the network looks alike at every grid level. |
| Soils | Base fertility by biome, lower on steep relief, plus alluvium on large rivers. |
| Aquatic foods | Coasts and rivers add up to +80% foraging capacity (fish and shellfish, not depleted by hunting). |
| Wild species | Each species in `data/species.json` has trapezoid niches (coldest and warmest month, rainfall, rain season, elevation) and a river affinity. Suitability is their product. Homelands are picked from suitable cells (the best of 24 tries by spread, far apart from each other) and ranges spread from them up to `range_km`. Stored as a 32-bit presence mask and an abundance byte per cell. |
| Deposits | Each good in `data/goods.json` that comes from a deposit (salt, flint and obsidian, timber, fish, copper and tin ore, shell, gold) lies in each suitable cell (hills, mountains, coast, dry land, forest...; never on ice sheets) with its chance per 50,000 km², so the same share of land at every grid level, at a random richness stored as a byte. |
| Trade links | Every pair of neighbouring land cells (marked as a river link where one drains into the other along a river), plus sea links between coasts whose open water connects within 500 km. Stored once each with length and height difference, and listed per cell (CSR). |

Content packs are plain JSON with `//` comments. The reader parses numbers
exactly (decimal digits up to 15 and powers of ten up to 22, a single correctly
rounded operation) and rejects anything it cannot read exactly, so content
never depends on the platform's `strtod`. The pack's hash goes into the world
hash: a snapshot made with different content is refused.

### Earth (M4)

An Earth world (`WorldParams::kind = WorldKind::Earth`; `--earth` on the
command line, "Earth" in the viewer) has Earth's land, sea and mountains in
place of the fractal planet. Everything else is built from that terrain as on
a generated world: climate, biomes, rivers, soils, and, from the seed, wild
species, deposits and the starting peoples. So the map is always Earth's,
while each seed is a different "alternate biology" and history on it.

The relief comes from ETOPO1 (NOAA, public domain), as resampled to 10 arc
minutes by ETOPO10 (github.com/g2e/etopo10, MIT), ice-surface version, so
Antarctica and Greenland are land. `tools/bake_earth.cpp` averages it onto
the level-7 grid (a cell is land if at least half its area is, at the mean
height of its land or sea) into `data/earth/elevation_l7.bin`, one int16 per
cell, compiled into the library. Coarser grids gather the level-7 cells the
same way; finer grids take their nearest level-7 cell. The bake is offline
and committed, so the simulation reads only integers, and 29.2% of cells are
land at every level, as on Earth. The coastline is today's (sea level 60 m
lower at 10,000 BCE, Doggerland, Beringia and Sundaland, is not modelled), and
the world hash includes the kind (generated worlds hash as before).

On Earth the climate model gets temperatures right (tropics, cold Tibet,
Andes and Siberia) but its rainfall, built for generated worlds, misses some
of Earth's: the eastern United States and eastern China come out as
grassland and desert (no summer monsoon onshore), Arabia and the Horn of
Africa too wet (moisture carried by the trades from the nearby sea, with no
subtropical subsidence), and the Congo a little dry.

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
| Shift | In a cell with several languages, 3% a year times the gap in pull move to the language with the most pull, keeping their culture; cultures assimilate the same way at 1.5%, keeping their language. Pull is people × (1 + 3 × farming share) × (1 + 1.5 × the rulers' control, for the rulers' language or culture): farmers absorb foragers, and conquerors' tongues spread through what they hold well. Bands under 20 people or 10% of a cell are absorbed at once (M1). |
| Splits | Every 20 years, a language whose farthest dialect is more than 0.5 from its average splits: everyone closer to that dialect than to the average forms a daughter language (both sides need 150 people). The daughter starts as a copy of its parent plus 300 years of change. Cultures split the same way on their traits (threshold 0.3). |
| Language change | Per language per year: a regular sound law from `data/phonology.json` about every 350 years, applied to every word at once (so daughters show systematic correspondences); word replacement weighted by meaning stability, keeping ~85% of the 75-word core vocabulary per thousand years (glottochronology: 81–86%); rare grammar changes. Words carry cognate ids, so lexical similarity is the share of cognates. |
| Names | A language is named by its word for "people" (or its land, river, mountains, or a new word in it) and is unique. A culture split gets a name coined in its main language. |
| Extinction | A language or culture with no one left is marked extinct and logged. |
| Mobility | The mobility trait (settled 0 to nomadic 1) adapts fast (1% of the gap a year) to how people eat: fishing, crowding and crop farming settle them, herding sets them moving again (target 0.75 − 0.6 × farming + 1.0 × herding − ...). It settles near 0.07 for crop farmers, 0.36 for foragers and 0.5 for herders (higher on the steppe). Mobile peoples wander more (up to ×1.17), take to the road in folk migrations more readily (×0.75 for the most settled to ×1.08 for the most nomadic), are held more loosely by rulers, and band into nomad confederations more readily; settled ones take up and domesticate crops faster. |

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
| Knowledge | Per cell, the domesticates its people know. Learned from any neighbour that knows one (0.05% a year × (0.5 + openness)), and carried by colonists, folk migrations and farming migrants who come in numbers (at least 10% of the people there), if the species can be raised there; lost when a cell empties. Farming spreads about 1 km a year, mostly with the farmers themselves. |
| Crops and herds | A species can be raised where its wild niche, widened by 4 °C, 30% rainfall and 500 m, still fits (`World::cultivated_suitability`). |
| Farmland | People per km² = 0.6 × best crop (suitability × food value) × soil (0.35 + 0.65 fertility) × floodplain (up to +50% on big rivers) × season (growing months) × rain (away from rivers, (rainfall − 200 mm) ÷ 400 mm, from 0.05 to 1: rain-fed crops fail often in dry land), plus half of 0.15 × best herd × pasture (grassland 1.6, shrubland 1.3, savanna 1.2, Mediterranean 1.0, tundra 0.8, forests 0.7, desert 0.6, rainforest 0.4); herds alone feed pastoralists. Where herds give most of the farm food (dry grassland and shrubland, the steppe) people are pastoralists. A regional average for early farming; later techniques raise it. Each year × harvest (rain 40–115% of normal, −12% per °C off normal). |
| Farming share | Per cohort. Grows by 1% a year × (0.3 + innovation) × (1.2 − mobility) × (strain − 0.6) × (1 − share), where strain = people ÷ forager capacity: farming spreads when foraging alone stops being enough, and raises the population so it is needed more (the Neolithic ratchet). Falls when farmland yields less than foraging; drops to zero where nothing can be farmed. |
| Capacity | forager capacity × (1 − share) + farmland × share, with the cell's population-weighted share. Hunting pressure on game comes from foragers only. |
| Farmers | 25% more births on empty land (room to grow, not an excess at capacity); 60% less migration. Culture: farming pushes toward settled, plant-reliant, storing, ranked, clan-based and less sharing ways. |

At level 5 the first crops appear around year 900 (often rice or yams in the
wet tropics), independent origins continue for thousands of years (squash,
lentils, sorghum, maize, sheep and goats, cattle, millets, potatoes...),
farmers are a few percent of people at year 1,500, over half by 3,000 and
three-quarters by 4,500, and population nearly doubles by year 3,000. Farming
peoples carry their languages with them: after 9,000 years about a third of
the land speaks a different family than at the start, and the most successful
families hold three to seven times their first land. `histsim farming` lists
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

### Towns, cities and early states (M3)

| Process | Model |
| --- | --- |
| Towns | Each cell's share of people in towns moves 1% a year toward 0.08 × (people fed by farmland per km² − 0.4), up to 35%, plus 4 points at a capital. A cell whose towns hold 5,000 people has a city, named in its people's language; below a third of that it is abandoned. Towns raise mortality (× 1 + 0.3 × town share) and disease contact (density × 1 + 20 × town share); crowd diseases (pox, measles) emerge only where towns hold 20,000–40,000 people. |
| Formation | Every 5 years, a stateless cell with farming ≥ 50%, at least 0.4 people per km² and hierarchy ≥ 0.3 founds a polity with chance 1% × hierarchy × (0.5 + circumscription) × density factor, where circumscription is the share of neighbours that are sea or much poorer land (Carneiro). |
| Reach | Control of a cell = 1 / (1 + (distance from the capital / reach)²); reach = 85 km × (1 + administration) × (1 + 0.3 contact). Writing (+0.5), bureaucracy (+0.8), roads (+0.8), provincial government (+2) and bronze (+0.1) add administration, so realms grow as these come in turn: a chiefdom holds a cell or a few, a state with writing a ring around its capital, an empire with provinces and roads millions of km². |
| Expansion | Each year a polity tries the stateless neighbouring cell it would control best (at least 30%), with chance 3% × control × (0.5 + warfare) × strength ratio. Land changes hands between polities only in war (below). |
| Stability | Each polity's stability (0–1) moves toward a target each year (10% of the gap when rising, 30% when falling): 0.45 + 0.25 × hierarchy + 0.15 × ritual (people at the capital) + 0.1 with writing + 0.1 with bureaucracy + 0.1 × the share of people holding the rulers' organized faith (or a sect of it), minus weighted pressures: famine (× 1.0; three times the share of people beyond what the land and food imports feed), war (× 0.8; the toll of land lost, war dead, lost wars and raids, fading 20% a year), overreach (× 0.5; 1 − the people-weighted mean control, each cell's control less by half its people's mobility), epidemic (× 0.6; disease deaths beyond 0.5% a year, × 40), division (× 0.3; share of people of another cultural lineage than the rulers, plus half the share holding another organized faith) and poverty (× 0.15; little trade per person). |
| Rulers' people | Each year the capital's speakers of the polity's language send 0.1% × control of each province's people (held at least 30%) to it: officials, garrisons and merchants who carry the rulers' language there. A capital moves to a town of the realm (for nomads, any cell) with twice as many townspeople (people) as the old seat. |
| Breakdown | Provinces under 20% control secede (1% a year) and polities of 6 or more cells split in civil war (0.4% × (1 − mean control)), the far half breaking away and fighting a war of independence; both are multiplied by 2 × (1 − stability). Below 30% stability a polity breaks up with chance 0.3 × ((0.3 − stability) ÷ 0.3)² a year, its heaviest pressure recorded as the cause. A chiefdom dissolves into its villages. A state leaves a core (cells it controls at least half) under its old capital, successor polities seated at the most populous dense, farming, hierarchical towns at least 250 km apart, each taking the cells within 250 km, and unruled land beyond. Until rulers are simulated (part 6), succession disputes break up chiefdoms at 0.05% and states at 0.02% a year whatever their state. A polity that loses its last cell was conquered (if the toll of war on it was heavy) or dwindled away. |
| Rulers | Each polity has a ruler of a house (`RulerSystem`, before the polity system each year). A new realm's first ruler founds a house; a ruler dies at 0.8 × the forager death rate for his or her age, and an heir of the house succeeds, a child (under 16, with a regent) if the ruler died young. A succession is disputed with chance 15% × (1.5 − stability), doubled for a child heir: of the disputed, 6.5% of chiefdoms and 2.6% of states break up (the old "succession crisis"), and the rest pass to a usurper who founds a new house. Rulers' ability (0–1) moves the stability target by ± 0.1 (−0.1 under a regent), and their martial bent (half the people's warfare, half chance) scales the chance of declaring war by 0.5 + martial. Heirs often take a forebear's name, with a regnal number. Reigns last about 27 years; the longest houses about a thousand years. |
| Institutions | A polity may found (1% a year once it qualifies) a temple priesthood (rulers of an organized faith, or ritual ≥ 0.6 with a town of 5,000), a council of notables (3+ level-5 cells' worth of land, hierarchy ≥ 0.4), a law code (writing) and a standing army (writing, 300,000 people, bronze or riding). Temples add 0.05 to the stability target and make disputed successions rarer (× 0.8); councils too (× 0.6); law codes add 0.05 and cut division (× 0.7); armies add a quarter to war strength and cut overreach (× 0.7). Below 30% stability each may be lost (1% a year). A broken state's core and successors keep its institutions, but not its army. |
| Culture | Being ruled pushes hierarchy, warfare and ritual up. |

Writing needs granaries, a calendar and a town of 20,000 people to be
invented (not to be kept); bureaucracy and roads follow it (roads need no
wheel: the Inca built them without), and provincial government needs both. At level 5 a polity lasts about 120 years at the
median (a quarter outlast 350). Of those that end, over half are conquered,
about one in six collapses in famine, about one in ten in epidemics or from
overreach, and about one in six breaks up in a succession dispute; break-ups
cluster in Bond events and epidemics. Chiefdoms appear a few centuries after
farming, thousands rise and fall, and the first states (with writing) appear
around 3,000–6,500 years after the first crops (in some worlds, without
large enough towns, never); the largest early states hold 0.3–0.8 million
km², the range of archaic states in Seshat, and empires reach 2–3 million
km² some 2,500 years after the first state. `histsim polities` prints the
timeline and the largest polities; `histsim gate` checks all this against
history (the M3 gate, below).

**The M3 gate** (`histsim gate --seeds 5`, about half an hour) runs several
worlds for 11,000 years and checks three things. The first state (a polity
with writing) arises within 1,000 years of 6,300 years after the first crop
(farming in the Fertile Crescent about 9500 BCE, Uruk about 3200 BCE), at the
median. Before 5,000 years of farming no polity exceeds 0.3 million km². And
the largest polity, era by era counted from each world's first state, is
within a factor of 3 of the largest empire of the same era counted from 3200
BCE (Taagepera's figures as listed in Wikipedia's "List of largest empires":
0.1 million km² in 3000 BCE, 0.65 under Akkad, 1.0 in 1450 BCE, 1.4 under
Assyria, 5.5 under Persia in 500 BCE), sizes below five cells (0.25 million
km² at level 5) being compared as five cells, the finest the grid shows.
Timing the eras from the first state keeps the growth of states apart from
the date of their rise, which varies far more from world to world. With seeds
1–5 at level 5 all nine eras pass: the median world has states of 0.4 million
km² 200 years after the first, 0.8 at Akkad's time, 1.6 by 1450 BCE and 2.1 by
500 BCE (Persia's 5.5 is at the edge of the factor of 3); of the five worlds
one never invents writing and one never builds roads.

### Goods, markets and trade (M3)

`data/goods.json` lists the goods: food, dried fish, livestock, salt, flint and
obsidian, timber, pottery, cloth, copper and tin ore, copper, bronze, shell
ornaments and gold, each with a weight (carrying cost relative to grain),
spoilage, base price, source and demand. `EconomySystem` runs in the markets
phase, after farming and towns and before demography.

| Process | Model |
| --- | --- |
| Production | Deposits yield `per_person` × people × richness; livestock `per_person` × farmers where they keep herd animals; crafts (`per_person` × people + `per_townsperson` × townspeople) where the people know the technique, limited by the inputs in stock (copper uses 4 ore; bronze 0.9 copper and 0.5 tin). All lean toward goods that sell: × last year's price ÷ base, from 0.5 to 2. |
| Demand | `per_person`, `per_farmer`, `per_townsperson` and `per_elite`, where elites are 2% × (0.5 + hierarchy) of people, doubled under a polity. Crafts also want their inputs, so ore flows to smelters. |
| Prices | Each inhabited cell is a market: price = base × √(wanted ÷ stock), the ratio kept within 1/25–25 (1/5 to 5 times base). A market that neither makes nor wants a good prices it at base. |
| Carrying | Per unit of weight per 100 km: overland 0.3 × (1 + 2 × height difference in km), down a river link 0.1, by sea 0.15 (only for people with boats); horses (+0.3), carts (+0.4) and roads (+0.6) divide the overland cost by 1 + their sum, canoes (+0.25) and sails (+1.25) the water cost. |
| Trade | Each year every market sends half of what it can spare (stock beyond what it wants) to neighbouring markets where the good sells for more than here plus carrying, split by that gain. All markets decide from the same start-of-trade stocks, so the order of cells does not matter. A market that only passes goods on prices them at base, between the glutted and the starved, so goods cross a continent a link a year. |
| Food | Food price = √(people × (1 + 2 × town share) ÷ capacity). Food goes only to neighbours, 0.5 × price gap ÷ price sum × the smaller capacity when the gap exceeds the carrying, at most 25% of the sender's capacity; it adds to the receiver's capacity this year (and is taken from the sender's), and counts toward the food that lets towns grow. |
| Use | Markets use up their demand, stocks spoil, and stocks above five years of use, production and imports are not kept. A good is on the market when at least 0.01 + 0.00001 × people of it is there. |
| Materials | Copper smelting needs copper ore on the market, bronze copper and tin; cut off, the craft fades (5% a year). |
| Routes | Each link's carried value is smoothed (10% a year). Trade routes are connected runs of at least two links among the busiest 5% (and at least 200 a year), named after their largest city (or busiest market's people) as a road, river trade or sea lanes by their main mode. |

At level 5 salt, stone, pottery and cloth reach most markets, ores and gold a
fifth or less, and the value carried over links each year comes to about 7%
of the food supply. Copper smelting begins a millennium or so after farming near ore, and
bronze follows where copper and traded tin meet in a town. `histsim trade`
prints the timeline, the goods and the busiest routes; the trade and
resources map modes and the trade-route overlay show them.

### War, diplomacy and nomads (M3)

`WarSystem` runs right after the polity system each year.

| Process | Model |
| --- | --- |
| Armies | A polity's levy is 4% × (0.5 + warfare) of the people in each cell it rules, times its control there; nomads raise four times as many (every herder rides). Strength = levy × (1 + military), where bows (+0.15), horses (+0.6), chariots and carts (+0.2), copper (+0.1) and bronze (+0.4) add military. Vassals add half their strength to their overlord's. |
| Declaring war | A polity that is no one's subject, not at war, and not under truce may attack a neighbouring polity when its strength is at least 1.2 times the defender's plus half its overlord's and allies': chance 8% × (0.5 + warfare) × min(2, ratio − 1) a year. Nomads attacking farmers fight for tribute; others for conquest. The defender's overlord, allies and vassals join it; the attacker's vassals join the attacker. |
| Fighting | Each year each side may take the enemy cell along the front it controls best, if it would control it at least 15% (distance counted less by the cell spacing − 112.5 km, so a capital's neighbours are always in reach): armies do not outrun their rulers' grip (chance 30% × min(2, ratio − 1), up to three tries for a side three times stronger). A taken cell is sacked (3% of its people killed), 10% flee to their own side, and 3% of the people of the cell it was taken from move in as settlers and garrisons (a conquest dynasty brings 10% of its homeland). Battles kill men of fighting age in the front cells: 10% of 30% of the smaller army, split by the other side's share of strength. |
| Peace | A war ends when a leader's capital falls, when a leader is gone, or by exhaustion (5% × (1 + years at war ÷ 10) a year). The winner keeps what it took; a defender whose capital fell becomes a vassal (or is annexed if one cell is left), and one beaten in a war for tribute a tributary. A 20-year truce follows. |
| Subjects | Tributaries and vassals send their overlord 10% of their stocks of goods each year; vassals fight for it. A subject at least 0.7 times as strong as its overlord rebels (3% a year), starting a war of independence; a vassal whose capital its overlord controls at least half is absorbed (1% a year). |
| Alliances | A free polity with a neighbour 1.5 times stronger seeks a defensive ally among its own and the threat's neighbours (strongest first, related cultures counting double): chance 5% a year, doubled for related cultures, up to three allies each. Alliances lapse at 1% a year, or when the allies fight each other. |
| Nomads | Pastoralists are cells whose farm food comes mostly from herds; they move as freely as foragers. Herders without mounts form ordinary tribal chiefdoms (needing only a quarter of farmers' density). On open pasture (grassland, shrubland, savanna), or in desert and dry scrub where camels are kept, where herders ride (horses or camels) and warfare is at least 0.4, confederations form, and herder chiefdoms that take to riding there become confederations: 1% × warfare × (0.5 + 0.67 × mobility) per five-year check, times 1 + the wealth of the settled farmers within 600 km ÷ 20,000 (up to 5), but only with a state (a polity with writing) within 600 km: the "shadow empires" that rose opposite rich agrarian states (Barfield 1989). Before then mounted herders form tribal chiefdoms. Their rule reaches twice as far. Each year a confederation may raid (30%) the richest settled farmers within 600 km of its land (wealth = people in towns + a twentieth of the rest), succeeding with chance attack ÷ (attack + defence), its strength weighed by its control at that distance: it carries off 30% of the cell's goods and kills 1% of its people, and the victim's ruler, if the nomads are at least half as strong, may agree to pay tribute (20%); tributaries are not raided. Nomads who take the capital of a state, or of a polity seated in a city, in a war for tribute while 1.5 times as strong found a conquest dynasty: they annex it, rule from its capital, and settle down after 60 years. |

Over 9,000 years at level 5, about 1,200 wars are fought, most between
neighbouring chiefdoms and lasting a few years. From about year 4,500 five to
ten nomad confederations (once there are states to shadow) hold the dry grasslands at a time, raid one or two
settled cells a year, take tribute from several neighbours (the largest realm
of a run is often a confederation with its tributaries), and found a dozen or
so conquest dynasties in 9,000 years. A few alliances and subjects stand at
any time, and realms reach 0.5–1.5 million km² by war. Deaths in war run to
several hundred a year: violence between bands and tribes is
already in the forager life table, so this counts only war between polities.
`histsim wars` prints the timeline, the deadliest wars and the largest realms
with their subjects and allies (nomads starred); the realms map shows nomad
confederations in browns, and the war map marks cells raided in the last 20
years.

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

### Religion (M3)

`ReligionSystem` runs each year after technology. Every inhabited cell holds
one faith (`SimState::faith_of`); kinds of religion are content
(data/religions.json).

| Process | Model |
| --- | --- |
| Folk beliefs | Each founding culture lineage has its own folk religion, made the first time its people are seen. It belongs to that people: it converts no one, and where another people's culture has become the main one, the land takes up theirs (2% a year). |
| Temple cults | At the capital of a living polity whose people still hold folk beliefs, with at least 5,000 townspeople and ritual of at least 0.5: chance 1% per five-year check. The cult of the city's gods grows out of its people's beliefs (its parent) and is the rulers' faith. |
| Universal faiths | In a town of at least 20,000 people that knows writing and holds no missionary faith already: chance 0.3% per five-year check. |
| Conversion | Each year each land weighs its own faith (people × appeal × 2) against each rival: neighbouring lands holding a non-folk faith (their people × appeal × (0.05 + missionary zeal)), trade routes carrying it (route value × 5 × appeal × zeal), and the rulers' faith (3 × control × people × appeal). Preachers and traders win over farmers and townspeople; foragers are only a fifth as open to them (rulers' pull is not scaled). The strongest rival wins the land with chance 3% × (its pull − own) ÷ (its pull + own). Appeal: folk 1, temple 1.5, universal 3; zeal: universal 0.5. All lands update from last year's faiths at once. |
| Rulers | A polity holds the faith of its capital, so a capital's conversion converts the state (an event), and its provinces follow by the rulers' pull. A people sharing the rulers' organized faith steadies a polity; one holding another organized faith divides it (see Stability). |
| Schisms | Every 20 years, a polity whose organized faith began over 1,000 km from its capital and which holds it in at least four lands forms its own sect (5%): "<faith> (<polity> rite)", a child of it. |
| Doctrine | Followers' culture traits close 0.2% of the gap to their faith's doctrine a year: temple cults toward ritual and hierarchy, universal faiths toward ritual, sharing, openness and less warfare. |

Over 9,000 years at level 5, temple cults appear from about year 2,500 in
the first city-states, and spread only as their cities conquer. Universal
faiths arise from about year 6,000 in the great literate cities and spread
along trade routes and through realms whose rulers convert, so organized
faiths hold about a fifth of the world by year 6,000 and half by year 9,000,
with a few universal faiths in the lead and sects where they are ruled far
from home. `histsim religions` prints the timeline and the largest faiths;
the religion map colors organized faiths by family (sects in shades of their
parent) and folk beliefs in muted tints.

### Forced labour and the slave trade (M3)

Each group of people holds a share in bondage (`Cohort::bonded`); groups
split off carry the same share, merged ones average it by headcount, and the
enslaved keep their own language and culture. `BondageSystem` runs each year
after the war system.

| Process | Model |
| --- | --- |
| Captives | When land is taken in war, 15% of its people are carried off to the conqueror's capital; nomad raids carry off 5% of the lands they strike to the raiders' capital. Fewer are taken of the captors' own lineage (×0.3), and few of their own organized faith (×0.1): a shared universal faith protects its own. |
| Demand | A land wants a share of its people in bondage: 0.4 × its town share + 0.05 where settled people work metal or salt deposits + 0.08 × farming × hierarchy, all × (1.5 − sharing), at most 35%. |
| Trade | Each year a land holding more than it wants sells half its surplus (lots of 50 or more) to the land within 1,500 km, on the market network, that wants them most (want ÷ (1 + distance ÷ 400 km)). Routes keep their totals and when they were used. |
| Freedom and its lack | 0.5% of the enslaved go free each year, 1.5% where they have taken up the main culture of the land. They die younger (mortality × 1.3) and have fewer children (× 0.6). A polity's division pressure counts half the share of its people in bondage. |

At level 5 the enslaved grow from the first wars between chiefdoms to about
0.4% of the world (3–11% of the people of the largest towns) by year 9,000;
about a million captives are taken and half a million sold on per 500 years
late in a run, along a few hundred routes of up to 1,500 km. `histsim slavery`
prints the timeline, where the enslaved live and who they are, and the
busiest routes; the Enslavement map (People) shades their share and draws the
routes used in the last 50 years.

## Roadmap

| Milestone | Simulation | Front end | Gate |
| --- | --- | --- | --- |
| M0 Foundations (done) | Grid, test-world generator, scheduler, RNG, snapshots, headless runner | Sim API, map viewer | Identical replays on Windows, Linux and macOS |
| M1 Living planet (done) | Climate and environment systems, cohort demography, migration, foragers | Globe, climate and population map modes, pyramid, charts | Forager populations settle near carrying capacity (`m1_gate_populations_settle_near_capacity`) |
| M2 Neolithic (done) | Agriculture, herding, disease, culture and language drift, early tech | Culture and language map modes, language tree | Farming arises in several plausible regions |
| M3 States and trade (done) | Goods, markets, transport, polities, war, religion, notable agents, forced labor and the slave trade | Trade flows, inspectors, explain view, timeline | Polity sizes track Seshat ranges by era (`histsim gate`) |
| M4 Modern era | Energy, carbon cycle, demographic transition, Earth importer, procedural generator | God mode, branch comparison, modding tools | Full runs to the near future on Earth and generated worlds |

M0 to M3 run on the generated test world, so the foundation is solid before any
real data is involved. Earth terrain (part of M4) is an option beside it.
