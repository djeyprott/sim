#include "histsim/view/map_modes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "histsim/language.hpp"

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
                               {0.075, hex("#9cdcc3")},
                               {0.15, hex("#1baf7a")},
                               {0.225, hex("#147e58")},
                               {0.30, hex("#0b4a33")}});
    return s;
}
const ColorScale& rain_season_scale() {  // winter rain (violet) - even (gray) - summer rain (green)
    static const ColorScale s({{-0.8, hex("#4a3aa7")},
                               {-0.4, hex("#9587e6")},
                               {0, hex("#f0efec")},
                               {0.4, hex("#6fbf73")},
                               {0.8, hex("#1f6b3a")}});
    return s;
}
const ColorScale& growing_scale() {  // olive green, light to dark
    static const ColorScale s({{0, hex("#f2f1e4")},
                               {3, hex("#d6dca0")},
                               {6, hex("#a3b85a")},
                               {9, hex("#6a8a2c")},
                               {12, hex("#36531a")}});
    return s;
}
const ColorScale& soil_scale() {  // brown, light to dark
    static const ColorScale s({{0, hex("#f5ede2")},
                               {0.25, hex("#e0c39d")},
                               {0.5, hex("#c1915b")},
                               {0.75, hex("#8f5f33")},
                               {1.0, hex("#5a3818")}});
    return s;
}
const ColorScale& species_scale() {  // teal, light to dark
    static const ColorScale s({{0, hex("#eef7f6")},
                               {2, hex("#b6e1dc")},
                               {4, hex("#6cbcb3")},
                               {6, hex("#2f8a82")},
                               {8, hex("#0f5753")}});
    return s;
}

// Conventional biome colors: forests green, grasslands yellow-green,
// drylands tan, ice white. Indexed by Biome.
const std::array<Rgba, static_cast<std::size_t>(Biome::Count)> kBiomeColors = {
    hex("#cfcec8"),  // Ocean (neutral)
    hex("#eef3f6"),  // Ice sheet
    hex("#b7c4b4"),  // Tundra
    hex("#4f7a5a"),  // Boreal forest
    hex("#3f8f4a"),  // Temperate forest
    hex("#78b158"),  // Warm temperate forest
    hex("#8f8a3a"),  // Mediterranean woodland
    hex("#d8d27a"),  // Temperate grassland
    hex("#d4a35a"),  // Dry shrubland
    hex("#efe1b5"),  // Desert
    hex("#b6c95a"),  // Tropical savanna
    hex("#1f6b3a"),  // Tropical rainforest
};
const ColorScale& farming_scale() {  // wheat gold, light to dark
    static const ColorScale s({{0, hex("#f6efd9")},
                               {0.25, hex("#e8d08a")},
                               {0.5, hex("#cfa640")},
                               {0.75, hex("#a07a1c")},
                               {1.0, hex("#654a0c")}});
    return s;
}
const ColorScale& domesticates_scale() {  // green, light to dark
    static const ColorScale s({{1, hex("#e4f2d9")},
                               {2, hex("#b5dc96")},
                               {4, hex("#7dbb53")},
                               {6, hex("#4d8a2a")},
                               {8, hex("#2a5513")}});
    return s;
}
const Rgba kForagers = hex("#dcd9cf");
const ColorScale& technology_scale() {  // slate blue, light to dark
    static const ColorScale s({{0, hex("#eceff7")},
                               {2, hex("#bcc6e3")},
                               {5, hex("#8394c9")},
                               {8, hex("#4f62a6")},
                               {12, hex("#26326a")}});
    return s;
}
const ColorScale& disease_scale() {  // deaths per 1,000 a year: plum, light to dark
    static const ColorScale s({{0, hex("#f6eef4")},
                               {1, hex("#e2bfd8")},
                               {3, hex("#c47fb0")},
                               {10, hex("#93407e")},
                               {30, hex("#55163f")}});
    return s;
}  // inhabited, no farming / nothing domesticated

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

