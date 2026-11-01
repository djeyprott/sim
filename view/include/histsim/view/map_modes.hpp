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
#include "histsim/view/raster.hpp"

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
    std::string group;        // "Land", "People", "Culture", "Economy" or "Power"
};

// All modes, in the order the viewer lists them.
const std::vector<MapModeInfo>& map_modes();

// Throws std::invalid_argument for an unknown id.
const MapModeInfo& map_mode(std::string_view id);

// One color per cell for the given mode.
void color_cells(const Simulation& sim, std::string_view mode_id, std::vector<Rgba>& out);

// The fixed part of a mode's legend.
Legend legend(std::string_view mode_id);
// The legend for the current state: for language and culture modes it lists
// the largest groups now living.
Legend legend(const Simulation& sim, std::string_view mode_id);

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
    std::string section;  // "Place", "People", "Language", ...: rows group under it
};
std::vector<CellField> describe_cell(const Simulation& sim, CellId cell);

// "Yiyi < Proto-Yiyi < Proto-Sivaw": a language and its ancestors.
std::string language_lineage(const Simulation& sim, std::uint32_t language);

// "water du, fire khu, ...": a few core words of a language.
std::string sample_words(const Simulation& sim, std::uint32_t language, std::size_t count);

// Colors used for languages, families and cultures on the map, for UI that
// lists them.
Rgba language_color(const SimState& state, std::uint32_t language);
Rgba language_family_color(const SimState& state, std::uint32_t language);
Rgba culture_color(const SimState& state, std::uint32_t culture);
Rgba polity_color(const SimState& state, std::uint32_t polity);

// Names of the wild species native to a cell.
std::vector<std::string> native_species(const Simulation& sim, CellId cell);

// The links of today's trade routes, for RenderOptions::routes.
std::vector<RouteLine> route_lines(const Simulation& sim);

// Folk migrations of the last `years` years, as arrows (strength by size).
std::vector<RouteLine> migration_lines(const Simulation& sim, std::int64_t years = 50);

// A name to write over the map: a state, empire or other large realm, at
// the middle of its land. u and v place it on the flat map (0-1 from the
// left at 180 W and from the top at the north pole), which the globe shares.
struct MapLabel {
    std::string name;
    double u = 0.0, v = 0.0;
    double area_km2 = 0.0;
    double capital_u = 0.0, capital_v = 0.0;  // where its capital is
    std::string level;  // "chiefdom", "state", "empire", "confederation"
    std::uint32_t polity = 0;
};
// Labels for living states and empires, and other polities of at least
// kLabelMinAreaKm2 (largest first, at most kMaxLabels).
inline constexpr double kLabelMinAreaKm2 = 400000.0;
inline constexpr std::size_t kMaxLabels = 60;
std::vector<MapLabel> polity_labels(const Simulation& sim);

// Capitals to mark in the polity, stability and realm modes (empty for
// other modes): in polity and stability, every living polity's capital,
// larger for states; in realms, the seats of states, empires and nomad
// confederations that answer to no one large and gold, other independent
// chiefdoms' as in polity mode, subjects' small.
std::vector<MapMarker> capital_markers(const Simulation& sim, std::string_view mode_id);

// For modes that group cells (language, language family, culture,
// religion, polity, realm, stability): marks the cells in the same group as
// `cell` (its main language, its faith, its ruler, ...): kFocusMain where it
// is the main group, and for languages, families and cultures 1-254 by the
// share of the people where it is a minority (0 elsewhere). `group_color`,
// if given, gets the group's map color. Returns false and leaves `out`
// empty for other modes, or when the cell has no group.
bool same_group_cells(const Simulation& sim, std::string_view mode_id, CellId cell, std::vector<std::uint8_t>& out,
                      Rgba* group_color = nullptr);

// Neutral colors shared by modes and overlays.
Rgba ocean_neutral();
Rgba coastline_color();

}  // namespace histsim::view
