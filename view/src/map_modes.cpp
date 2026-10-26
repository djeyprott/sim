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

const ColorScale& anomaly_scale() {
    static const ColorScale s({{-3, hex("#184f95")},
                               {-1.5, hex("#6da7ec")},
                               {0, hex("#f0efec")},
                               {1.5, hex("#ee9a99")},
                               {3, hex("#b52a2a")}});
    return s;
}
const ColorScale& game_scale() {  // violet, light to dark
    static const ColorScale s({{0, hex("#ece9fb")},
                               {0.25, hex("#c2b9f2")},
                               {0.5, hex("#9587e6")},
                               {0.75, hex("#6250d6")},
                               {1.0, hex("#2f2475")}});
    return s;
}
const ColorScale& crowding_scale() {  // room to grow (blue) - at capacity (gray) - overcrowded (red)
    static const ColorScale s({{0, hex("#184f95")},
                               {0.5, hex("#6da7ec")},
                               {1, hex("#f0efec")},
                               {1.5, hex("#ee9a99")},
                               {2, hex("#b52a2a")}});
    return s;
}
const ColorScale& age_scale() {  // magenta, light to dark
    static const ColorScale s({{12, hex("#fbe4ed")},
                               {17, hex("#f2b0c8")},
                               {22, hex("#e87ba4")},
                               {27, hex("#b84576")},
                               {32, hex("#6e1c40")}});
    return s;
}

const std::vector<MapModeInfo> kModes = {
    {"terrain", "Terrain", "Elevation above and below sea level"},
    {"temperature", "Temperature", "This year's mean temperature"},
    {"temp_anomaly", "Temp. anomaly", "This year's temperature minus the long-run baseline"},
    {"rainfall", "Rainfall", "This year's precipitation on land"},
    {"capacity", "Forager capacity", "People per square km the land can feed by foraging this year"},
    {"game", "Game", "Wild food abundance relative to undisturbed land"},
    {"population", "Population", "People per square km"},
    {"crowding", "Crowding", "Population relative to what the land can feed"},
    {"median_age", "Median age", "Median age of the people living in each cell"},
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
    return static_cast<double>(s.cell_population[c]) / w.grid.area_km2(c);
}

double crowding(const SimState& s, CellId c) {
    const double k = s.env.capacity[c];
    return k > 0.0 ? static_cast<double>(s.cell_population[c]) / k : 0.0;
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

Pyramid cell_pyramid(const Simulation& sim, CellId cell) {
    Pyramid p;
    if (cell >= sim.world().cell_count()) return p;
    for (const Cohort& c : sim.state().cohorts_in(cell)) {
        for (int b = 0; b < kAgeBins; ++b) {
            p.female[b] += c.female[b];
            p.male[b] += c.male[b];
        }
    }
    return p;
}

double median_age(const Pyramid& p) {
    std::int64_t total = 0;
    for (int b = 0; b < kAgeBins; ++b) total += p.female[b] + p.male[b];
    if (total == 0) return 0.0;
    const double half = 0.5 * static_cast<double>(total);
    double below = 0.0;
    for (int b = 0; b < kAgeBins; ++b) {
        const double in_bin = static_cast<double>(p.female[b] + p.male[b]);
        if (below + in_bin >= half && in_bin > 0.0) {
            return kYearsPerAgeBin * (b + (half - below) / in_bin);
        }
        below += in_bin;
    }
    return kYearsPerAgeBin * kAgeBins;
}

void color_cells(const Simulation& sim, std::string_view mode_id, std::vector<Rgba>& out) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    const std::size_t n = w.cell_count();
    out.resize(n);
    const std::string id(map_mode(mode_id).id);

    for (CellId c = 0; c < n; ++c) {
        const bool land = w.is_land[c] != 0;
        const bool inhabited = s.cell_population[c] > 0;
        if (id == "terrain") {
            if (!land) {
                out[c] = terrain_ocean()(w.elevation_m[c]);
            } else if (s.env.temperature_c[c] < kIceBelowC) {
                out[c] = kIce;
            } else {
                out[c] = terrain_land()(w.elevation_m[c]);
            }
        } else if (id == "temperature") {
            out[c] = temperature_scale()(s.env.temperature_c[c]);
        } else if (id == "temp_anomaly") {
            out[c] = anomaly_scale()(s.env.temperature_c[c] - w.temperature_c[c]);
        } else if (!land) {
            out[c] = kOceanNeutral;  // remaining modes are land-only
        } else if (id == "rainfall") {
            out[c] = rainfall_scale()(s.env.precipitation_mm[c]);
        } else if (id == "capacity") {
            out[c] = capacity_scale()(s.env.capacity[c] / w.grid.area_km2(c));
        } else if (id == "game") {
            out[c] = game_scale()(s.env.game[c]);
        } else if (id == "population") {
            out[c] = inhabited ? density_scale()(density(w, s, c)) : kEmptyLand;
        } else if (id == "crowding") {
            out[c] = inhabited ? crowding_scale()(crowding(s, c)) : kEmptyLand;
        } else {  // median_age
            out[c] = inhabited ? age_scale()(median_age(cell_pyramid(sim, c))) : kEmptyLand;
        }
    }
}