// Order matters: keys 1-9 and 0 select the first ten.
const std::vector<MapModeInfo> kModes = {
    {"terrain", "Terrain", "Elevation above and below sea level"},
    {"biome", "Biome", "Vegetation zone from the climate of each month"},
    {"population", "Population", "People per square km"},
    {"crowding", "Crowding", "Population relative to what the land can feed"},
    {"temperature", "Temperature", "This year's mean temperature"},
    {"rainfall", "Rainfall", "This year's precipitation on land"},
    {"rain_season", "Rain season", "Whether rain falls in summer, in winter or all year"},
    {"growing", "Growing season", "Months warm and wet enough for plants to grow"},
    {"soil", "Soil", "Soil fertility"},
    {"species", "Wild species", "Number of native wild species that could later be domesticated"},
    {"capacity", "Forager capacity", "People per square km the land can feed by foraging this year"},
    {"game", "Game", "Wild food abundance relative to undisturbed land"},
    {"temp_anomaly", "Temp. anomaly", "This year's temperature minus the long-run baseline"},
    {"median_age", "Median age", "Median age of the people living in each cell"},
    {"language", "Language", "Main language spoken; related languages share a hue"},
    {"language_family", "Language family", "Family of the main language"},
    {"culture", "Culture", "Main culture; related cultures share a hue"},
    {"farming", "Farming", "Share of people's food from crops and herds"},
    {"domesticates", "Domesticates", "Domesticated crops and animals the people know"},
    {"disease", "Disease", "Deaths from disease this year, per 1,000 people"},
    {"technology", "Technology", "Number of techniques the people know"},
};

// ---- Colors for languages and cultures.
//
// Each family (or founding culture) gets a hue spread around the color wheel
// by the golden angle; each member varies it a little in hue and lightness,
// so a family reads as one block of related shades.

std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

double unit_hash(std::uint64_t x) { return static_cast<double>(mix(x) >> 11) * 0x1.0p-53; }

