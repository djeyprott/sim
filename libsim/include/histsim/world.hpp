// Static world: terrain, baseline climate and seasons, biomes, rivers, soils,
// wild species and the starting map of peoples.
//
// Nothing here changes during a run. The climate and environment systems
// write this year's conditions into SimState, as departures from the
// baselines below.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "histsim/content.hpp"
#include "histsim/grid.hpp"

namespace histsim {

struct WorldParams {
    std::uint64_t seed = 1;
    int grid_level = 5;
    double ocean_fraction = 0.70;  // share of cells below sea level
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

// Forager carrying capacity per km^2 for a given climate. Warm, wet land
// supports up to ~0.2 people/km^2; ice and desert support none. Used for the
// baseline at world generation and every year by the environment system.
double forager_density(double temperature_c, double precipitation_mm);

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

    [[nodiscard]] std::size_t cell_count() const noexcept { return grid.cell_count(); }
    [[nodiscard]] std::size_t species_count() const noexcept {
        return species_mask.empty() ? 0 : species_abundance.size() / species_mask.size();
    }
    [[nodiscard]] double abundance(CellId c, std::size_t species) const noexcept {
        return species_abundance[c * species_count() + species] / 255.0;
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
// biomes; rivers draining to the sea; soils; and native ranges of wild
// species.
//
// A stand-in for real data: the full procedural generator (plates, erosion,
// climate spin-up) and the Earth importer come in M4.
World generate_test_world(const WorldParams& params, const Content& content = default_content());

inline constexpr int kClimateGridLevel = 2;  // 162 coarse cells, ~3.1 million km^2 each

// A cell carries a river when it drains a basin of at least this size with
// at least this much flow. Basin size, unlike flow, does not depend on how
// wet a single cell is, so the river network looks alike at every grid level.
inline constexpr double kRiverMinBasinKm2 = 150000.0;
inline constexpr double kRiverMinDischarge = 300.0;  // m^3/s

[[nodiscard]] inline bool is_river(const World& w, CellId c) noexcept {
    return w.is_land[c] && w.basin_km2[c] >= kRiverMinBasinKm2 && w.discharge_m3s[c] >= kRiverMinDischarge;
}

}  // namespace histsim
