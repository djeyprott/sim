#include "histsim/view/map_modes.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace histsim::view {

namespace {

// Palettes. Magnitudes use one hue, light to dark; temperature uses a
// blue-red diverging scale with a neutral gray at 0 °C; terrain uses a muted
// conventional hypsometric tint. Ocean and empty land are neutral grays so
// they never read as data.
const Rgba kOceanNeutral = hex("#cfcec8");
const Rgba kEmptyLand = hex("#f4f3ef");
const Rgba kCoastline = hex("#52514e");
const Rgba kIce = hex("#e8edf1");
constexpr double kIceBelowC = -10.0;  // land colder than this shows as ice on the terrain map

const ColorScale& terrain_ocean() {
    static const ColorScale s({{-5200, hex("#3f6283")}, {-2000, hex("#6f93b3")}, {-200, hex("#b3cbe0")}});
    return s;
}
const ColorScale& terrain_land() {
    static const ColorScale s({{0, hex("#9dbf8b")},
                               {400, hex("#c4d29b")},
                               {1200, hex("#ddc893")},
                               {2500, hex("#b8916a")},
                               {4000, hex("#9a8b80")},
                               {5500, hex("#f2f1ed")}});
    return s;
}
const ColorScale& temperature_scale() {
    static const ColorScale s({{-30, hex("#184f95")},
                               {-15, hex("#6da7ec")},
                               {0, hex("#f0efec")},
                               {15, hex("#ee9a99")},
                               {30, hex("#b52a2a")}});
    return s;
}
const ColorScale& rainfall_scale() {
    static const ColorScale s({{0, hex("#eef4fc")},
                               {250, hex("#9ec5f4")},
                               {750, hex("#3987e5")},
                               {1500, hex("#1c5cab")},
                               {2500, hex("#0d366b")}});
    return s;
}
const ColorScale& capacity_scale() {
    static const ColorScale s({{0, hex("#e3f4ec")},
                               {0.05, hex("#9cdcc3")},
                               {0.10, hex("#1baf7a")},
                               {0.15, hex("#147e58")},
                               {0.20, hex("#0b4a33")}});
    return s;
}
const ColorScale& density_scale() {
    // Log scale: forager densities sit around 0.01-0.2 per km^2; farming and
    // cities will need the scale extended upward.
    static const ColorScale s({{0.001, hex("#fde7dc")},
                               {0.005, hex("#f7b89a")},
                               {0.02, hex("#eb6834")},
                               {0.1, hex("#b5481d")},
                               {1.0, hex("#6e2a10")}},
                              /*log_scale=*/true);
    return s;
}

const std::vector<MapModeInfo> kModes = {
    {"terrain", "Terrain", "Elevation above and below sea level"},
    {"temperature", "Temperature", "Mean annual temperature"},
    {"rainfall", "Rainfall", "Mean annual precipitation on land"},
    {"capacity", "Forager capacity", "People per square km the land supports by foraging"},
    {"population", "Population", "People per square km"},
};

std::string fmt(const char* format, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

// Thousands separators for whole numbers.
std::string with_commas(long long n) {
    std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return n < 0 ? "-" + out : out;
}

std::vector<LegendEntry> ramp_entries(const ColorScale& s, const char* format) {
    std::vector<LegendEntry> out;
    for (const auto& stop : s.stops()) {
        out.push_back({fmt(format, stop.value), stop.color});
    }
    return out;
}

double density(const World& w, const SimState& s, CellId c) {
    return static_cast<double>(s.population.read()[c]) / w.grid.area_km2(c);
}

}  // namespace

Rgba ocean_neutral() { return kOceanNeutral; }
Rgba coastline_color() { return kCoastline; }

const std::vector<MapModeInfo>& map_modes() { return kModes; }

const MapModeInfo& map_mode(std::string_view id) {
    for (const auto& m : kModes) {
        if (m.id == id) return m;
    }
    throw std::invalid_argument("unknown map mode: " + std::string(id));
}

void color_cells(const Simulation& sim, std::string_view mode_id, std::vector<Rgba>& out) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    const std::size_t n = w.cell_count();
    out.resize(n);
    const std::string id(map_mode(mode_id).id);

