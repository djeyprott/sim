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
| `systems/economy.hpp` | Goods and markets: production, prices, trade along links, food brought in, food stores, trade routes. |
| `systems/war.hpp` | Armies, wars and their terms, tributaries and vassals, alliances, nomad raids. |
| `systems/diplomacy.hpp` | Relations between realms: opinion, trust, memory of wars, land claims, causes for war; how subjects weigh their overlords; war score and peace terms. |
| `systems/farming.hpp` | Domestication, knowledge of domesticates, farming share, farmland capacity and harvests. |
| `systems/culture.hpp` | Culture and dialect drift, contact, adaptation, language shift, assimilation, splits, extinctions; founding new peoples. |
| `view/` (separate library) | Map modes, color scales, legends, pixel-to-cell rasterizing and PNG output. Display only: it reads state and never writes it, so the determinism rules do not apply to it. |
| `godot/extension/` | GDExtension class `HistSimWorld`: the Sim API plus `view/`, exposed to GDScript. |

### Map modes

A map mode (view/src/map_modes.cpp) reads one value per cell and maps it to a
color, with a legend. Magnitudes use one hue from light to dark; signed
quantities (temperature) use a blue-red diverging scale with a neutral
midpoint; ocean and empty land are neutral grays so they never read as data.
Every mode added there shows up in the viewer and `histsim map` automatically. Names are written over the map by mode (`map_labels`): the language,
language family, culture and religion modes name each connected stretch of
land where a group is the main one (150,000 km² or more; a group in two
separate regions is named in each), the realms mode names each realm (a power
and its subjects); every other mode names states, empires and large realms.

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
| Environment | Forager capacity = forager density for this year's temperature and rainfall (0.2 people/km² × warmth (−8 °C to 22 °C) × wetness (100 to 1,200 mm)) × the biome's forage quality (savanna and temperate grassland 1.4, the big-game lands; Mediterranean and tundra 1.2; temperate woods 1.1; shrubland and desert 1.0; boreal forest 0.8; rainforest 0.45, food scattered in the canopy with little game, Bailey et al. 1989) × area × ((0.4 + 0.6 × game) × (1 + land techniques) + aquatic bonus × (1 + water techniques)); a cell's capacity mixes it with farmland by its farming share (below). Game regrows logistically and is depleted when people exceed 60% of the climate's capacity. |
| Demography | Average hunter-gatherer life table (Siler fit, Gurven & Kaplan 2007: life expectancy 31 at birth, 38 more at 15) and a natural-fertility age pattern with total fertility 5.4 (Marlowe 2005), precomputed by `tools/vital_rates.py`. Fertility falls linearly to replacement as a cell reaches capacity; past capacity, famine raises mortality. Empty land grows about 0.7% a year. Replacement is worked out each year for the cell's own death rates (the life table, raised by towns, plus disease; famine excluded), so where disease kills more children women bear more (up to total fertility ~7, at most ~8 in all), as in historical high-mortality regimes: disease shortens lives (life expectancy about 30 for foragers, 26 for early farmers, 23 where smallpox is endemic) rather than leaving farmland empty, and only disease beyond what fertility can make up thins a people. |
| Migration | 0.5% of people a year explore; crowded cells send more (rising with crowding squared); a cell with no food empties. Each cohort's movers travel as one band to a neighbor picked by spare capacity over climbing cost. Young adults move most. Groups under 20 people or 10% of their cell join the cell's largest group (unless they farm and it does not: colonists keep apart). |
| Colonists (M3) | Farmers (or herders) at 75% or more of their land's capacity send up to 3% of their people a year (rising with crowding), in a party every five years (at most 15% of the cell, at least 15% of the people where they go), as one group of their largest farming people to the neighbouring land their crops and herds could fill best, where at least 30% of what it could feed is free; by sea too for people with boats, at half the appeal. Colonists keep their language and customs and bring their crops: the farming dispersals. Small parties of ordinary migrants no longer carry crops; they marry in. |
| Folk migrations (M3) | A people leaves in a body: 25% × (1 + mobility) of a cell's largest people (at most 60%: herders go whole clans at a time) moves to the most open land within 700 km (spare capacity ÷ (1 + distance ÷ 300 km)) when its land was taken in war (30% chance that year, fleeing the conqueror's land), fought over (10% × mobility²: only mobile peoples leave ahead of the fighting), raided by nomads (10%), or struck by a lasting dearth: capacity smoothed over a few years below 85% of its fifty-year level while people exceed what it feeds (up to 30%, as the dearth deepens to 60%). One bad harvest is weathered; climate shifts set peoples moving as they begin. Arriving at least 30% as many as the largest people there, and no less warlike, they push some of them on (15% × newcomers ÷ locals, at most 40%: outnumbered locals flee, where the newcomers are few nearly all stay and live under them), and so on up to five links; a people driven out by dearth within 30 years of newcomers' arrival counts as pushed out by them. Movements of 1,000 or more are recorded. |
| Wandering peoples (M3) | A tribe or nomad confederation (a polity without writing; literate states stay put) of 5,000 or more moves as a whole when hard pressed: pressure is the largest of its war shock, famine, a lasting dearth, land hunger (people over what the land feeds) and a stronger neighbour (a realm seated within 600 km with over five times its people). At pressure 0.3 or more it sets out with chance 3% × pressure × how readily its people move (×0.3 for settled farmers to ×1.15 for nomads) a year; peoples more mobile than 0.4 also roam unpressed (up to 0.6% a year). It aims for the most open land (pasture for herders) up to 600 + 1,400 × mobility km away, not held by anyone over half its size (appeal ÷ (1 + distance ÷ 1,000 km)). It goes step by step, keeping as much land around it as it held: 60% + 40% × 0.7 × mobility of its rulers' people set out (nearly all of a herding people), with the realm's other peoples at least 0.6 mobile following (60% × mobility of them: herders under the horde), and each year it advances one cell toward the goal with chance (30 + 120 × mobility) km ÷ the cell spacing (so 30–150 km a year), 90% going on at each step and the rest settling along the way; no more go into a cell than fill it to (1.1 + mobility) times what it feeds (the rest stay behind: a horde crowds in on the locals and drives them off, where farmers must find space), as real migrations shed people on the way. Each step kills 3% of those taking it (× 3 into desert, × 2 into dry scrub or tundra, × 2 into land held by another people); `SimState::march_deaths` counts them. On the march and for 20 years after it settles, a wandering people holds together under its war leaders (+0.15 to its stability target) and lives off the herds and stores it brings and the harvests it takes (famine pressure × 0.3). The trek ends at the goal, or where a stronger people blocks the way. Its name, rulers, language and faith go with it; its wars end in its flight. The land left behind is free; locals where it lands are pushed on as in folk migrations and their rulers shaken; the rest stay under the newcomers. When a nomad confederation takes land in war, its herders follow: the next year 30% × mobility of the largest group of its own people in the cell it struck from move into it (filling it to at most (1.1 + mobility) times what it feeds), recorded as a migration following their horde. At level 5 about 135 tribes (~360 km, from one step to several years on the road) and 30 confederations (~700 km) move in 9,000 years: Celtic- and Germanic-style tribal movements and steppe peoples shifting west. |
| History | One row per year: population, cumulative births, deaths and migrants, inhabited cells, global temperature anomaly, total capacity. |

