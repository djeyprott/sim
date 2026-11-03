// Static world: terrain, baseline climate and seasons, biomes, rivers, soils,
// wild species and the starting map of peoples.
//
// Nothing here changes during a run. The climate and environment systems
// write this year's conditions into SimState, as departures from the
// baselines below.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "histsim/content.hpp"
#include "histsim/grid.hpp"

namespace histsim {

// What the terrain is: generated from the seed, or Earth's (ETOPO1 relief,
// baked into data/earth). On Earth the seed still places species, deposits
// and peoples and shapes all of history; only land and sea are fixed.
// Append new values; never renumber (saved in snapshots).
enum class WorldKind : std::uint8_t {
    Generated = 0,
    Earth = 1,
};
const char* world_kind_name(WorldKind kind);  // "generated", "earth"

struct WorldParams {
    std::uint64_t seed = 1;
    int grid_level = 5;
    double ocean_fraction = 0.70;  // share of cells below sea level (generated worlds only)
    WorldKind kind = WorldKind::Generated;
};

// Biomes from a Köppen-style classification of each cell's monthly climate.
// Append new values; never renumber.
enum class Biome : std::uint8_t {
    Ocean = 0,
    IceSheet,
    Tundra,
    BorealForest,
    TemperateForest,
    WarmTemperateForest,
    Mediterranean,
    TemperateGrassland,
    DryShrubland,
    Desert,
    TropicalSavanna,
    TropicalRainforest,
    Count
};
const char* biome_name(Biome b);

// Forager carrying capacity per km^2 of land foods (plants and game) for a
// given climate and biome: 0.2 x warmth x wetness x forage_quality(biome).
// Warm, wet land supports up to ~0.2 people/km^2 before the biome; ice and
// desert support none. Used for the baseline at world generation and every
// year by the environment system.
double forager_density(double temperature_c, double precipitation_mm, Biome biome);

// How rich a biome's land foods are for foragers, relative to its climate:
// the big-game grasslands and savannas and the nut- and acorn-rich
// temperate woods above 1, the rainforest (food scattered high in the
// canopy, little game, few starchy plants: Bailey et al. 1989) well below.
double forage_quality(Biome biome);

struct World {
    WorldParams params;
    Grid grid;
    std::uint64_t content_hash = 0;  // content pack the world was built with
    std::shared_ptr<const Content> content;  // that pack (never null)

    // ---- Terrain
    std::vector<double> elevation_m;            // relative to sea level; negative = ocean depth
    std::vector<std::uint8_t> is_land;          // 1 = land, 0 = ocean
    std::vector<std::uint32_t> coast_distance;  // steps to nearest ocean cell (0 for ocean)

    // ---- Baseline climate
    std::vector<double> temperature_c;          // mean annual temperature
    std::vector<double> precipitation_mm;       // mean annual precipitation
    std::vector<double> temperature_range_c;    // warmest minus coldest month
    std::vector<double> rain_season;            // -1 all rain in winter .. 0 even .. +1 all in summer
    std::vector<double> continentality;         // 0 maritime .. 1 deep interior (land upwind)
    std::vector<std::uint8_t> biome;            // Biome
    std::vector<std::uint8_t> growing_months;   // months warm and wet enough for plants to grow
    std::vector<std::uint8_t> warm_months;      // months warm enough (5 C), water aside: a floodplain's season

    // ---- Water and soil
    std::vector<CellId> downstream;             // where this cell drains (kInvalidCell for ocean)
    std::vector<double> discharge_m3s;          // mean river flow out of the cell, m^3/s
    std::vector<double> basin_km2;              // land area draining through the cell, itself included
    std::vector<double> soil_fertility;         // 0..1
    std::vector<double> aquatic_bonus;          // extra foraging from coast and rivers (0 = none)

