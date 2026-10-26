// Static physical world: terrain and a fixed baseline climate per cell.
//
// In M0 the world does not change during a run. Later milestones move climate
// and environment into dynamic state (see state.hpp).
#pragma once

#include <cstdint>
#include <vector>

#include "histsim/grid.hpp"

namespace histsim {

struct WorldParams {
    std::uint64_t seed = 1;
    int grid_level = 5;
    double ocean_fraction = 0.70;  // share of cells below sea level
};

struct World {
    WorldParams params;
    Grid grid;

    // Per-cell fields, indexed by CellId.
    std::vector<double> elevation_m;       // relative to sea level; negative = ocean depth
    std::vector<std::uint8_t> is_land;     // 1 = land, 0 = ocean
    std::vector<std::uint32_t> coast_distance;  // steps to nearest ocean cell (0 for ocean)
    std::vector<double> temperature_c;     // mean annual
    std::vector<double> precipitation_mm;  // mean annual
    std::vector<double> forager_capacity;  // people the cell supports by foraging

    [[nodiscard]] std::size_t cell_count() const noexcept { return grid.cell_count(); }

    // Hash of the grid and every field: identifies a world exactly.
    [[nodiscard]] std::uint64_t hash() const noexcept;
};

// Generate a simple Earth-scale test world from the seed: fractal-noise
// continents, a latitude- and altitude-driven temperature field, rainfall that
// falls off inland and in the subtropics, and a forager carrying capacity.
//
// This is a stand-in so the core can be built and tested without real data.
// The full procedural generator (plates, erosion, climate spin-up) and the
// Earth importer come in M4.
World generate_test_world(const WorldParams& params);

}  // namespace histsim