Typical behavior: a world starting at half capacity grows for two or three
centuries, then holds near 90% of capacity, dipping after eruptions and in cold
spells.

### World geography (M2)

`generate_test_world()` builds, in order:

| Step | Model |
| --- | --- |
| Terrain | Plate tectonics (below); sea level set so the requested share of cells is ocean. On Earth worlds (`WorldKind::Earth`, `--earth`), Earth's relief instead (below). |
| Temperature | Mean annual T = 27 − 30z² − 17z⁴ − 6.5 × altitude in km (z = sine of latitude), plus up to 6 °C on mid-latitude lands with the sea upwind (6 × (1 − continentality), strongest near 46°: winter westerlies off a warm ocean, as in western Europe). From coldest to warmest month: 2 + 18 × min(z², 0.64) + 37 × z × continentality °C (London about 13, Moscow 25, Yakutsk 50+). |
| Rainfall | Two sources. The prevailing wind: a zonal profile (wet equator, a dry subtropical belt from about 15° to 30°, wet westerlies, dry poles) scaled by moisture from upwind: each cell traces back along its prevailing wind (trades and polar easterlies from the east, westerlies from the west between 30° and 60°) and dries with the distance of land crossed (fading over 1,400 km, three times as far at the equator, where rising air and forests recycle rain: the Congo and the Amazon). The summer monsoon: land heated in summer draws moist air from the sea on its equator side, and beyond about 27° from the east coast too where that is nearer (the western flank of the subtropical highs): up to 1,300 mm, strongest near 27° and fading to nothing by 5° and 50°, falling off steeply inland (÷ (1 + (distance ÷ 600 km)³)), so India, West Africa's Sahel, East Asia, the eastern United States and northern Australia are wet in summer while the hearts of the Sahara and Australia stay dry. Water narrower than 1,200 km (the Red Sea, Caspian, Black Sea, Baltic) is not a moisture source: a trace crosses it, counting half its width as land, so the steppes behind it stay dry. A ridge more than 800 m above the cell on either path casts a rain shadow; otherwise height lifts rain on the windward side. Rainfall is then smoothed once with each cell's land neighbours. |
| Seasons | Temperature range grows with latitude and continentality (land upwind); rain season is signed (−1 winter rain, Mediterranean; +1 summer rain, monsoon). Monthly values come from these, peaking in July in the north and January in the south. |
| Biomes | Köppen-style classification from the twelve months: ice, tundra, boreal, temperate, warm temperate, Mediterranean, grassland, shrubland, desert, savanna, rainforest. Growing months = months with T ≥ 5 °C and rain ≥ 35% of potential evaporation. |
| Rivers | Runoff = max(0, rain − 0.8 × potential evaporation). Priority-flood drainage (lakes and pits are filled) gives every land cell one downstream cell, and discharge and basin area accumulate downstream; crossing dry land a river loses 3% of its flow per 100 km per 1,000 mm of yearly water deficit (evaporation beyond rain, at most half a cell), so the Nile is much weakened by the Sahara. A cell is a river when its basin is at least 150,000 km² and its flow at least 300 m³/s, so the network looks alike at every grid level. A river cell's floodplain is a strip 0.5 × √flow km wide (2–60 km) along a path as long as the cell's spacing (`World::floodplain_share`): a tenth or so of a level-5 cell for a large river. |
| Soils | Base fertility by biome, lower on steep relief, plus alluvium on large rivers. |
| Aquatic foods | Coasts add 0.3, and up to 0.5 more on cool mid- and high-latitude seas (salmon and seals: the densest foragers known, on the Pacific Northwest coast); big rivers up to 0.6 (0.6 × flow ÷ (flow + 3,000 m³/s)); at most +120% foraging capacity in all (fish and shellfish, not depleted by hunting). |
| Wild species | Each species in `data/species.json` has trapezoid niches (coldest and warmest month, rainfall, rain season, elevation) and a river affinity. Suitability is their product. Homelands are picked from suitable cells (the best of 24 tries by spread, far apart from each other) and ranges spread from them up to `range_km`. Stored as a 32-bit presence mask and an abundance byte per cell. |
| Deposits | Each good in `data/goods.json` that comes from a deposit (salt, flint and obsidian, timber, fish, copper and tin ore, shell, gold) lies in each suitable cell (hills, mountains, coast, dry land, forest...; never on ice sheets) with its chance per 50,000 km², so the same share of land at every grid level, at a random richness stored as a byte. |
| Trade links | Every pair of neighbouring land cells (marked as a river link where one drains into the other along a river), plus sea links between coasts whose open water connects within 500 km. Stored once each with length and height difference, and listed per cell (CSR). |

#### Plate tectonics

Generated worlds get their relief from a simple model of plate tectonics
(`tectonic_relief` in `world.cpp`; the plate of each cell is `World::plate`,
shown by the Plates map mode):