Rgba hsl(double h, double s, double l) {
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360.0;
    const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = l - c / 2.0;
    double r = 0, g = 0, b = 0;
    if (h < 60) r = c, g = x;
    else if (h < 120) r = x, g = c;
    else if (h < 180) g = c, b = x;
    else if (h < 240) g = x, b = c;
    else if (h < 300) r = x, b = c;
    else r = c, b = x;
    auto to8 = [](double v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return {to8(r + m), to8(g + m), to8(b + m), 255};
}

double family_hue(std::uint32_t root) { return std::fmod(static_cast<double>(root) * 137.50776, 360.0); }

Rgba group_color(std::uint32_t root, std::uint32_t id) {
    if (id == root) return hsl(family_hue(root), 0.55, 0.52);
    const double dh = (unit_hash(id * 2 + 1) - 0.5) * 36.0;
    const double dl = (unit_hash(id * 2 + 2) - 0.5) * 0.30;
    return hsl(family_hue(root) + dh, 0.55, 0.52 + dl);
}

// Main language and culture of a cell (most people; ties: the lower id).
struct MainPeople {
    std::uint32_t language = 0, culture = 0;
    std::int64_t language_pop = 0, culture_pop = 0;
};
MainPeople main_people(const SimState& s, CellId c) {
    MainPeople m;
    std::vector<std::pair<std::uint32_t, std::int64_t>> langs, cults;
    auto add = [](std::vector<std::pair<std::uint32_t, std::int64_t>>& v, std::uint32_t id, std::int64_t p) {
        for (auto& e : v) {
            if (e.first == id) {
                e.second += p;
                return;
            }
        }
        v.emplace_back(id, p);
    };
    for (const Cohort& co : s.cohorts_in(c)) {
        add(langs, co.language, co.total());
        add(cults, co.culture, co.total());
    }
    for (const auto& [id, p] : langs) {
        if (p > m.language_pop || (p == m.language_pop && id < m.language)) m.language = id, m.language_pop = p;
    }
    for (const auto& [id, p] : cults) {
        if (p > m.culture_pop || (p == m.culture_pop && id < m.culture)) m.culture = id, m.culture_pop = p;
    }
    return m;
}

std::string family_name(const SimState& s, std::uint32_t lang) {
    std::string name = s.languages[s.languages[lang].family].name;
    if (name.rfind("Proto-", 0) == 0) name = name.substr(6);
    return name;
}

int species_count(std::uint32_t mask) {
    int n = 0;
    for (; mask != 0; mask &= mask - 1) ++n;
    return n;
}

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

Rgba language_color(const SimState& s, std::uint32_t lang) {
    if (lang == 0 || lang >= s.languages.size()) return kEmptyLand;
    return group_color(s.languages[lang].family, lang);
}

Rgba language_family_color(const SimState& s, std::uint32_t lang) {
    if (lang == 0 || lang >= s.languages.size()) return kEmptyLand;
    const std::uint32_t f = s.languages[lang].family;
    return group_color(f, f);
}

Rgba culture_color(const SimState& s, std::uint32_t culture) {
    if (culture == 0 || culture >= s.cultures.size()) return kEmptyLand;
    // Offset so a culture and its founding language family differ in hue.
    return group_color(s.cultures[culture].root + 7919, culture + 7919);
}

namespace {
Rgba people_color(const SimState& s, const MainPeople& m, const std::string& id) {
    if (id == "language") return language_color(s, m.language);
    if (id == "language_family") return language_family_color(s, m.language);
    return culture_color(s, m.culture);
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
        } else if (id == "biome") {
            out[c] = kBiomeColors[w.biome[c]];
        } else if (!land) {
            out[c] = kOceanNeutral;  // remaining modes are land-only
        } else if (id == "rain_season") {
            out[c] = rain_season_scale()(w.rain_season[c]);
        } else if (id == "growing") {
            out[c] = growing_scale()(static_cast<double>(w.growing_months[c]));
        } else if (id == "soil") {
            out[c] = soil_scale()(w.soil_fertility[c]);
        } else if (id == "species") {
            out[c] = species_scale()(static_cast<double>(species_count(w.species_mask[c])));
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
        } else if (id == "farming") {
            const double f = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            out[c] = !inhabited ? kEmptyLand : (f < 0.01 ? kForagers : farming_scale()(f));
        } else if (id == "technology") {
            const std::uint64_t t = s.techniques.empty() ? 0 : s.techniques[c];
            int count = 0;
            for (std::uint64_t b = t; b != 0; b &= b - 1) ++count;
            out[c] = inhabited ? technology_scale()(count) : kEmptyLand;
        } else if (id == "disease") {
            const double per_1000 = s.disease_deaths.empty() || !inhabited
                                        ? 0.0
                                        : 1000.0 * s.disease_deaths[c] / static_cast<double>(s.cell_population[c]);
            out[c] = inhabited ? disease_scale()(per_1000) : kEmptyLand;
        } else if (id == "domesticates") {
            const std::uint32_t known = s.domesticates.empty() ? 0 : s.domesticates[c];
            out[c] = !inhabited ? kEmptyLand : (known == 0 ? kForagers : domesticates_scale()(species_count(known)));
        } else if (id == "language" || id == "language_family" || id == "culture") {
            out[c] = inhabited ? people_color(s, main_people(s, c), id) : kEmptyLand;
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
    } else if (id == "biome") {
        l.title = "Biome";
        for (std::size_t b = 1; b < kBiomeColors.size(); ++b) {
            l.swatches.push_back({biome_name(static_cast<Biome>(b)), kBiomeColors[b]});
        }
    } else if (id == "rain_season") {
        l.title = "When the rain falls";
        l.ramp = {{"Winter", rain_season_scale()(-0.8)}, {"", rain_season_scale()(-0.4)},
                  {"All year", rain_season_scale()(0)}, {"", rain_season_scale()(0.4)},
                  {"Summer", rain_season_scale()(0.8)}};
        l.swatches = ocean;
    } else if (id == "growing") {
        l.title = "Growing season (months)";
        l.ramp = ramp_entries(growing_scale(), "%.0f");
        l.swatches = ocean;
    } else if (id == "soil") {
        l.title = "Soil fertility";
        l.ramp = {{"Poor", soil_scale()(0)}, {"", soil_scale()(0.25)}, {"", soil_scale()(0.5)},
                  {"", soil_scale()(0.75)}, {"Rich", soil_scale()(1.0)}};
        l.swatches = ocean;
    } else if (id == "species") {
        l.title = "Native wild species that could be domesticated";
        l.ramp = ramp_entries(species_scale(), "%.0f");
        l.ramp.back().label = "8+";
        l.swatches = ocean;
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
    } else if (id == "farming") {
        l.title = "Share of food from farming";
        l.ramp = {{"1%", farming_scale()(0.0)}, {"25%", farming_scale()(0.25)}, {"50%", farming_scale()(0.5)},
                  {"75%", farming_scale()(0.75)}, {"100%", farming_scale()(1.0)}};
        l.swatches = {{"Foragers", kForagers}, {"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    } else if (id == "technology") {
        l.title = "Techniques known";
        l.ramp = ramp_entries(technology_scale(), "%.0f");
        l.ramp.back().label = "12+";
        l.swatches = empty_and_ocean;
    } else if (id == "disease") {
        l.title = "Deaths from disease this year (per 1,000 people)";
        l.ramp = ramp_entries(disease_scale(), "%.0f");
        l.ramp.back().label = "30+";
        l.swatches = empty_and_ocean;
    } else if (id == "domesticates") {
        l.title = "Domesticated species known";
        l.ramp = ramp_entries(domesticates_scale(), "%.0f");
        l.ramp.back().label = "8+";
        l.swatches = {{"None", kForagers}, {"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    } else if (id == "language") {
        l.title = "Main language";
        l.swatches = empty_and_ocean;
    } else if (id == "language_family") {
        l.title = "Language family";
        l.swatches = empty_and_ocean;
    } else if (id == "culture") {
        l.title = "Main culture";
        l.swatches = empty_and_ocean;
    } else {
        l.title = "Median age (years)";
        l.ramp = ramp_entries(age_scale(), "%.0f");
        l.swatches = empty_and_ocean;
    }
    return l;
}

Legend legend(const Simulation& sim, std::string_view mode_id) {
    Legend l = legend(mode_id);
    const std::string id(map_mode(mode_id).id);
    if (id != "language" && id != "language_family" && id != "culture") return l;
    const SimState& s = sim.state();
    // The largest groups by people, then a count of the rest.
    const bool culture = id == "culture";
    const bool family = id == "language_family";
    std::vector<std::int64_t> people(culture ? s.cultures.size() : s.languages.size(), 0);
    for (const Cohort& co : s.cohorts) {
        const std::uint32_t key = culture ? co.culture : (family ? s.languages[co.language].family : co.language);
        people[key] += co.total();
    }
    std::vector<std::uint32_t> order;
    for (std::uint32_t i = 1; i < people.size(); ++i) {
        if (people[i] > 0) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return people[a] > people[b]; });
    constexpr std::size_t kShown = 10;
    std::vector<LegendEntry> entries;
    for (std::size_t i = 0; i < order.size() && i < kShown; ++i) {
        const std::uint32_t k = order[i];
        std::string name = culture ? s.cultures[k].name : (family ? family_name(s, k) + " family" : s.languages[k].name);
        if (!culture && !family) name += " (" + family_name(s, k) + ")";
        const Rgba color = culture ? culture_color(s, k) : (family ? language_family_color(s, k) : language_color(s, k));
        entries.push_back({name + ": " + with_commas(people[k]), color});
    }
    if (order.size() > kShown) {
        const char* what = culture ? " other cultures" : (family ? " other families" : " other languages");
        entries.push_back({"+ " + with_commas(static_cast<long long>(order.size() - kShown)) + what, Rgba{0, 0, 0, 0}});
    }
    entries.insert(entries.end(), l.swatches.begin(), l.swatches.end());
    l.swatches = std::move(entries);
    l.title += " (" + with_commas(static_cast<long long>(order.size())) + " living)";
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

    f.push_back({"Biome", biome_name(static_cast<Biome>(w.biome[c]))});
    f.push_back({"Seasons", fmt("coldest month %.0f °C", w.coldest_month_c(c)) +
                                fmt(", warmest %.0f °C", w.warmest_month_c(c))});
    const double rs = w.rain_season[c];
    f.push_back({"Rain season", rs > 0.35 ? "Summer rain" : (rs < -0.35 ? "Winter rain (Mediterranean)" : "Rain all year")});
    f.push_back({"Growing season", with_commas(static_cast<long long>(w.growing_months[c])) + " months"});
    f.push_back({"Soil", fmt("%.0f%% fertility", 100.0 * w.soil_fertility[c])});
    if (is_river(w, c)) {
        f.push_back({"River", with_commas(std::llround(w.discharge_m3s[c])) + " m³/s"});
    }

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

    // Peoples.
    const Content& content = *w.content;
    const MainPeople m = main_people(s, c);
    const double cell_pop = static_cast<double>(s.cell_population[c]);
    const Language& lang = s.languages[m.language];
    f.push_back({"Language", lang.name + " (" + family_name(s, m.language) + " family)" +
                                 (m.language_pop < s.cell_population[c]
                                      ? fmt(", %.0f%%", 100.0 * static_cast<double>(m.language_pop) / cell_pop)
                                      : "")});
    {  // Other languages here, by speakers.
        std::vector<std::pair<std::uint32_t, std::int64_t>> others;
        for (const Cohort& co : s.cohorts_in(c)) {
            if (co.language == m.language) continue;
            bool found = false;
            for (auto& e : others) {
                if (e.first == co.language) e.second += co.total(), found = true;
            }
            if (!found) others.emplace_back(co.language, co.total());
        }
        std::stable_sort(others.begin(), others.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string text;
        for (std::size_t i = 0; i < others.size() && i < 3; ++i) {
            if (i) text += ", ";
            text += s.languages[others[i].first].name +
                    fmt(" %.0f%%", 100.0 * static_cast<double>(others[i].second) / cell_pop);
        }
        if (!text.empty()) f.push_back({"Also spoken", text});
    }
    f.push_back({"Lineage", language_lineage(sim, m.language)});
    f.push_back({"Grammar", std::string(word_order_name(content, lang)) + ", " + morphology_name(content, lang) +
                                (lang.tone == 0 ? "" : std::string(", ") + tone_name(content, lang) + " tone")});
    f.push_back({"Words", sample_words(sim, m.language, 6)});

    const Culture& cul = s.cultures[m.culture];
    f.push_back({"Culture", cul.name + (m.culture_pop < s.cell_population[c]
                                            ? fmt(", %.0f%%", 100.0 * static_cast<double>(m.culture_pop) / cell_pop)
                                            : "")});
    // The culture's most distinctive ways here: traits far from the middle.
    for (const Cohort& co : s.cohorts_in(c)) {
        if (co.culture != m.culture) continue;
        std::vector<std::pair<double, std::string>> marked;
        for (std::size_t t = 0; t < content.traits.size(); ++t) {
            const double x = co.traits[t];
            if (std::fabs(x - 0.5) >= 0.15) {
                marked.emplace_back(std::fabs(x - 0.5), x < 0.5 ? content.traits[t].low : content.traits[t].high);
            }
        }
        std::stable_sort(marked.begin(), marked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::string text;
        for (std::size_t i = 0; i < marked.size() && i < 4; ++i) text += (i ? ", " : "") + marked[i].second;
        if (!text.empty()) f.push_back({"Ways", text});
        break;
    }

    // Farming.
    const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
    f.push_back({"Farming", farming < 0.005 ? "none: foragers" : fmt("%.0f%% of food", 100.0 * farming)});
    const std::uint32_t known = s.domesticates.empty() ? 0 : s.domesticates[c];
    std::string crops, animals;
    for (std::size_t sp = 0; sp < content.species.size(); ++sp) {
        if (!((known >> sp) & 1u)) continue;
        std::string& list = content.species[sp].kind == SpeciesKind::Plant ? crops : animals;
        list += (list.empty() ? "" : ", ") + content.species[sp].domesticated;
    }
    if (!crops.empty()) f.push_back({"Crops", crops});
    if (!animals.empty()) f.push_back({"Animals", animals});
    if (!s.env.farm_capacity.empty() && s.env.farm_capacity[c] > 0.0) {
        f.push_back({"Farmland", fmt("%.2f people per km² if all farmed", s.env.farm_capacity[c] / w.grid.area_km2(c))});
    }
    // Techniques.
    if (!s.techniques.empty() && s.techniques[c] != 0) {
        std::string known_techs;
        for (std::size_t k = 0; k < content.techniques.size(); ++k) {
            if ((s.techniques[c] >> k) & 1u) known_techs += (known_techs.empty() ? "" : ", ") + content.techniques[k].name;
        }
        f.push_back({"Techniques", known_techs});
    }
    for (const Invention& v : s.inventions) {
        if (v.cell != c) continue;
        f.push_back({"Invented", content.techniques[v.technique].name + " here in year " +
                                     with_commas(static_cast<long long>(v.tick / kTicksPerYear)) + " by the " +
                                     s.cultures[v.culture].name});
    }

    // Disease.
    const std::size_t nd = content.diseases.size();
    if (s.disease_present.size() == w.cell_count() * nd) {
        std::string present;
        for (std::size_t d = 0; d < nd; ++d) {
            if (!s.disease_present[c * nd + d]) continue;
            const DiseaseDef& def = content.diseases[d];
            present += (present.empty() ? "" : ", ") + def.name;
            if (def.kind == DiseaseKind::Epidemic) {
                present += fmt(" (epidemic, %.0f%% immune)", 100.0 * (1.0 - s.susceptible[c * nd + d]));
            }
        }
        f.push_back({"Diseases", present.empty() ? "none beyond the usual" : present});
        if (!s.disease_deaths.empty() && s.cell_population[c] > 0) {
            f.push_back({"Disease deaths", with_commas(static_cast<long long>(s.disease_deaths[c])) + " this year" +
                                               fmt(" (%.1f per 1,000)", 1000.0 * s.disease_deaths[c] /
                                                                            static_cast<double>(s.cell_population[c]))});
        }
    }

    // Domestication under way here, and origins.
    std::string underway;
    for (std::size_t k = 0; k < w.wild_stands.size() && k < s.domestication.size(); ++k) {
        if (w.wild_stands[k].cell != c || s.domestication[k] < 0.05 || s.domestication[k] >= 1.0) continue;
        underway += (underway.empty() ? "" : ", ") + content.species[w.wild_stands[k].species].name +
                    fmt(" %.0f%%", 100.0 * s.domestication[k]);
    }
    if (!underway.empty()) f.push_back({"Domesticating", underway});
    for (const Domestication& d : s.domestications) {
        if (d.cell != c) continue;
        f.push_back({"Origin", content.species[d.species].domesticated + " first domesticated here in year " +
                                   with_commas(static_cast<long long>(d.tick / kTicksPerYear)) + " by the " +
                                   s.cultures[d.culture].name});
    }
    return f;
}

std::string language_lineage(const Simulation& sim, std::uint32_t lang) {
    const SimState& s = sim.state();
    std::string out;
    for (std::uint32_t id = lang; id != 0 && id < s.languages.size(); id = s.languages[id].parent) {
        if (!out.empty()) out += " < ";
        out += s.languages[id].name;
    }
    return out;
}

std::string sample_words(const Simulation& sim, std::uint32_t lang, std::size_t count) {
    const SimState& s = sim.state();
    const Content& c = *sim.world().content;
    if (lang == 0 || lang >= s.languages.size()) return {};
    static const char* const kSample[] = {"water", "fire", "sun", "two", "eye", "mother", "people", "river", "stone"};
    std::string out;
    std::size_t shown = 0;
    for (const char* id : kSample) {
        if (shown == count) break;
        for (std::size_t k = 0; k < c.concepts.size(); ++k) {
            if (c.concepts[k].id != id) continue;
            out += (shown ? ", " : "") + c.concepts[k].id + " " + romanize(c, s.languages[lang].words[k].sounds);
            ++shown;
        }
    }
    return out;
}

std::vector<std::string> native_species(const Simulation& sim, CellId c) {
    std::vector<std::string> out;
    const World& w = sim.world();
    if (c >= w.cell_count()) return out;
    const auto& species = w.content->species;
    for (std::size_t s = 0; s < species.size() && s < 32; ++s) {
        if (w.species_mask[c] & (1u << s)) out.push_back(species[s].name);
    }
    return out;
}

}  // namespace histsim::view
