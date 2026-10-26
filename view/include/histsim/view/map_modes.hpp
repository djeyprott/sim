// Map modes: what each map view shows, how it is colored, and its legend.
//
// A map mode reads one quantity per cell from the world and the run state and
// turns it into a color. The Godot viewer and the `histsim map` command both
// use this, so a new system only has to add a mode here to appear everywhere.
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/view/color.hpp"

namespace histsim::view {

struct LegendEntry {
    std::string label;
    Rgba color;
};

struct Legend {
    std::string title;                // e.g. "Mean annual temperature (°C)"
    std::vector<LegendEntry> ramp;    // ordered stops of the continuous scale, low to high
    std::vector<LegendEntry> swatches;  // fixed categories, e.g. "Ocean", "Uninhabited"
};

struct MapModeInfo {
    std::string id;           // stable identifier, e.g. "population"
    std::string name;         // button label, e.g. "Population"
    std::string description;  // one line for tooltips
};

// All modes, in the order the viewer lists them.
const std::vector<MapModeInfo>& map_modes();

// Throws std::invalid_argument for an unknown id.
const MapModeInfo& map_mode(std::string_view id);

// One color per cell for the given mode.
void color_cells(const Simulation& sim, std::string_view mode_id, std::vector<Rgba>& out);

Legend legend(std::string_view mode_id);

// The age-sex pyramid of everyone in a cell (all cohorts summed).
struct Pyramid {
    std::array<std::int64_t, kAgeBins> female{};
    std::array<std::int64_t, kAgeBins> male{};
};
Pyramid cell_pyramid(const Simulation& sim, CellId cell);

// Median age in years, interpolated within the 5-year bin. 0 if empty.
double median_age(const Pyramid& p);

// Human-readable values for one cell, for hover panels.
struct CellField {
    std::string label;
    std::string value;
};
std::vector<CellField> describe_cell(const Simulation& sim, CellId cell);

// Neutral colors shared by modes and overlays.
Rgba ocean_neutral();
Rgba coastline_color();

}  // namespace histsim::view