    // ---- Life
    std::vector<double> forager_capacity;       // baseline people supported by foraging
    std::vector<std::uint32_t> species_mask;    // bit s set if species s is native here
    std::vector<std::uint8_t> species_abundance;  // cell * species_count + s, 0-255
    // How well each species does here once domesticated (its niche widened),
    // cell * species_count + s, 0-255. Crops and herds can be raised far
    // beyond where their wild ancestors grew.
    std::vector<std::uint8_t> cultivated_suitability;
    // The same where water comes from a river, not the rain (floodplains):
    // rainfall and its season do not limit (cell * species_count + s, 0-255).
    std::vector<std::uint8_t> watered_suitability;
    // Share (0-1) of a cell's land that is a river's floodplain: river cells
    // only, a strip kFloodplainKmPerRootFlow x sqrt(flow) km wide (2-60 km)
    // along a path as long as the cell's spacing: the Nile's about 25 km,
    // a tenth of a level-5 cell.
    std::vector<double> floodplain_share;
    // Every (cell, species) where a domesticable species grows wild, sorted by
    // cell then species: where domestication can begin.
    struct WildStand {
        CellId cell = 0;
        std::uint8_t species = 0;
    };
    std::vector<WildStand> wild_stands;
    // How well each disease's niche fits each cell (cell * disease count + d,
    // 0-1; 1 for diseases without a niche, 0 on ocean).
    std::vector<double> disease_niche;

    // ---- Resources and routes (M3)
    // Deposits of each good, cell * goods + g, 0-255 (0 = none; only goods
    // that come from deposits).
    std::vector<std::uint8_t> deposits;
    // Links goods can travel along: overland between neighbouring land cells,
    // down rivers, and by sea between coasts within kSeaReachKm. Each link is
    // listed once (a < b); trade_edge_ids[trade_edge_begin[c] ..
    // trade_edge_begin[c + 1]) are the links of cell c, in order.
    struct TradeEdge {
        CellId a = 0, b = 0;
        double distance_km = 0.0;
        double climb = 0.0;          // height difference, km (overland)
        std::uint8_t modes = 0;      // kTradeLand | kTradeRiver | kTradeSea
    };
    std::vector<TradeEdge> trade_edges;
    std::vector<std::uint32_t> trade_edge_begin;
    std::vector<std::uint32_t> trade_edge_ids;

    // Starting peoples: each land cell's forager group at the start of the run
    // (0 for ocean). Groups are contiguous regions; M2 turns them into
    // cultures and languages that drift apart.
    std::vector<std::uint32_t> initial_group;
    std::uint32_t group_count = 0;

    // Coarse grid for regional climate anomalies, and for each fine cell the
    // three nearest coarse cells with interpolation weights summing to 1.
    Grid climate_grid;
    std::vector<std::array<CellId, 3>> climate_cells;
    std::vector<std::array<double, 3>> climate_weights;

    // River corridors and coastal strips about as wide as a level-5 cell
    // (225 km), at any grid level: the share (0-1) of a cell's land within a
    // river's corridor, that river's flow (m^3/s), and the share within the
    // coastal strip. At level 5: 1 for river cells (their own flow) and for
    // cells on the coast, else 0. Derived, not part of the world hash.
    std::vector<double> river_share;
    std::vector<double> river_flow;
    std::vector<double> coast_share;

    // Each cell's hinterland: the cells within hinterland_steps() of it (the
    // cell first), covering about the area of a level-5 cell. Built with the
    // world; hinterland_ids[hinterland_begin[c] .. hinterland_begin[c + 1]).
    // Derived from the grid alone, so not part of the world hash.
    std::vector<std::uint32_t> hinterland_begin;
    // For each cell, bit t set if technique t's place (`where`) fits it
    // (technique_place_ok). Derived from the terrain; not hashed.
    std::vector<std::uint64_t> technique_places;
    std::vector<CellId> hinterland_ids;

    [[nodiscard]] std::size_t cell_count() const noexcept { return grid.cell_count(); }

    // ---- Grid scale. The model is tuned at grid level 5 (cells of about
    // 50,000 km^2, 225 km apart). Rules that act cell by cell use these so
    // the same history unfolds at any level; at level 5 every factor is
    // exactly 1, so results there do not change.

