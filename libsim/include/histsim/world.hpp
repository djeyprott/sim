// Static world: terrain, baseline climate and the starting map of peoples.
//
// Nothing here changes during a run. The climate and environment systems
// write this year's conditions into SimState, as departures from the
// baselines below.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "histsim/grid.hpp"

namespace histsim {

struct WorldParams {
    std::uint64_t seed = 1;
    int grid_level = 5;
    double ocean_fraction = 0.70;  // share of cells below sea level
};

// Forager carrying capacity per km^2 for a given climate. Warm, wet land
// supports up to ~0.2 people/km^2; ice and desert support none. Used for the
// baseline at world generation and every year by the environment system.
double forager_density(double temperature_c, double precipitation_mm);

struct World {
    WorldParams params;
    Grid grid;

    // Per-cell fields, indexed by CellId.
    std::vector<double> elevation_m;            // relative to sea level; negative = ocean depth
    std::vector<std::uint8_t> is_land;          // 1 = land, 0 = ocean
    std::vector<std::uint32_t> coast_distance;  // steps to nearest ocean cell (0 for ocean)
    std::vector<double> temperature_c;          // baseline mean annual temperature
    std::vector<double> precipitation_mm;       // baseline mean annual precipitation
    std::vector<double> forager_capacity;       // baseline people supported by foraging

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

    // Hash of the grids and every field: identifies a world exactly.
    [[nodiscard]] std::uint64_t hash() const noexcept;
};

// Generate a simple Earth-scale test world from the seed: fractal-noise
// continents, a latitude- and altitude-driven temperature field, rainfall that
// falls off inland and in the subtropics, forager capacity and starting groups.
//
// This is a stand-in so the core can be built and tested without real data.
// The full procedural generator (plates, erosion, climate spin-up) and the
// Earth importer come in M4.
World generate_test_world(const WorldParams& params);

inline constexpr int kClimateGridLevel = 2;  // 162 coarse cells, ~3.1 million km^2 each

}  // namespace histsim