| Step | Model |
| --- | --- |
| Plates | 8–14 seeds scattered on the sphere; each cell belongs to the seed nearest it in a noise-warped space (so borders wander), with a random weight per seed (so plates come in many sizes); stray specks join the plate around them. Each plate turns about its own random pole at its own rate. |
| Continental crust | Plates are made continental in a random order until they cover about the land's share. The crust is a warped low-frequency noise biased toward the continental plates (0.35 of a blurred plate mask plus noise), cut at 1.12 times the land's share, so continents spill over or fall short of their plates: some coasts follow plate borders (active margins), others lie inside a plate (passive ones). |
| Boundaries | At each boundary cell, how fast the plates close or part (their relative velocity along the boundary normal) and the crust on each side; continent rides over ocean, and of two like plates the lighter. Each cell takes the effect of the nearest boundary within its plate (within 1,400 km): ocean under continent, a coastal range 150 km inland (the Andes); continent against continent, the highest ranges and a plateau behind (the Himalaya and Tibet); ocean against ocean, an island arc on the riding plate and a trench on the diving one (Japan); parting plates, a rift valley on land and a mid-ocean ridge at sea. Ranges are broken into peaks and passes by noise, and the uplift is smoothed over the grid's steps. |
| Base relief | Continents rise from a slope and shelf (by distance from the crust's edge) to low interiors with swells and basins; old ranges worn low (ridged noise) inland; the ocean deepens away from the continents. 3–7 hotspots raise volcanic islands and highlands; three passes of weathering wear the heights toward their neighbours. |
| Heights | Sea level is the quantile for the requested ocean share. Land heights follow Earth's hypsometry by rank (median about 450 m, 30% below 250 m, 5% above 2,300 m at the scale of a cell), with a quarter of the world's own relief kept, so mountains stay where the plates put them. |

Over seeds 1–10 at level 5: about 27% of the land within one cell of the coast
(Earth at the same resolution, 29%), 6.1% desert and 5.1% temperate grassland
(Earth: 6.2% and 4.0%), the median land height about 520 m. The M3 gate passes
17 of 18 eras.

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

On Earth the climate model puts the main belts where they are: rainforest in
the Congo, Amazon and Indonesia; the Sahara, Arabia, the Kalahari and
inner Australia desert, with the Sahel and savannas around them; steppe from
the Caspian to Mongolia; forest in the eastern United States, Europe and
China; taiga and tundra in the north. Smaller features are coarser than on
Earth: the Horn of Africa is too wet (its dryness comes from winds blowing
along the coast and cold upwelling), the American Southwest and Patagonia
too green.

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
| Crops and herds | A species can be raised where its wild niche, widened, still fits (`World::cultivated_suitability`): 4 °C on each temperature and 12 °C more toward cold winters (hardy winter wheat and barley), rainfall from 70% to 160%, the rain season by 1.0 either way (spring-sown and winter crops: wheat under the monsoon of northern India and China), 500 m. Where it is first domesticated stays in the wild niche. |
| Farmland | People per km² = 0.6 × (rain-fed land × (1 − floodplain share) + floodplain × floodplain share). Rain-fed: best crop (suitability × food value) × soil (0.35 + 0.65 fertility) × season (growing months) × rain ((rainfall − 200 mm) ÷ 400 mm, from 0.05 to 1: rain-fed crops fail often in dry land) × fallow (shifting cultivation: rainforest 0.35, savanna 0.75, elsewhere 1). Floodplain: watered by the river whatever the rain, its silt renewed each flood: best crop judged as if well watered (`World::watered`) × silt soil (fertility 0.9) × season (warm months, 5 °C and up) × 3, farmed every year. So desert rivers (the Nile, the Euphrates) feed narrow, crowded strips: on Earth river cells in desert and dry scrub hold about six times the people of dry land without rivers. Then, plus half of 0.15 × best herd × pasture (grassland 1.6, shrubland 1.3, savanna 1.2, Mediterranean 1.0, tundra 0.8, forests 0.7, desert 0.6, rainforest 0.4); herds alone feed pastoralists. Where herds give most of the farm food (dry grassland and shrubland, the steppe) people are pastoralists. A regional average for early farming; later techniques raise it. Each year × harvest (rain 40–115% of normal, −12% per °C off normal, down to 30%; "normal" is the climate the crops are bred for, below); the harvest buffer of their techniques makes good its share of a shortfall. |
| Climate and crops | People are used to the climate of the last generation or so: each cell keeps a running mean (1/30 a year) of its warmth above the world's own climate and its rainfall against the world's own. Local varieties are bred for the swings of the climate they grow in, so the part of that change beyond 1 °C (or 10% of the rain), in steps of 0.25 °C (2.5%), is the climate the crops are bred for. Where it moves a step, each species' suitability as a crop or herd (its niche widened as for domesticates, against coldest and warmest months shifted by the change and rainfall scaled by it) and the cell's growing and warm months are worked out again, and farmland, the spread of domesticates and farming colonists all use them; the year's harvest is judged against it too. So natural swings are hardship as before (pressed farmers take up new techniques), a sudden large change is felt in the harvest and drains the stores, and a lasting one changes what grows: a cooling drives crops toward the equator and shortens the summers, a warming opens the cold lands and parches the dry margins. People farm the best crop they know that suits the new climate. Forcing on a level-4 world at year 6,000 (none: 4 polities collapse in a century; mean state stability 0.72–0.76): −10.4 W/m² (about −8 °C) drains the stores over some 30 years, drops stability to 0.45 and brings 142 collapses (133 in famine) and uprisings at three times the usual rate, the people falling from 50 to 27 million; +10.4 W/m² is a brief shock (stability 0.61) before the crops follow the heat; −2.6 W/m² (about −2 °C) is a lean spell: the stores fall by a third, a few collapses. The inspector shows the usual climate and the growing season under it, and the growing-season map follows it. Over seeds 1–10 at level 5 the M3 gate passes 18 of 18 eras. |
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
| Reach | Control of a cell = 1 / (1 + (travel distance from the capital / reach)²), the travel distance being the cheapest route over the land, river and sea links (below); reach = 85 km × (1 + administration) × (1 + 0.3 contact). Writing (+0.5), bureaucracy (+0.8), roads (+0.8), provincial government (+2) and bronze (+0.1) add administration, so realms grow as these come in turn: a chiefdom holds a cell or a few, a state with writing a ring around its capital, an empire with provinces and roads millions of km². |
| Travel of rule | Each year each living realm finds its travel distance from its capital to the cells within 6 × its reach (`RuleField`: a shortest-path search over the trade links; beyond, the straight-line distance). Overland: km × the mean of the two cells' terrain (1 temperate and Mediterranean land and shrubland, 0.9 grassland and savanna, 1.4 boreal forest, 2 desert, tundra and rainforest, 4 ice; × (1 + metres above 1,000 ÷ 1,500)) × (1 + 0.6 × the height difference in km) ÷ (1 + half its land transport); along a navigable river 0.8 × km, by sea (with boats) the sea route's km, each ÷ (1 + half its water transport). So a realm's grip stops at mountains and deserts, runs up river valleys and along coasts. Used for expansion, strength, levies, conquest, provinces, garrisons, secession, vassal absorption, assimilation and conversion; successor seats at a collapse still use straight lines. Over seeds 1–10 at level 5 the M3 gate passes 17 of 18 eras. |
| Direct rule | Ruling a cell directly takes 1 ÷ control of a realm's administration (in level-5 cells' worth of land), so remote cells cost far more; a realm can run at most 40 × (1 + administration) × (1 + 0.3 contact) (× 2 for nomads). At its limit it takes no more land; at 80% of it a settled realm makes war for tribute rather than land: its victories in the field win it a tributary and keep no ground. The inspector shows how much of its administration a realm uses. The M3 gate's era table counts the largest realm with its tributaries and vassals (`YearRecord::largest_sphere_km2`), since many early empires were largely tributary; over seeds 1–10 at level 5 it passes 14 of 18 eras (main measured the same way: 16), the misses being the two earliest eras, where main is at the limit too. With nomads' full reach and subjects waiting on bronze arms and writing extending rule by a quarter (it was a half), 16 of 18 (seeds 1–5 all nine; on seeds 6–10 the two earliest eras, early chiefdoms' spheres among them). |
| Expansion | Each year a polity tries the stateless neighbouring cell it would control best (at least 30%), with chance 3% × control × (0.5 + warfare) × strength ratio. A settled realm (its capital not living mostly by herds) takes only land worth ruling: where crops (farming × (1 − herding)) are at least a quarter of the food, or trade passing through is worth at least a head's share; herders' and foragers' land, with little to tax and people who can move away from tax collectors, it leaves alone. Herding realms and nomads take pasture as before. Land changes hands between polities only in war (below). |
| Stability | Each polity's stability (0–1) moves toward a target each year (10% of the gap when rising, 30% when falling): 0.45 + 0.25 × hierarchy + 0.15 × ritual (people at the capital) + 0.1 with writing + 0.1 with bureaucracy + 0.1 × the share of people holding the rulers' organized faith (or a sect of it), minus weighted pressures: famine (× 1.0; three times the share of people beyond what the land and food imports feed), war (× 0.8; the toll of land lost, war dead, lost wars and raids, fading 20% a year), overreach (× 0.5; 1 − the people-weighted mean control, each cell's control less by half its people's mobility), epidemic (× 0.6; disease deaths beyond 0.5% a year, × 40), division (× 0.3; share of people of another cultural lineage than the rulers, plus half the share holding another organized faith) and poverty (× 0.15; little trade per person). |
| Rulers' people | Each year the capital's speakers of the polity's language send 0.1% × control of each province's people (held at least 30%) to it: officials, garrisons and merchants who carry the rulers' language there. A capital moves to a town of the realm (for nomads, any cell) with twice as many townspeople (people) as the old seat. |
| Provinces | A state (with writing) of four or more level-5 cells' worth is divided into provinces seated at its largest towns, 500 km or more from the capital and from each other; each cell belongs to the nearest seat, the capital's being the crown's own land. Each province has a governor (a figure, re-appointed when he dies) and a loyalty that moves 10% a year toward 0.5 × stability + 0.4 × the capital's control at its seat + 0.1 if its people are the rulers' + 0.05 if their faith is + 0.3 × (0.5 − the governor's ambition) + 0.3 × (the ruler's ability − 0.5) − 0.3 × how badly a war the crown leads is going (its war score against it) − 0.15 × min(1, the governor's years in office ÷ 30) (a governor long in office makes the province his own); a new ruler costs every province 0.1 loyalty at once. A province below 45% is autonomous in all but name: it sends the crown only 30% of its soldiers, so a realm hollows out before it breaks. Below 40% a province breaks away with chance 5% × (0.4 − loyalty) ÷ 0.4 × (0.5 + ambition) a year: its governor becomes the first ruler of a new realm and founds a house, and fights for independence; the realm loses 0.05 stability and every other province 0.05 loyalty, so a weak moment can set off a cascade. When a state with provinces breaks up, every province below 60% loyalty goes its own way under its governor, and the loyal ones stay with the rump at the capital, if an heir holds it (always after a disputed succession, otherwise half the time); if not, every province goes its own way and the rump keeps only the crown's land. States with provinces lose land this way instead of by the secession of single cells and civil war. The provinces map mode shows them by loyalty, with the crown's land in gold. Over seeds 1–10 at level 5 the M3 gate passes 15 of 18 eras (main 15–16). |
| Uprisings | In every state, each province and the crown's own land has an unrest that moves 20% a year toward hunger (the share of its people beyond what its land feeds) + 0.3 × land hunger (people ÷ what the land feeds, from 0.85 to 1.15) + 0.1 at war + 0.3 × the recent toll of war + 0.3 × epidemic pressure + 0.15 paying tribute + 0.15 under rulers of another people + 0.1 of another faith + 0.2 × (0.5 − the ability of its governor, or of the ruler on the crown's land). Above 30% the people rise with chance 8% × (unrest − 0.3) ÷ 0.7 a year, led by a rebel leader (a figure): 15% × unrest of them against a force of 10% of the realm's strength × its control there × (0.5 + stability) (× 1.5 with a standing army). Crushed (chance force ÷ (force + rebels)): 2% of the region's people die, unrest halves, the realm loses 0.03 stability. Victorious: 1% die; a province breaks away under the leader, and on the crown's land or with twice the force against them the dynasty falls and the leader takes the throne and founds a house; the realm loses 0.1 stability. On a level-4 world over 5,000 years: 322 risings, 223 crushed, 88 provinces lost, 11 dynasties overthrown. The unrest map mode shows them. Over seeds 1–10 at level 5 the M3 gate passes 16 of 18 eras. |
| Generals and high priests | Every state at war or with a standing army has a general (a figure in `SimState::commanders`), appointed at the capital aged 30–55 and replaced when he dies; he retires when the realm is at peace without an army. In war the leading realm's strength on each side is × (0.75 + 0.5 × his martial skill). A victory gives him 0.3 fame (+0.3 if an enemy capital fell); a defeat costs 0.3 and gets him dismissed half the time; fame fades 3% a year. Each year a general may seize the throne with chance 1% × fame × (0.5 + ambition) × (1.5 − stability) × (1.5 − the ruler's ability), doubled under a regency; he founds a house if he lives to take it the next year. In a disputed succession that passes to a new house, a general with fame ≥ 0.3 is the usurper. States with a temple or an organized faith have a high priest (35–60, replaced on death): stability's target gains 0.1 × 2 × (the ruler's piety − 0.5) × (0.5 + the priest's piety), and the unrest of people of another faith gains 0.1 × 2 × (the priest's piety − 0.5) when he is more pious than not. On a level-4 world over 5,000 years: 731 generals, 21 of them seized thrones, and 1,366 high priests. Over seeds 1–10 at level 5 the M3 gate passes 17 of 18 eras. |
| Breakdown | Provinces under 20% control secede (1% a year) and polities of 6 or more cells split in civil war (0.4% × (1 − mean control)), the far half breaking away and fighting a war of independence; both are multiplied by 2 × (1 − stability). Below 30% stability a polity breaks up with chance 0.3 × ((0.3 − stability) ÷ 0.3)² a year, its heaviest pressure recorded as the cause. A state leaves a core (cells it controls at least half) under an heir at its old capital, if one holds it (always after a disputed succession, otherwise half the time; if not, the old capital is one claimant among the rest, with its own reach); the rest balkanizes by how advanced each place is. Places that could found a polity (farming, density, hierarchy) are ranked by (townspeople + 5% of people) × hierarchy, × 4 with writing and × 2 more with scribes, × 0.6 at the old capital (the fallen court discredited, its city sacked or deserted) and × a random 0.4–1.6 (the fortunes of its local strongmen), so the old capital chooses first in about a quarter of break-ups instead of five in six; strongest first, each not already held (control ≥ 30%) by a stronger seat becomes a successor that takes the cells it holds best, as far as its own administration reaches (85 km × (1 + administration), wider for nomads) and in a fallen state at least 200 km, the province it held under the old realm: a great literate city keeps a large province, a lesser town its own. Seats with writing are successor states, the others chiefdoms; land no seat holds falls back to villages. A chiefdom of at least three level-5 cells' worth breaks the same way into smaller chiefdoms; a smaller one dissolves into its villages. (Say an empire of 21 level-4 cells: a rump of 5, two successor states of 4, six chiefdoms of one cell, two cells of villages.) Over seeds 1–10 at level 5 the M3 gate comes out as before (16 of 18 eras against main's 16; first states and early sizes alike); late empires are about a tenth smaller at level 4, as successors now keep only what they can administer. With successors holding provinces, they last a median 90–170 years instead of 30–90 (a quarter fall within 40–55 years instead of 7–10), though the rump still often wins its old provinces back within a generation; over seeds 1–10 the gate passes 15 of 18 eras. Until rulers are simulated (part 6), succession disputes break up chiefdoms at 0.05% and states at 0.02% a year whatever their state. A polity that loses its last cell was conquered (if the toll of war on it was heavy) or dwindled away. |
| Rulers | Each polity has a ruler of a house (`RulerSystem`, before the polity system each year). A new realm's first ruler founds a house; a ruler dies at 0.8 × the forager death rate for his or her age, and an heir of the house succeeds, a child (under 16, with a regent) if the ruler died young. A succession is disputed with chance 15% × (1.5 − stability), doubled for a child heir: of the disputed, 6.5% of chiefdoms and 2.6% of states break up (the old "succession crisis"), and the rest pass to a usurper who founds a new house. Rulers' ability (0–1) moves the stability target by ± 0.1 (−0.1 under a regent), and their martial bent (half the people's warfare, half chance) scales the chance of declaring war by 0.5 + martial. Heirs often take a forebear's name, with a regnal number. Reigns last about 27 years; the longest houses about a thousand years. |
| Institutions | A polity may found (1% a year once it qualifies) a temple priesthood (rulers of an organized faith, or ritual ≥ 0.6 with a town of 5,000), a council of notables (3+ level-5 cells' worth of land, hierarchy ≥ 0.4), a law code (writing) and a standing army (writing, 300,000 people, bronze or riding). Temples add 0.05 to the stability target and make disputed successions rarer (× 0.8); councils too (× 0.6); law codes add 0.05 and cut division (× 0.7); armies add a quarter to war strength and cut overreach (× 0.7). Below 30% stability each may be lost (1% a year). A broken state's core and successors keep its institutions, but not its army. |
| Culture | Being ruled pushes hierarchy, warfare and ritual up. |

Writing needs granaries, a calendar and a town of 25,000 people to be
invented (not to be kept); bureaucracy and roads follow it (roads need no
wheel: the Inca built them without), and provincial government needs both. At level 5 a polity lasts about 120 years at the
median (a quarter outlast 350). Of those that end, over half are conquered,
about one in six collapses in famine, about one in ten in epidemics or from
overreach, and about one in six breaks up in a succession dispute; break-ups
cluster in Bond events and epidemics. Chiefdoms appear a few centuries after
farming, thousands rise and fall, and the first states (with writing) appear
around 5,000–10,000 years after the first crops (in some worlds, without
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
one never invents writing and one never builds roads. Since the monsoon and
dry-subtropics rainfall (M4) the gate has not been re-tuned: with seeds 1–3
all nine eras still pass, but the first states come about 1,350 years early
(4,950 years after the first crop, against 6,300). After the changes to
wandering peoples and nomads that followed, seeds 1–3 pass seven of nine:
the last two eras come out too small (a median 1.0 million km² at 500 BCE). After the farmland changes (fallow, wider crop ranges, warmer maritime
mid-latitudes), seeds 1–3 pass all nine eras again (2.0 million km² at 500
BCE), but the first states come about 2,400 years early (3,900 years after
the first crop) and the largest polity before 5,000 years of farming is 0.52
million km² (the check wants at most 0.3): denser farming grows towns, and
writing, sooner. Re-tuning is left for later. With floodplains (rain-fed land no longer
counts a whole river cell as watered), the swing goes the other way: in
seeds 1–3 the first states come about 8,000 years after the first crop
(seed 1 has none in 11,000 years); the size eras that can be scored pass and
early polities stay small. Foraging by biome leaves that about the same (first states about
7,900 years after the first crop; the last two eras, scored on one world,
too small). With steppe clans, plunder and deaths on the march: first states
about 7,900 years after the first crop; the six eras that can be scored
pass. Re-tuned for the first states: writing now needs a town of 25,000 in the
hinterland (Uruk had some 40,000 by 3200 BCE) and, once one stands, comes
within a few thousand years (rate 1e-4, from 3e-6), so the size of the
largest towns sets the date rather than a long lottery among the one or two
places that qualify; scribal bureaucracy comes three times as fast. With seeds
1–5 at level 5 all checks pass: the first states come a median 6,640 years
after the first crop (5,100–9,900 by world), bureaucracy about 1,000 years
after writing, provinces in four worlds of five, and all nine eras are within
a factor of 3 (a median 0.8 million km² at Akkad's time and 2.0 by 500 BCE).
The largest polity in the first 5,000 years stays a chiefdom-sized 0.24
million km².

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
| Food stores | After the harvest and food trade, a cell whose land feeds more than its people puts by 0.7 × (0.1 + 0.9 × farming share) of the surplus, up to (0.25 + 3 × storage) years of food for its people (storage: the harvest buffer of pottery, granaries, calendars and scribes). In a lean year the store feeds those the harvest does not; it spoils 10% a year and is lost when a cell empties. Part of a bad harvest is still made good without stores (mixed crops and fields, famine foods, kin: the same harvest buffer), so stores carry people through a bad year or two and famine comes when a long drought empties them. Raids carry off 30% of the stores of the lands they strike to the raiders' capital, and tributaries send 10% of their capital's stores to their overlord's. Farming cells hold a median of about 0.45 years of food at level 5 (a quarter over a year); the "Food stores" map shows them. |
| Grain trade | Stored food beyond half a year of a cell's own needs is for sale: each year half of it goes to neighbouring markets where food sells for more than here plus the carrying (food weighs 1: about 0.3 per 100 km overland, 0.1 down a river, 0.15 by sea), split by the gain, into their stores, where it feeds the hungry or passes on a link a year. Overland grain goes little farther than a cell or two; by water it travels far. |
| The capital's bread | A polity with writing takes 30% of what each of its cells grows beyond its own people's needs (× its control there) and carries it to its capital, losing 50% × (1 − control) on the way. It counts as food brought in: a great capital outgrows its own land and its townspeople multiply, and when the realm falls or loses its provinces, the grain stops and the city starves and empties, as Rome did without Egypt's grain. |
| State granaries | A polity with writing moves stored grain from its full granaries (beyond half a year's reserve) to its provinces whose people this year's harvest, trade and own stores do not feed: up to 30% of what is spare, 70% with a scribal bureaucracy. |
| War and trade | No goods, food or grain cross a link between polities on opposite sides of a war. |
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
| Relations | `systems/diplomacy.hpp`. Every pair of living realms in touch (bordering, overlord and subject, allied, or at war within the last 150 years) has a `Relation` (`SimState::relations`, sorted by pair): an opinion from −1 to 1 that moves a tenth of the way toward a target each year (a new acquaintance starts at it). Target: +0.3 if their founders' cultures share a root; +0.2 if they share a faith, otherwise −0.4 × the zeal of the more zealous high priest (2 × (piety − 0.5)); +0.2 × how much the less mercantile capital lives by trade; −0.7 × 25 ÷ (25 + years since their last war); −0.15 per cell either claims of the other's (at most −0.6); +0.3 if allied; −0.2 if one pays the other. A war begun costs 0.3 at once. Land lost in war is claimed by the loser (`SimState::claims`) for 100 years, until won back or the claimant dies. A realm has cause for war on another if it claims land of it or its high priest (piety ≥ 0.7) preaches holy war on another faith. |
| Declaring war | A polity that is no one's subject, not at war, and not under truce may attack a neighbouring polity when its strength, weighed by what it can bring to the enemy capital (× its control there ÷ 0.2, between 0.85 and 1), is at least 1.2 times the defender's plus half its overlord's and allies': chance 8% × (0.5 + warfare) × min(2, ratio − 1) × (3 with cause for war, 1.5 without) × (1 − 0.5 × opinion) a year. Nomads attacking farmers fight for tribute; others for conquest. The defender's overlord, allies and vassals join it; the attacker's vassals join the attacker. |
| Fighting | Each year each side may take the enemy cell along the front nearest its field army (below; without one, the one it controls best), if it would control it at least 15% (distance counted less by the cell spacing − 112.5 km, so a capital's neighbours are always in reach): armies do not outrun their rulers' grip (chance 30% × min(2, ratio − 1), up to three tries for a side three times stronger). A taken cell is sacked (3% of its people killed), 10% flee to their own side, and 3% of the people of the cell it was taken from move in as settlers and garrisons (a conquest dynasty brings 10% of its homeland). Battles kill men of fighting age in the front cells: 10% of 30% of the smaller army, split by the other side's share of strength. |
| Field armies | When a war begins each side's leading realm raises half its levy as a field army at its capital, under its general (`SimState::armies`). Each year it marches up to 450 km by land (× 2 for mounted nomads, × (1 + land transport)) toward the enemy army, if that is in its own side's land or within 700 km, else toward the enemy capital; its side's conquests that year are made next to it, and it moves into what it takes, so a war is a drive on the enemy capital. Armies in the same or neighbouring cells fight a battle (`SimState::battles`): the winner is drawn with chance s₁² ÷ (s₁² + s₂²), strength = men × the quality of its realm's soldiers (military techniques, a standing army) × its general; the loser loses 40% of its men and falls back a cell toward its capital, the winner 10% × the strength ratio; the dead are men of fighting age of each realm, and the winning general gains 0.15 fame. Marching wears an army down: 8% of its men a year in enemy land (30% of that at home), × 2.5 in desert, tundra and rainforest; at home it is refilled by 15% of its strength a year. It lives off the land: it eats half a year's food per man from the stores where it stands, and where they run short up to 2% of the people starve; the unrest of the province (or the crown's land) it stands in rises by 0.05 a year. An army worn below 20% of its men breaks up: its side fights on at 60% of its strength, the war is twice as likely to end each year, and a new army is raised after five years. Armies disband when their war ends. A general at the head of an army that has won battles is up to three times as likely to seize the throne. The war and realms maps show armies (red, larger for more men; a white heart for one that has won more battles than it lost); the inspector lists a realm's field armies and the armies in a cell; battles that kill 1,000 or more are in the timeline. On a level-5 world over 7,000 years: about 6,700 armies (most of chiefdoms, of a few hundred to some 19,000 men) and 19,000 battles killing 6 million. |
| Peace | Each war keeps a score from the attackers' side (`War::score`, −1 to 1): 2 × the land gained (cells taken less cells lost, over the cells the losing leader held when it began), + 0.4 × (field battles won less lost) ÷ (all battles + 2), + 0.3 for a field army at the gates of the enemy capital (less 0.3 for the enemy's at its own). A war ends when a leader's capital falls, when a leader is gone, or by peace: exhaustion (5% × (1 + years at war ÷ 10) a year), or the side the score is against suing for it (30% × the score's size a year once it is at least 0.5). Terms (`War::terms`) follow the score: within 0.1 of 0 a white peace, all land taken in the war given back both ways; otherwise the victors keep what they took, and if they fought for tribute and won by at least 0.4 (or took the capital) the loser pays it; defenders who prevail take back what they lost. A defender whose capital fell becomes a vassal (or is annexed if one cell is left). An ally of a side's leader (not its vassal or overlord) with the score at least 0.5 against them may make a separate peace (8% a year × (1 − opinion of the leader ÷ 2)), with a truce with the enemy leader: a betrayal (trust −0.3, opinion −0.3, the alliance ended). Allies who see a war through gain trust (+0.1), and trust weighs in the choice of allies (× (0.5 + trust)). A 20-year truce follows. |
| Infamy | The balance of power (`Polity::infamy`): a realm that takes land in war gains the cells taken ÷ max(5, its cells), and 0.3 for each realm it annexes or makes its vassal; infamy fades 5% a year. Every relation's opinion target falls by 0.5 × the greater infamy of the pair (at most 1). A neighbour with infamy at least 0.3 counts as a threat to ally against if it is stronger at all (otherwise only 1.5 times stronger), and the chance of finding an ally against a threat is × (1 + its infamy). Without it, quicker peaces and separate peaces let conquerors snowball (largest realms some 20% larger at level 5). |
| Restoration | For 200 years after a realm falls, the realms that rose from it (its successors, and provinces that broke away from it) are rival heirs: each has cause for war on the others, they think 0.2 the worse of each other, and the same holds between a claimant's party and the crown it claims after a war of succession left the realm divided. So a fallen empire's successors fight over its legacy until one reunites much of it, or the claim lapses. (Recalling disloyal governors and a lower infamy for restoring land were tried and dropped: both made empires too large in the M3 gate.) |
| Wars of succession | A disputed succession that would break a free realm of two or more cells is fought out instead: the claimant's party takes the provinces below 60% loyalty (at least the least loyal one; without provinces, the half of the realm farthest from the capital), seated at the province's seat or its largest town, and fights a war of succession. The victor rules the whole realm (a claimant moves into the old capital); a war that ends in neither's favour leaves it divided, the two rival heirs. On a level-5 world in 9,000 years about 70 such wars: the crown wins most, the claimant a few, and a few divide the realm; most disputes are in chiefdoms of one cell, which still break up. Over seeds 1–10 at level 5 the M3 gate passes 14 of 18 eras (main 12). |
| Subjects | Tributaries and vassals send their overlord 10% of their stocks of goods each year; vassals fight for it. A subject weighs its overlord's hold on it: the overlord's strength × its control at the subject's capital ÷ 0.2 (between 0.5 and 1), × 0.6 if it is at war with someone else, × (0.5 + trust). A subject as strong as that rebels (3% a year), starting a war of independence; a tributary at least half as strong withholds its tribute and goes free (4% a year), which gives the overlord cause for a war to restore it (fought for tribute) for 30 years. Both chances are × (1 − opinion), × 1.5 for another culture root and again for another faith, and × 2 in the year either realm has a new ruler (at least 0.5, at most 2 in all). An overlord at war elsewhere when its subject is attacked leaves it to fend for itself (trust −0.25, opinion −0.2); one that comes to its aid gains trust (+0.1); trust drifts back toward a half by 0.02 a year. Otherwise a vassal whose capital its overlord controls at least half (the distance counted as for conquest, so a bordering vassal qualifies at any grid level) is absorbed (1% a year). Vassals are few (about a dozen in 9,000 years at level 5): a beaten polity of one cell is annexed outright, and most realms grow that way. |
| Alliances | A free polity with a neighbour 1.5 times stronger seeks a defensive ally among its own and the threat's neighbours (strongest first, related cultures counting double, × (1 + opinion)): chance 5% a year, doubled for related cultures, up to three allies each. Alliances lapse at 1% a year, or when the allies fight each other. A threatened realm (not nomadic) that finds no ally may instead offer the threat tribute (2% a year × (1 + opinion), × 1.5 for a related culture), if the threat is a free state (it writes) with capacity to rule more that could bring at least three times the realm's strength to its capital (strength × control there ÷ 0.2, at most 1). |
| Nomads | Pastoralists are cells whose farm food comes mostly from herds; they move as freely as foragers. Herders without mounts form ordinary tribal chiefdoms (needing only a quarter of farmers' density). On open pasture (grassland, shrubland, savanna), or in desert and dry scrub where camels are kept, where herders ride (horses or camels) and warfare is at least 0.4, confederations form, and herder chiefdoms that take to riding there become confederations: 1% × warfare × (0.5 + 0.67 × mobility) per five-year check, times 1 + the wealth of the settled farmers within 600 km ÷ 20,000 (up to 5), and five times as readily with a state (a polity with writing) within 1,500 km: the "shadow empires" that rose opposite rich agrarian states (Barfield 1989). (A riding herder chiefdom becomes a confederation with chance 5% a check beside a state, 1% without: most riding tribes stay tribes, for confederations to take in.) A herder tribe (no writing, no city, mostly herding) beaten by nomads in war or by raids becomes a vassal clan, not a tributary, adding half its strength, and merges into the confederation when they border (2% a year); small tribes are usually overrun in the fighting and their land and people taken outright. Loot and tribute received count as plunder (at base prices, fading 20% a year): a confederation's followers come for it, its strength × (1 + 0.5 × plenty) and its stability target ± 0.1 (plenty = min(1, plunder ÷ (people × 0.05)): cut off from loot, it loses followers). Mounted herders who do not band together form tribal chiefdoms. Horses and other herd animals spread fast among mobile herders (learning from neighbours × (1 + 12 × mobility × the share of food from herds)), so riding crosses a steppe in centuries. Their rule reaches twice as far once they have bronze arms (mounted warfare as the steppe knew it, got by contact with the states), 1.3 times before; and before bronze, a victory over a neighbour wins them plunder, not a tributary or vassal clan. Each year a confederation may raid (30%) the richest settled farmers within 600 km of its land (wealth = people in towns + a twentieth of the rest), succeeding with chance attack ÷ (attack + defence), its strength weighed by its control at that distance: it carries off 30% of the cell's goods and kills 1% of its people, and the victim's ruler, if the nomads are at least half as strong and their hold on it (as for subjects, diplomacy.hpp) at least twice its strength, may agree to pay tribute (20%), unless it withheld tribute from them in the last 30 years; tributaries are not raided. Nomads who take the capital of a state, or of a polity seated in a city, in a war for tribute while 1.5 times as strong found a conquest dynasty: they annex it, rule from its capital, and settle down after 60 years. |

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
years. `histsim diplomacy [--id POLITY]` prints how the leading realms stand
(infamy, subjects, allies, friends and foes, wars with their scores), one
realm's relations in full (opinion, trust, wars, claims, truces, grievances,
rival heirs) and the last 25 dealings. The diplomacy map colors the realm
ruling the clicked cell gold, its overlord and subjects purple, allies blue,
enemies red, realms under truce orange and the rest by its opinion of them
(red to green, pale where they have no dealings); with no cell chosen, it
shows each realm's infamy (`histsim map --mode diplomacy` centres on the
realm `--id`, else the largest). The inspector lists a realm's relations,
claims, grievances and infamy.

### Historical figures

Beside the many counted in cohorts, a few people are followed by name
(`SimState::figures`), after the historical figures of Dwarf Fortress's
legends: every ruler; for states (polities with writing), the ruler's spouse
and the children who outlived infancy; the rival who claims a throne in a
disputed succession (the heir's eldest grown sibling, or a kinsman); usurpers;
the inventor of each independent invention; and the teacher of each
missionary faith ("Teaching of X" is X's). Each figure has a name in its
people's language, a sex, birth and death, parents, spouse, house, realm,
culture, language and home cell, its roles, four traits (ability, martial,
piety, ambition; a ruler's ability and martial are its reign's) and a goal
drawn from them (conquest, a prosperous realm, faith, power, knowledge or
the family's future).

Figures are recorded from their own random stream (`Stream::Figures`), so
recording them changes nothing else. A ruler's family is filled in when the
ruler dies: the heir the succession rules chose becomes one of the children
(born when the ruler was 20–35, or, for a ruler who died a child, a younger
sibling or a kinsman), with a spouse and up to three siblings besides. Lives
are drawn from the life table when a figure is recorded (times 0.8: they eat
well), so nothing is updated year by year; a ruler's death is RulerSystem's.
Chiefs' families are recorded only when their succession is disputed:
thousands of chiefdoms rise and fall, and their kin would swamp the record
(at level 5 about 43,000 figures by year 7,000, one per reign or so, adding a
quarter to a snapshot).

What rulers are like steers what their realms do. Each trait scales a chance
by 0.5 + trait (from 0.5 to 1.5, 1 on average, so overall rates and the M3
calibration hold and only reigns differ): ambition the chance of declaring
war (beside martial, which did so before); piety the founding of a temple
priesthood and the pull of the rulers' faith on their subjects; ability the
founding of councils and law codes (beside steadying the realm, as before);
martial the founding of a standing army. In a disputed succession the
claimant is the heir's most ambitious grown sibling (else a kinsman), and the
chance that the claim tears the realm apart is scaled by the claimant's
standing among those who might have claimed (the most ambitious of k has
ambition^k, which is uniform, so breakups are no more common than before).
The M3 gate, seeds 1–5 at level 5, still passes all checks: first states a
median 6,640 years after the first crop, all nine eras within a factor of 3,
the largest polity in the first 5,000 years 0.29 million km²; ten worlds at
level 4 give the same picture with and without the traits. Over 6,000 years at level 4, the most ambitious rulers start about
1.5 times as many wars per year of reign as the least, the most pious found
temples 2.4 times as often, and claimants who broke realms average 0.62 in
ambition against 0.5 for all. Next: marriages between houses, and generals
and merchants leading the armies and caravans to come.

`histsim figures` counts them and lists the most notable now (the rulers of
the largest realms, teachers of faiths, the latest inventors); `histsim
figure --id N` prints one life, family and deeds (wars, conquests,
institutions and conversions in the reign). In the viewer, the People button
(P) opens the same pages, a ruler's name in the inspector and timeline
entries about a person link to theirs, and pages link to relatives and
places.

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

**Threads**

Systems split their per-cell and per-cohort work across threads with
`parallel_for` (parallel.hpp), and a run is still bit-identical with any
number of threads, on any machine. Inside a parallel loop:

- Each index writes only its own data (its cell's slots, its own cohort) and
  reads nothing another index writes in the same loop. Anything that reads
  its neighbours reads last year's values or a copy taken before the loop.
- Each index draws from its own `RngStream` (keyed by cell or cohort), never
  from one shared by the loop.
- Results that go elsewhere (new cohorts, inventions, sales to other markets)
  are gathered per chunk with `parallel_gather`, which hands them back in
  index order, and are applied after the loop exactly as a serial loop would
  have applied them. Floating-point totals are added up serially in index
  order; integer totals may be added in any order.
- Work whose order matters (a cell's choice depending on what an earlier cell
  did this year) stays serial.

The thread count defaults to the machine's cores (up to 16).
`HISTSIM_THREADS=1` runs everything on one thread; check a change to a
parallel loop by comparing hashes with `HISTSIM_THREADS=1` and `=4`.
`HISTSIM_PROFILE=1` prints the time spent in each system at exit. The viewer
runs the simulation on its own worker thread (`HistSimWorld::queue_years`),
so drawing never waits for more than one year.

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