    // Mean cell spacing relative to level 5: exactly 2^(5 - level).
    [[nodiscard]] double cell_scale() const noexcept {
        double k = 1.0;
        for (int l = grid.level(); l < 5; ++l) k *= 2.0;
        for (int l = grid.level(); l > 5; --l) k *= 0.5;
        return k;
    }
    // A chance a year of passing something to a neighbour, for the same pace
    // in km a year at any level.
    [[nodiscard]] double per_step(double p) const noexcept { return std::min(1.0, p / cell_scale()); }
    // A rate a year of moving people to a neighbour, for the same spread
    // (diffusion) at any level.
    [[nodiscard]] double per_step_diffusion(double p) const noexcept {
        return p / (cell_scale() * cell_scale());
    }
    // A chance per cell of an event, for the same number per area at any level.
    [[nodiscard]] double per_area(double p) const noexcept { return p * cell_scale() * cell_scale(); }
    // A count of level-5 cells as a count of cells at this level.
    [[nodiscard]] std::size_t cells_for(std::size_t level5_cells) const noexcept {
        const double k2 = cell_scale() * cell_scale();
        return static_cast<std::size_t>(static_cast<double>(level5_cells) / k2 + 0.5);
    }
    // Grid steps that span one level-5 cell spacing: 2^(level - 5), at least 1.
    [[nodiscard]] int reach_steps() const noexcept {
        int s = 1;
        for (int l = grid.level(); l > 5; --l) s *= 2;
        return s;
    }
    // Steps of a hinterland about a level-5 cell in area: 0 at level 5 and
    // coarser, else 2^(level - 6).
    [[nodiscard]] int hinterland_steps() const noexcept { return reach_steps() / 2; }
    // Calls f(cell) for each cell of c's hinterland (just c when none is built).
    template <class F>
    void for_hinterland(CellId c, F&& f) const {
        if (hinterland_begin.size() != cell_count() + 1) {
            f(c);
            return;
        }
        for (std::uint32_t i = hinterland_begin[c]; i < hinterland_begin[c + 1]; ++i) f(hinterland_ids[i]);
    }
    [[nodiscard]] std::size_t species_count() const noexcept {
        return species_mask.empty() ? 0 : species_abundance.size() / species_mask.size();
    }
    [[nodiscard]] double abundance(CellId c, std::size_t species) const noexcept {
        return species_abundance[c * species_count() + species] / 255.0;
    }
    [[nodiscard]] double cultivated(CellId c, std::size_t species) const noexcept {
        return cultivated_suitability[c * species_count() + species] / 255.0;
    }
    [[nodiscard]] double watered(CellId c, std::size_t species) const noexcept {
        return watered_suitability.empty() ? cultivated(c, species)
                                           : watered_suitability[c * species_count() + species] / 255.0;
    }
    // How well a domesticate can be raised anywhere in the cell: on its
    // rain-fed land, or on its floodplain where it has one.
    [[nodiscard]] double raisable(CellId c, std::size_t species) const noexcept {
        const bool floodplain = !floodplain_share.empty() && floodplain_share[c] > 0.0;
        return floodplain ? std::max(cultivated(c, species), watered(c, species)) : cultivated(c, species);
    }
    [[nodiscard]] double deposit(CellId c, std::size_t good) const noexcept {
        const std::size_t ng = content->goods.size();
        return deposits.size() == cell_count() * ng ? deposits[c * ng + good] / 255.0 : 0.0;
    }
    [[nodiscard]] std::span<const std::uint32_t> trade_links(CellId c) const noexcept {
        return {trade_edge_ids.data() + trade_edge_begin[c], trade_edge_ids.data() + trade_edge_begin[c + 1]};
    }
    [[nodiscard]] CellId trade_other(std::uint32_t edge, CellId c) const noexcept {
        const TradeEdge& e = trade_edges[edge];
        return e.a == c ? e.b : e.a;
    }

    // Baseline climate of month m (0 = January) at cell c. Months follow the
    // cell's hemisphere: July is summer in the north, January in the south.
    [[nodiscard]] double month_temperature_c(CellId c, int m) const noexcept;
    [[nodiscard]] double month_precipitation_mm(CellId c, int m) const noexcept;
    [[nodiscard]] double coldest_month_c(CellId c) const noexcept {
        return temperature_c[c] - 0.5 * temperature_range_c[c];
    }
    [[nodiscard]] double warmest_month_c(CellId c) const noexcept {
        return temperature_c[c] + 0.5 * temperature_range_c[c];
    }