Legend legend(std::string_view mode_id) {
    const std::string id(map_mode(mode_id).id);
    Legend l;
    const std::vector<LegendEntry> ocean = {{"Ocean", kOceanNeutral}};
    const std::vector<LegendEntry> empty_and_ocean = {{"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    if (id == "terrain") {
        l.title = "Elevation (m)";
        l.ramp = ramp_entries(terrain_ocean(), "%.0f");
        const auto land = ramp_entries(terrain_land(), "%.0f");
        l.ramp.insert(l.ramp.end(), land.begin(), land.end());
        l.swatches = {{"Ice (below -10 °C this year)", kIce}};
    } else if (id == "temperature") {
        l.title = "Mean temperature this year (°C)";
        l.ramp = ramp_entries(temperature_scale(), "%.0f");
    } else if (id == "temp_anomaly") {
        l.title = "Temperature vs. baseline (°C)";
        l.ramp = ramp_entries(anomaly_scale(), "%+.1f");
        l.ramp[2].label = "0";
    } else if (id == "rainfall") {
        l.title = "Rainfall this year (mm)";
        l.ramp = ramp_entries(rainfall_scale(), "%.0f");
        l.swatches = ocean;
    } else if (id == "capacity") {
        l.title = "Forager capacity this year (people per km²)";
        l.ramp = ramp_entries(capacity_scale(), "%.2f");
        l.swatches = ocean;
    } else if (id == "game") {
        l.title = "Game abundance (share of undisturbed level)";
        l.ramp = {{"0%", game_scale()(0)}, {"25%", game_scale()(0.25)}, {"50%", game_scale()(0.5)},
                  {"75%", game_scale()(0.75)}, {"100%", game_scale()(1.0)}};
        l.swatches = ocean;
    } else if (id == "population") {
        l.title = "Population density (people per km², log scale)";
        l.ramp = ramp_entries(density_scale(), "%g");
        l.swatches = empty_and_ocean;
    } else if (id == "crowding") {
        l.title = "Population ÷ capacity (1 = at capacity)";
        l.ramp = ramp_entries(crowding_scale(), "%g");
        l.swatches = empty_and_ocean;
    } else {
        l.title = "Median age (years)";
        l.ramp = ramp_entries(age_scale(), "%.0f");
        l.swatches = empty_and_ocean;
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
    f.push_back({land ? "Elevation" : "Depth", with_commas(std::llround(std::fabs(w.elevation_m[c]))) + " m"});
    const double anomaly = s.env.temperature_c[c] - w.temperature_c[c];
    f.push_back({"Temperature", fmt("%.1f °C", s.env.temperature_c[c]) + fmt(" (%+.1f)", anomaly)});
    if (!land) return f;

    f.push_back({"Rainfall", with_commas(std::llround(s.env.precipitation_mm[c])) + " mm" +
                                 fmt(" (%+.0f%%)", 100.0 * (s.env.precipitation_mm[c] / std::max(1.0, w.precipitation_mm[c]) - 1.0))});
    f.push_back({"Game", fmt("%.0f%%", 100.0 * s.env.game[c])});
    f.push_back({"Capacity", with_commas(std::llround(s.env.capacity[c])) + " people"});
    f.push_back({"Population", with_commas(static_cast<long long>(s.cell_population[c])) + " people"});
    if (s.cell_population[c] == 0) return f;

    f.push_back({"Density", fmt("%.3f per km²", density(w, s, c))});
    f.push_back({"Crowding", fmt("%.2f of capacity", crowding(s, c))});
    const Pyramid p = cell_pyramid(sim, c);
    f.push_back({"Median age", fmt("%.1f years", median_age(p))});
    std::int64_t children = 0, total = 0;
    for (int b = 0; b < kAgeBins; ++b) {
        total += p.female[b] + p.male[b];
        if (b < 3) children += p.female[b] + p.male[b];
    }
    f.push_back({"Under 15", fmt("%.0f%%", 100.0 * static_cast<double>(children) / static_cast<double>(total))});
    const auto groups = s.cohorts_in(c);
    std::uint32_t main_group = 0;
    std::int64_t main_size = -1;
    for (const Cohort& co : groups) {
        if (co.total() > main_size) {
            main_size = co.total();
            main_group = co.culture;
        }
    }
    f.push_back({"Groups", with_commas(static_cast<long long>(groups.size())) + " (largest: group " +
                               with_commas(static_cast<long long>(main_group)) + ")"});
    return f;
}

}  // namespace histsim::view