    for (CellId c = 0; c < n; ++c) {
        const bool land = w.is_land[c] != 0;
        if (id == "terrain") {
            if (!land) {
                out[c] = terrain_ocean()(w.elevation_m[c]);
            } else if (w.temperature_c[c] < kIceBelowC) {
                out[c] = kIce;
            } else {
                out[c] = terrain_land()(w.elevation_m[c]);
            }
        } else if (id == "temperature") {
            out[c] = temperature_scale()(w.temperature_c[c]);
        } else if (!land) {
            out[c] = kOceanNeutral;  // remaining modes are land-only
        } else if (id == "rainfall") {
            out[c] = rainfall_scale()(w.precipitation_mm[c]);
        } else if (id == "capacity") {
            out[c] = capacity_scale()(w.forager_capacity[c] / w.grid.area_km2(c));
        } else {  // population
            out[c] = s.population.read()[c] == 0 ? kEmptyLand : density_scale()(density(w, s, c));
        }
    }
}

Legend legend(std::string_view mode_id) {
    const std::string id(map_mode(mode_id).id);
    Legend l;
    if (id == "terrain") {
        l.title = "Elevation (m)";
        l.ramp = ramp_entries(terrain_ocean(), "%.0f");
        const auto land = ramp_entries(terrain_land(), "%.0f");
        l.ramp.insert(l.ramp.end(), land.begin(), land.end());
        l.swatches = {{"Ice (below -10 °C)", kIce}};
    } else if (id == "temperature") {
        l.title = "Mean annual temperature (°C)";
        l.ramp = ramp_entries(temperature_scale(), "%.0f");
    } else if (id == "rainfall") {
        l.title = "Annual rainfall (mm)";
        l.ramp = ramp_entries(rainfall_scale(), "%.0f");
        l.swatches = {{"Ocean", kOceanNeutral}};
    } else if (id == "capacity") {
        l.title = "Forager capacity (people per km²)";
        l.ramp = ramp_entries(capacity_scale(), "%.2f");
        l.swatches = {{"Ocean", kOceanNeutral}};
    } else {
        l.title = "Population density (people per km², log scale)";
        l.ramp = ramp_entries(density_scale(), "%g");
        l.swatches = {{"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    }
    return l;
}

std::vector<CellField> describe_cell(const Simulation& sim, CellId c) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    if (c >= w.cell_count()) {
        return {};
    }
    const double lat = w.grid.latitude_deg_display(c);
    const double lon = w.grid.longitude_deg_display(c);
    std::vector<CellField> f;
    f.push_back({"Cell", with_commas(static_cast<long long>(c))});
    f.push_back({"Location", fmt("%.1f°", std::fabs(lat)) + (lat >= 0 ? " N, " : " S, ") +
                                 fmt("%.1f°", std::fabs(lon)) + (lon >= 0 ? " E" : " W")});
    f.push_back({"Area", with_commas(std::llround(w.grid.area_km2(c))) + " km²"});
    const bool land = w.is_land[c] != 0;
    f.push_back({"Surface", land ? "Land" : "Ocean"});
    f.push_back({land ? "Elevation" : "Depth",
                 with_commas(std::llround(std::fabs(w.elevation_m[c]))) + " m"});
    f.push_back({"Temperature", fmt("%.1f °C", w.temperature_c[c])});
    if (land) {
        f.push_back({"Rainfall", with_commas(std::llround(w.precipitation_mm[c])) + " mm/yr"});
        f.push_back({"Coast distance", with_commas(static_cast<long long>(w.coast_distance[c])) + " cells"});
        f.push_back({"Forager capacity", with_commas(std::llround(w.forager_capacity[c])) + " people"});
        f.push_back({"Population", with_commas(static_cast<long long>(s.population.read()[c])) + " people"});
        f.push_back({"Density", fmt("%.4f per km²", density(w, s, c))});
    }
    return f;
}

}  // namespace histsim::view