    // Hash of the grids, content and every field: identifies a world exactly.
    [[nodiscard]] std::uint64_t hash() const noexcept;
};

// Generate an Earth-scale test world from the seed and a content pack:
// fractal-noise continents; temperature by latitude and altitude; rainfall
// carried by prevailing winds (trade winds, westerlies, polar easterlies)
// with rain shadows behind mountains; seasons by latitude and continentality;
// biomes; rivers draining to the sea; soils; native ranges of wild species;
// deposits of salt, stone, ores and gold; and the links trade can follow.
//
// A stand-in for real data: the full procedural generator (plates, erosion,
// climate spin-up) and the Earth importer come in M4.
World generate_test_world(const WorldParams& params, const Content& content = default_content());

inline constexpr std::uint8_t kTradeLand = 1, kTradeRiver = 2, kTradeSea = 4;
// Longest sea crossing between coasts in a year's sailing season, km.
inline constexpr double kSeaReachKm = 500.0;

// Whether a deposit can lie in a cell.
bool good_place_ok(const World& world, CellId cell, GoodPlace place);

inline constexpr int kClimateGridLevel = 2;  // 162 coarse cells, ~3.1 million km^2 each

// A cell carries a river when it drains a basin of at least this size with
// at least this much flow. Basin size, unlike flow, does not depend on how
// wet a single cell is, so the river network looks alike at every grid level.
inline constexpr double kRiverMinBasinKm2 = 150000.0;
inline constexpr double kRiverMinDischarge = 300.0;  // m^3/s
// Fish and shellfish for foragers on a coast: kCoastFood, plus kColdCoastFood
// on cool, nutrient-rich mid- and high-latitude seas (salmon, seals: the
// densest foragers known, on the Pacific Northwest coast), times the cell's
// coastal share; with rivers' fish, at most kMaxAquatic.
inline constexpr double kCoastFood = 0.3;
inline constexpr double kColdCoastFood = 0.5;
inline constexpr double kMaxAquatic = 1.2;
// Floodplain width per square root of flow (m^3/s), km, and its bounds.
inline constexpr double kFloodplainKmPerRootFlow = 0.5;
inline constexpr double kFloodplainMinKm = 2.0;
inline constexpr double kFloodplainMaxKm = 60.0;
// A river crossing dry land loses kRiverEvaporation of its flow per 100 km
// per 1,000 mm of yearly water deficit (evaporation beyond rain): the Nile
// loses much of its water crossing the Sahara.
inline constexpr double kRiverEvaporation = 0.03;

[[nodiscard]] inline bool is_river(const World& w, CellId c) noexcept {
    return w.is_land[c] && w.basin_km2[c] >= kRiverMinBasinKm2 && w.discharge_m3s[c] >= kRiverMinDischarge;
}

// A river's effect on a cell, f(flow): the larger of its own flow's, and the
// corridor river's scaled by the cell's share of the corridor.
template <class F>
[[nodiscard]] double river_effect(const World& w, CellId c, F&& f) {
    double v = f(w.discharge_m3s[c]);
    if (c < w.river_share.size() && w.river_share[c] > 0.0) v = std::max(v, w.river_share[c] * f(w.river_flow[c]));
    return v;
}
// Share of a cell's land along a river (is_river at level 5).
[[nodiscard]] inline double river_share(const World& w, CellId c) noexcept {
    if (c < w.river_share.size()) return w.river_share[c];
    return is_river(w, c) ? 1.0 : 0.0;
}
// Share of a cell's land in the coastal strip (coast_distance 1 at level 5).
[[nodiscard]] inline double coast_share(const World& w, CellId c) noexcept {
    if (c < w.coast_share.size()) return w.coast_share[c];
    return w.coast_distance[c] == 1 ? 1.0 : 0.0;
}

}  // namespace histsim
