#include "histsim/view/map_modes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>
#include <stdexcept>

#include "histsim/language.hpp"
#include "histsim/view/timeline.hpp"
#include "histsim/systems/bondage.hpp"
#include "histsim/systems/economy.hpp"
#include "histsim/systems/people.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/systems/religion.hpp"
#include "histsim/systems/war.hpp"

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
const ColorScale& towns_scale() {  // brick, light to dark
    static const ColorScale s({{0, hex("#f7ece6")},
                               {0.03, hex("#ebc2ab")},
                               {0.08, hex("#d68a63")},
                               {0.15, hex("#ad5530")},
                               {0.3, hex("#6b2c12")}});
    return s;
}
const ColorScale& bondage_scale() {  // plum, light to dark
    static const ColorScale s({{0, hex("#f3edf2")},
                               {0.01, hex("#dcc3d8")},
                               {0.03, hex("#b98ab3")},
                               {0.1, hex("#8a4f86")},
                               {0.25, hex("#4f1f4d")}});
    return s;
}
const Rgba kStateless = hex("#dcd9cf");
const Rgba kCrownLand = hex("#e8b84a");
const Rgba kSmallRealm = hex("#a9a59b");
const ColorScale& trade_scale() {  // value carried a year, log: teal, light to dark
    static const ColorScale s({{10, hex("#e6f4f1")},
                               {100, hex("#a8dcd1")},
                               {1000, hex("#5bb3a3")},
                               {10000, hex("#2a7f72")},
                               {100000, hex("#0f4a42")}},
                              /*log_scale=*/true);
    return s;
}
const Rgba kNoTrade = hex("#dcd9cf");
// Ways of life.
const Rgba kHunting = hex("#9bb07a");     // hunting and gathering
const Rgba kFishing = hex("#5fa3b5");     // fishing and gathering
const Rgba kMixed = hex("#d9c56b");       // farming and foraging
const Rgba kCrops = hex("#c08a2e");       // crop farming
const Rgba kIrrigation = hex("#2f6f9f");  // irrigation farming
const Rgba kHerding = hex("#8a5a3c");     // herding
const Rgba kTowns = hex("#7a2f4f");       // a fifth or more in towns and cities
const ColorScale& trait_scale() {  // low pole (teal) - middle (gray) - high pole (orange)
    static const ColorScale s({{0, hex("#1f6f80")},
                               {0.25, hex("#7fb6bf")},
                               {0.5, hex("#efeee9")},
                               {0.75, hex("#e8a77a")},
                               {1, hex("#a8461e")}});
    return s;
}
const ColorScale& stability_scale() {  // fragile (red) to steady (green)
    static const ColorScale s({{0, hex("#9e1f1a")},
                               {0.3, hex("#e0784f")},
                               {0.5, hex("#f2e2a8")},
                               {0.7, hex("#8cc07a")},
                               {1.0, hex("#2f7a3d")}});
    return s;
}
// Deposits shown on the resources map, most valuable first wins a cell.
struct ResourceColor {
    const char* good;
    Rgba color;
};
const std::array<ResourceColor, 6> kResourceColors = {{
    {"gold", hex("#d4a017")},
    {"tin_ore", hex("#5d6d7e")},
    {"copper_ore", hex("#b4532a")},
    {"ornaments", hex("#d77fa1")},
    {"salt", hex("#6fb7c9")},
    {"stone", hex("#3b3a36")},
}};
const Rgba kNoResource = hex("#e3e0d6");
// War map: what happened in each cell this year.
const Rgba kPeace = hex("#dcd9cf");
const Rgba kAtWar = hex("#f2c4b8");    // ruled by a polity at war
const Rgba kFighting = hex("#d9603b");  // battles here
const Rgba kTaken = hex("#8c1d18");     // changed hands in war
const Rgba kRaided = hex("#7a3d8f");    // raided by nomads this year
const Rgba kRaidedLately = hex("#c9a3d6");  // raided in the last kRaidMemoryYears
constexpr std::int64_t kRaidMemoryYears = 20;

Rgba lighten(Rgba c, double t) {
    auto mixc = [&](std::uint8_t v) { return static_cast<std::uint8_t>(std::lround(v + (255.0 - v) * t)); };
    return {mixc(c.r), mixc(c.g), mixc(c.b), c.a};
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
const ColorScale& stores_scale() {  // amber, light to dark: years of food in store
    static const ColorScale s({{0, hex("#fbf3dc")},
                               {0.25, hex("#f3d68f")},
                               {0.5, hex("#e3b04a")},
                               {1.0, hex("#b97c16")},
                               {2.0, hex("#6e4508")}});
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
    {"terrain", "Terrain", "Elevation above and below sea level", "Land"},
    {"biome", "Biome", "Vegetation zone from the climate of each month", "Land"},
    {"temperature", "Temperature", "This year's mean temperature", "Land"},
    {"temp_anomaly", "Temp. anomaly", "This year's temperature minus the long-run baseline", "Land"},
    {"rainfall", "Rainfall", "This year's precipitation on land", "Land"},
    {"rain_season", "Rain season", "Whether rain falls in summer, in winter or all year", "Land"},
    {"growing", "Growing season", "Months warm and wet enough for plants to grow", "Land"},
    {"soil", "Soil", "Soil fertility", "Land"},
    {"species", "Wild species", "Number of native wild species that could later be domesticated", "Land"},
    {"capacity", "Forager capacity", "People per square km the land can feed by foraging this year", "Land"},
    {"game", "Game", "Wild food abundance relative to undisturbed land", "Land"},
    {"resources", "Resources", "Deposits of salt, stone, ores, gold and shell; the most valuable shown", "Land"},
    {"population", "Population", "People per square km", "People"},
    {"crowding", "Crowding", "Population relative to what the land can feed", "People"},
    {"stores", "Food stores", "Food put by in granaries, pits and herds, in years of food for the people", "People"},
    {"median_age", "Median age", "Median age of the people living in each cell", "People"},
    {"disease", "Disease", "Deaths from disease this year, per 1,000 people", "People"},
    {"language", "Language", "Main language spoken; related languages share a hue", "Culture"},
    {"language_family", "Language family", "Family of the main language", "Culture"},
    {"culture", "Culture", "Main culture; related cultures share a hue", "Culture"},
    {"religion", "Religion", "Faith of each land: organized religions in strong colors (sects in shades of their parent), folk beliefs muted", "Culture"},
    {"technology", "Technology", "Number of techniques the people know", "Culture"},
    {"bondage", "Enslavement", "Share of people held in bondage; arrows: slave routes used in the last 50 years", "People"},
    {"subsistence", "Way of life", "How people get their food: foraging, fishing, farming, herding, towns", "Economy"},
    {"farming", "Farming", "Share of people's food from crops and herds", "Economy"},
    {"domesticates", "Domesticates", "Domesticated crops and animals the people know", "Economy"},
    {"towns", "Towns", "Share of people living in towns and cities", "Economy"},
    {"trade", "Trade", "Value of goods sent and received this year", "Economy"},
    {"polity", "Polities", "Chiefdoms, states and empires; each its own color", "Power"},
    {"realms", "Realms", "Polities with their vassals and tributaries (lighter), each realm its own color", "Power"},
    {"war", "War", "Wars, battles, conquests and nomad raids this year", "Power"},
    {"stability", "Stability", "How steady each polity is against famine, war, overreach and other pressures", "Power"},
    {"provinces", "Provinces", "Provinces of the larger realms, by their governors' loyalty; the crown's own land in gold", "Power"},
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

Rgba polity_color(const SimState& s, std::uint32_t polity) {
    if (polity == 0 || polity >= s.polities.size()) return kEmptyLand;
    // Nomad confederations share an ochre-brown family, so they stand out.
    if (s.polities[polity].nomadic) {
        return hsl(28.0 + 16.0 * unit_hash(static_cast<std::uint64_t>(polity) * 13 + 5), 0.55,
                   0.32 + 0.14 * unit_hash(static_cast<std::uint64_t>(polity) * 7 + 3));
    }
    // Spread hues by the golden angle; vary lightness so neighbours differ.
    return hsl(std::fmod(static_cast<double>(polity) * 137.50776 + 40.0, 360.0), 0.5,
               0.42 + 0.16 * unit_hash(static_cast<std::uint64_t>(polity) * 7 + 3));
}

Rgba culture_color(const SimState& s, std::uint32_t culture) {
    if (culture == 0 || culture >= s.cultures.size()) return kEmptyLand;
    // Offset so a culture and its founding language family differ in hue.
    return group_color(s.cultures[culture].root + 7919, culture + 7919);
}

// Organized religions: a hue per founding faith, sects as shades of it.
// Folk beliefs: muted, a hue per people.
Rgba religion_color(const World& w, const SimState& s, std::uint32_t religion) {
    if (religion == 0 || religion >= s.religions.size()) return kEmptyLand;
    if (!religion_organized(w, s, religion)) {
        return hsl(family_hue(s.religions[religion].culture_root + 104729), 0.12,
                   0.62 + 0.1 * unit_hash(religion));
    }
    return group_color(religion_family(s, religion) + 3571, religion + 3571);
}

namespace {
Rgba people_color(const SimState& s, const MainPeople& m, const std::string& id) {
    if (id == "language") return language_color(s, m.language);
    if (id == "language_family") return language_family_color(s, m.language);
    return culture_color(s, m.culture);
}
}  // namespace

std::vector<RouteLine> route_lines(const Simulation& sim) {
    std::vector<RouteLine> out;
    const World& w = sim.world();
    const SimState& s = sim.state();
    double busiest = 0.0;
    const auto routes = trade_routes(w, s);
    for (const TradeRoute& r : routes) {
        for (std::uint32_t e : r.edges) busiest = std::max(busiest, s.route_value[e]);
    }
    for (const TradeRoute& r : routes) {
        for (std::uint32_t e : r.edges) {
            const World::TradeEdge& edge = w.trade_edges[e];
            out.push_back({edge.a, edge.b, s.route_value[e] / busiest, (edge.modes & (kTradeLand | kTradeRiver)) == 0});
        }
    }
    return out;
}

std::vector<RouteLine> migration_lines(const Simulation& sim, std::int64_t years) {
    std::vector<RouteLine> out;
    const SimState& s = sim.state();
    for (auto it = s.migrations.rbegin(); it != s.migrations.rend(); ++it) {
        if (s.tick - it->tick > years * kTicksPerYear) break;
        RouteLine l;
        l.a = it->from;
        l.b = it->to;
        l.strength = std::min(1.0, static_cast<double>(it->people) / 30000.0);
        l.migration = true;
        out.push_back(l);
    }
    return out;
}

std::vector<RouteLine> slave_route_lines(const Simulation& sim, std::int64_t years) {
    std::vector<RouteLine> out;
    const SimState& s = sim.state();
    for (const SlaveRoute& r : s.slave_routes) {
        if (s.tick - r.last > years * kTicksPerYear) continue;
        RouteLine l;
        l.a = r.from;
        l.b = r.to;
        l.strength = std::min(1.0, static_cast<double>(r.people) / 20000.0);
        l.migration = true;
        out.push_back(l);
    }
    return out;
}

Rgba ocean_neutral() { return kOceanNeutral; }
Rgba coastline_color() { return kCoastline; }

// The fixed modes, then one per culture trait ("trait:<id>").
const std::vector<MapModeInfo>& map_modes() {
    static const std::vector<MapModeInfo> modes = [] {
        std::vector<MapModeInfo> all = kModes;
        for (const CultureTrait& t : default_content().traits) {
            all.push_back({"trait:" + t.id, t.name, "Culture trait: from " + t.low + " to " + t.high, "Culture"});
        }
        return all;
    }();
    return modes;
}

const MapModeInfo& map_mode(std::string_view id) {
    for (const auto& m : map_modes()) {
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

namespace {

// A trait averaged over a cell's people (0.5 if unknown).
double cell_trait_mean(const SimState& s, CellId c, std::size_t t) {
    double sum = 0.0, people = 0.0;
    for (const Cohort& co : s.cohorts_in(c)) {
        const auto n = static_cast<double>(co.total());
        sum += n * (t < kMaxTraits ? co.traits[t] : 0.5);
        people += n;
    }
    return people > 0.0 ? sum / people : 0.5;
}

Rgba way_of_life_color(const World& w, const SimState& s, CellId c, std::size_t irrigation) {
    const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
    const double pastoral = s.env.pastoral.empty() ? 0.0 : s.env.pastoral[c];
    const double towns = s.urban.empty() ? 0.0 : s.urban[c];
    if (towns >= 0.2) return kTowns;
    if (farming >= 0.5) {
        if (pastoral >= 0.5) return kHerding;
        const bool irrigates = irrigation < 64 && !s.techniques.empty() && ((s.techniques[c] >> irrigation) & 1u) &&
                               is_river(w, c);
        return irrigates ? kIrrigation : kCrops;
    }
    if (farming >= 0.15) return kMixed;
    return w.aquatic_bonus[c] >= 0.3 ? kFishing : kHunting;
}

}  // namespace

void color_cells(const Simulation& sim, std::string_view mode_id, std::vector<Rgba>& out) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    const std::size_t n = w.cell_count();
    out.resize(n);
    const std::string id(map_mode(mode_id).id);

    // The technique and trait a mode needs.
    std::size_t irrigation = w.content->techniques.size(), trait = kMaxTraits;
    for (std::size_t k = 0; k < w.content->techniques.size(); ++k) {
        if (w.content->techniques[k].id == "irrigation") irrigation = k;
    }
    if (id.rfind("trait:", 0) == 0) {
        for (std::size_t t = 0; t < w.content->traits.size(); ++t) {
            if ("trait:" + w.content->traits[t].id == id) trait = t;
        }
    }
    // Polities at war now (for the war map).
    std::vector<std::uint8_t> at_war(s.polities.size(), 0);
    if (id == "war") {
        for (std::size_t i = 1; i < s.wars.size(); ++i) {
            if (s.wars[i].ended != kNever) continue;
            for (std::uint32_t p : s.wars[i].attackers) at_war[p] = 1;
            for (std::uint32_t p : s.wars[i].defenders) at_war[p] = 1;
        }
    }
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
        } else if (id == "stores") {
            const double store = s.env.food_store.size() > c ? s.env.food_store[c] : 0.0;
            out[c] = inhabited ? stores_scale()(store / static_cast<double>(s.cell_population[c])) : kEmptyLand;
        } else if (id == "farming") {
            const double f = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            out[c] = !inhabited ? kEmptyLand : (f < 0.01 ? kForagers : farming_scale()(f));
        } else if (id == "polity") {
            const std::uint32_t p = s.polity_of.empty() ? 0 : s.polity_of[c];
            out[c] = !inhabited ? kEmptyLand : (p == 0 ? kStateless : polity_color(s, p));
        } else if (id == "towns") {
            out[c] = inhabited ? towns_scale()(s.urban.empty() ? 0.0 : s.urban[c]) : kEmptyLand;
        } else if (id == "subsistence") {
            out[c] = inhabited ? way_of_life_color(w, s, c, irrigation) : kEmptyLand;
        } else if (id == "bondage") {
            out[c] = inhabited ? bondage_scale()(static_cast<double>(bonded_people(s, c)) /
                                                 static_cast<double>(s.cell_population[c]))
                               : kEmptyLand;
        } else if (id == "religion") {
            out[c] = inhabited && s.faith_of.size() == n ? religion_color(w, s, s.faith_of[c]) : kEmptyLand;
        } else if (id.rfind("trait:", 0) == 0) {
            out[c] = inhabited ? trait_scale()(cell_trait_mean(s, c, trait)) : kEmptyLand;
        } else if (id == "realms") {
            const std::uint32_t p = s.polity_of.empty() ? 0 : s.polity_of[c];
            const std::uint32_t top = top_overlord(s, p);
            out[c] = !inhabited ? kEmptyLand
                     : p == 0   ? kStateless
                                : (top == p ? polity_color(s, top) : lighten(polity_color(s, top), 0.45));
        } else if (id == "stability") {
            const std::uint32_t p = s.polity_of.empty() ? 0 : s.polity_of[c];
            out[c] = !inhabited ? kEmptyLand : (p == 0 ? kStateless : stability_scale()(s.polities[p].stability));
        } else if (id == "provinces") {
            const std::uint32_t p = s.polity_of.empty() ? 0 : s.polity_of[c];
            const std::uint32_t v = s.province_of.size() == n ? s.province_of[c] : 0;
            bool crown = false;
            if (p != 0 && v == 0) {
                for (const Province& pv : s.provinces) {
                    if (pv.id != 0 && pv.polity == p && pv.ended == kNever) {
                        crown = true;
                        break;
                    }
                }
            }
            out[c] = !inhabited ? kEmptyLand
                     : p == 0   ? kStateless
                     : v != 0   ? stability_scale()(s.provinces[v].loyalty)
                     : crown    ? kCrownLand
                                : kSmallRealm;
        } else if (id == "war") {
            const std::uint8_t strife = s.strife.size() == n ? s.strife[c] : 0;
            if (!inhabited) {
                out[c] = kEmptyLand;
            } else if (strife == 3) {
                out[c] = kRaided;
            } else if (strife == 2) {
                out[c] = kTaken;
            } else if (strife == 1) {
                out[c] = kFighting;
            } else if (s.last_raid.size() == n && s.last_raid[c] != kNever &&
                       s.tick - s.last_raid[c] <= kRaidMemoryYears * kTicksPerYear) {
                out[c] = kRaidedLately;
            } else {
                const std::uint32_t p = s.polity_of.empty() ? 0 : s.polity_of[c];
                out[c] = p != 0 && at_war[p] ? kAtWar : kPeace;
            }
        } else if (id == "trade") {
            const double v = s.trade_value.empty() ? 0.0 : s.trade_value[c];
            out[c] = !inhabited ? kEmptyLand : (v < 10.0 ? kNoTrade : trade_scale()(v));
        } else if (id == "resources") {
            out[c] = kNoResource;
            for (const ResourceColor& rc : kResourceColors) {
                const auto& goods = w.content->goods;
                const auto it = std::find_if(goods.begin(), goods.end(), [&](const GoodDef& g) { return g.id == rc.good; });
                if (it != goods.end() && w.deposit(c, static_cast<std::size_t>(it - goods.begin())) > 0.0) {
                    out[c] = rc.color;
                    break;
                }
            }
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
    } else if (id == "stores") {
        l.title = "Food in store (years of food per person)";
        l.ramp = ramp_entries(stores_scale(), "%g");
        l.swatches = empty_and_ocean;
    } else if (id == "farming") {
        l.title = "Share of food from farming";
        l.ramp = {{"1%", farming_scale()(0.0)}, {"25%", farming_scale()(0.25)}, {"50%", farming_scale()(0.5)},
                  {"75%", farming_scale()(0.75)}, {"100%", farming_scale()(1.0)}};
        l.swatches = {{"Foragers", kForagers}, {"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    } else if (id == "polity") {
        l.title = "Polities";
        l.swatches = {{"No ruler (bands, tribes, villages)", kStateless}, {"Uninhabited", kEmptyLand},
                      {"Ocean", kOceanNeutral}};
    } else if (id == "bondage") {
        l.title = "Share of people held in bondage (arrows: slave routes of the last 50 years)";
        l.ramp = {{"0%", bondage_scale()(0)}, {"1%", bondage_scale()(0.01)}, {"3%", bondage_scale()(0.03)},
                  {"10%", bondage_scale()(0.1)}, {"25%", bondage_scale()(0.25)}};
        l.swatches = empty_and_ocean;
    } else if (id == "towns") {
        l.title = "Share of people living in towns";
        l.ramp = {{"0%", towns_scale()(0)}, {"3%", towns_scale()(0.03)}, {"8%", towns_scale()(0.08)},
                  {"15%", towns_scale()(0.15)}, {"30%", towns_scale()(0.3)}};
        l.swatches = empty_and_ocean;
    } else if (id == "subsistence") {
        l.title = "Way of life (how most food is got)";
        l.swatches = {{"Hunting and gathering", kHunting},
                      {"Fishing and gathering (coasts and rivers)", kFishing},
                      {"Farming and foraging", kMixed},
                      {"Crop farming", kCrops},
                      {"Irrigation farming (on rivers)", kIrrigation},
                      {"Herding (pastoralists)", kHerding},
                      {"Towns and cities (a fifth or more in towns)", kTowns},
                      {"Uninhabited", kEmptyLand},
                      {"Ocean", kOceanNeutral}};
    } else if (id.rfind("trait:", 0) == 0) {
        for (const CultureTrait& t : default_content().traits) {
            if ("trait:" + t.id != id) continue;
            l.title = t.name + " (average of the people)";
            l.ramp = {{t.low, trait_scale()(0)}, {"", trait_scale()(0.25)}, {"middle", trait_scale()(0.5)},
                      {"", trait_scale()(0.75)}, {t.high, trait_scale()(1)}};
        }
        l.swatches = empty_and_ocean;
    } else if (id == "religion") {
        l.title = "Religion";
        l.swatches = {{"Folk beliefs (each people its own; muted)", hsl(30.0, 0.12, 0.67)},
                      {"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    } else if (id == "realms") {
        l.title = "Realms (subjects in lighter shades; nomad confederations in browns)";
        l.swatches = {{"No ruler (bands, tribes, villages)", kStateless}, {"Uninhabited", kEmptyLand},
                      {"Ocean", kOceanNeutral}};
    } else if (id == "stability") {
        l.title = "Stability of the ruling polity (below 30% it may break up)";
        l.ramp = {{"0%", stability_scale()(0)}, {"30%", stability_scale()(0.3)}, {"50%", stability_scale()(0.5)},
                  {"70%", stability_scale()(0.7)}, {"100%", stability_scale()(1.0)}};
        l.swatches = {{"No ruler", kStateless}, {"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    } else if (id == "provinces") {
        l.title = "Provinces by their governors' loyalty (below 40% they may break away)";
        l.ramp = {{"0%", stability_scale()(0)}, {"40%", stability_scale()(0.4)}, {"60%", stability_scale()(0.6)},
                  {"80%", stability_scale()(0.8)}, {"100%", stability_scale()(1.0)}};
        l.swatches = {{"The crown's own land", kCrownLand}, {"Realm too small for provinces", kSmallRealm},
                      {"No ruler", kStateless}, {"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    } else if (id == "war") {
        l.title = "War this year";
        l.swatches = {{"Changed hands in war", kTaken}, {"Battles", kFighting}, {"Raided by nomads", kRaided},
                      {"Raided in the last 20 years", kRaidedLately},
                      {"Ruled by a polity at war", kAtWar}, {"At peace", kPeace}, {"Uninhabited", kEmptyLand},
                      {"Ocean", kOceanNeutral}};
    } else if (id == "trade") {
        l.title = "Goods sent and received this year (value, food = 1; log scale)";
        l.ramp = ramp_entries(trade_scale(), "%g");
        l.ramp.back().label = "100,000+";
        l.swatches = {{"No trade", kNoTrade}, {"Uninhabited", kEmptyLand}, {"Ocean", kOceanNeutral}};
    } else if (id == "resources") {
        l.title = "Deposits (the most valuable in each cell)";
        const auto& goods = default_content().goods;
        for (const ResourceColor& rc : kResourceColors) {
            for (const GoodDef& g : goods) {
                if (g.id == rc.good) l.swatches.push_back({g.name, rc.color});
            }
        }
        l.swatches.push_back({"None of these", kNoResource});
        l.swatches.push_back({"Ocean", kOceanNeutral});
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
    if (id == "polity") {
        const SimState& s = sim.state();
        std::vector<std::int64_t> people(s.polities.size(), 0);
        for (CellId c = 0; c < s.polity_of.size(); ++c) people[s.polity_of[c]] += s.cell_population[c];
        std::vector<std::uint32_t> order;
        for (std::uint32_t i = 1; i < people.size(); ++i) {
            if (people[i] > 0) order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return people[a] > people[b]; });
        std::vector<LegendEntry> entries;
        for (std::size_t i = 0; i < order.size() && i < 10; ++i) {
            const std::uint32_t p = order[i];
            entries.push_back({s.polities[p].name + " (" + polity_level(sim.world(), s, p) + "): " +
                                   with_commas(people[p]),
                               polity_color(s, p)});
        }
        if (order.size() > 10) {
            entries.push_back(
                {"+ " + with_commas(static_cast<long long>(order.size() - 10)) + " other polities", Rgba{0, 0, 0, 0}});
        }
        entries.insert(entries.end(), l.swatches.begin(), l.swatches.end());
        l.swatches = std::move(entries);
        l.title += " (" + with_commas(static_cast<long long>(order.size())) + ")";
        return l;
    }
    if (id == "realms") {
        const SimState& s = sim.state();
        std::vector<std::int64_t> people(s.polities.size(), 0);
        std::vector<std::size_t> subjects(s.polities.size(), 0);
        for (CellId c = 0; c < s.polity_of.size(); ++c) people[top_overlord(s, s.polity_of[c])] += s.cell_population[c];
        for (std::uint32_t i = 1; i < s.polities.size(); ++i) {
            if (s.polities[i].ended == kNever && s.polities[i].overlord != 0) ++subjects[top_overlord(s, i)];
        }
        std::vector<std::uint32_t> order;
        for (std::uint32_t i = 1; i < people.size(); ++i) {
            if (people[i] > 0) order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return people[a] > people[b]; });
        std::vector<LegendEntry> entries;
        for (std::size_t i = 0; i < order.size() && i < 10; ++i) {
            const std::uint32_t p = order[i];
            std::string label = s.polities[p].name + (s.polities[p].nomadic ? " (nomads)" : "") + ": " +
                                with_commas(people[p]);
            if (subjects[p] > 0) {
                label += " (" + with_commas(static_cast<long long>(subjects[p])) +
                         (subjects[p] == 1 ? " subject)" : " subjects)");
            }
            entries.push_back({label, polity_color(s, p)});
        }
        if (order.size() > 10) {
            entries.push_back(
                {"+ " + with_commas(static_cast<long long>(order.size() - 10)) + " other realms", Rgba{0, 0, 0, 0}});
        }
        entries.insert(entries.end(), l.swatches.begin(), l.swatches.end());
        l.swatches = std::move(entries);
        l.title += " (" + with_commas(static_cast<long long>(order.size())) + ")";
        return l;
    }
    if (id == "religion") {
        const World& w = sim.world();
        const SimState& s = sim.state();
        std::vector<std::int64_t> people(s.religions.size(), 0);
        for (CellId c = 0; c < s.faith_of.size(); ++c) people[s.faith_of[c]] += s.cell_population[c];
        std::vector<std::uint32_t> order;
        std::size_t folk = 0;
        for (std::uint32_t i = 1; i < people.size(); ++i) {
            if (people[i] == 0) continue;
            if (religion_organized(w, s, i)) order.push_back(i);
            else ++folk;
        }
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return people[a] > people[b]; });
        std::vector<LegendEntry> entries;
        for (std::size_t i = 0; i < order.size() && i < 10; ++i) {
            const std::uint32_t r = order[i];
            entries.push_back({s.religions[r].name + " (" + religion_kind_name(w, s, r) + "): " + with_commas(people[r]),
                               religion_color(w, s, r)});
        }
        if (order.size() > 10) {
            entries.push_back({"+ " + with_commas(static_cast<long long>(order.size() - 10)) + " other organized faiths",
                               Rgba{0, 0, 0, 0}});
        }
        l.swatches.front().label = "Folk beliefs of " + with_commas(static_cast<long long>(folk)) +
                                   " peoples (muted)";
        entries.insert(entries.end(), l.swatches.begin(), l.swatches.end());
        l.swatches = std::move(entries);
        l.title += " (" + with_commas(static_cast<long long>(order.size())) + " organized)";
        return l;
    }
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

namespace {
constexpr std::size_t kHistoryRows = 6;  // lines of history in the inspector
struct Row {
    std::string label, value;
    std::uint32_t figure = 0;  // a person the row names (SimState::figures)
};
// The rows of describe_cell; `mark(name)` starts a section at the next row.
std::vector<Row> describe_cell_rows(const Simulation& sim, CellId c,
                                          std::vector<std::pair<std::size_t, std::string>>& marks) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    if (c >= w.cell_count()) {
        return {};
    }
    const double lat = w.grid.latitude_deg_display(c);
    const double lon = w.grid.longitude_deg_display(c);
    std::vector<Row> f;
    auto mark = [&](const char* name) { marks.emplace_back(f.size(), name); };
    mark("Place");
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
    mark("People");
    f.push_back({"Population", with_commas(static_cast<long long>(s.cell_population[c])) + " people"});
    if (s.cell_population[c] == 0) return f;

    f.push_back({"Density", fmt("%.3f per km²", density(w, s, c))});
    f.push_back({"Crowding", fmt("%.2f of capacity", crowding(s, c))});
    if (s.env.food_store.size() > c) {
        f.push_back({"Food stores", fmt("%.2f years of food", s.env.food_store[c] / static_cast<double>(s.cell_population[c]))});
    }
    const Pyramid p = cell_pyramid(sim, c);
    f.push_back({"Median age", fmt("%.1f years", median_age(p))});
    {
        const LifeTable life = cell_life_table(s, c);
        double full = 0.0;  // children per woman at the life table's full fertility
        for (int b = 0; b < kAgeBins; ++b) full += kYearsPerAgeBin * forager_fertility(b);
        const double tfr = full * std::min(DemographySystem::kMaxReplacement, cell_replacement_fertility(s, c));
        f.push_back({"Life expectancy", fmt("%.0f years at birth", life.life_expectancy) +
                                            fmt("; %.1f children per woman replace them", tfr)});
    }
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
    mark("Language");
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
    mark("Culture");
    f.push_back({"Culture", cul.name + (m.culture_pop < s.cell_population[c]
                                            ? fmt(", %.0f%%", 100.0 * static_cast<double>(m.culture_pop) / cell_pop)
                                            : "")});
    mark("Culture traits");
    // The culture's ways here, every trait.
    for (const Cohort& co : s.cohorts_in(c)) {
        if (co.culture != m.culture) continue;
        for (std::size_t t = 0; t < content.traits.size(); ++t) {
            const CultureTrait& def = content.traits[t];
            const double x = co.traits[t];
            const char* lean = x < 0.35 ? def.low.c_str() : x > 0.65 ? def.high.c_str() : "between";
            f.push_back({def.name, fmt("%.0f%%, ", 100.0 * x) + lean});
        }
        break;
    }

    mark("Religion");
    // Faith.
    if (s.faith_of.size() == w.cell_count() && s.faith_of[c] != 0) {
        const Religion& r = s.religions[s.faith_of[c]];
        std::string faith = r.name + " (" + religion_kind_name(w, s, r.id) + ")";
        if (r.founded > 0) faith += ", since year " + with_commas(static_cast<long long>(r.founded / kTicksPerYear));
        f.push_back({"Religion", faith});
        if (r.parent != 0) f.push_back({"Arose from", s.religions[r.parent].name});
        if (religion_organized(w, s, r.id) && r.origin != kInvalidCell && r.origin != c) {
            const std::uint32_t town = s.city_of.empty() ? 0 : s.city_of[r.origin];
            f.push_back({"Began", (town != 0 ? "at " + s.cities[town].name + ", " : std::string()) +
                                      with_commas(std::llround(w.grid.distance_km(c, r.origin))) + " km away"});
        }
    }

    mark("Food");
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
    mark("Rule");
    // Rule and towns.
    if (!s.polity_of.empty() && s.polity_of[c] != 0) {
        const std::uint32_t ruler = s.polity_of[c];
        const Polity& pol = s.polities[ruler];
        const double reach = polity_reach(w, s, ruler);
        const double control = polity_control(w.grid.distance_km(pol.capital, c), reach);
        const std::uint32_t capital_city = s.city_of.empty() ? 0 : s.city_of[pol.capital];
        f.push_back({"Polity", pol.name + " (" + polity_level(w, s, ruler) + "), " +
                                   (c == pol.capital ? std::string("capital here")
                                                     : "capital " + (capital_city != 0 ? s.cities[capital_city].name
                                                                                       : std::string("elsewhere"))) +
                                   fmt(", control %.0f%%", 100.0 * control)});
        const PolitySize z = polity_size(w, s, ruler);
        f.push_back({"Realm", with_commas(static_cast<long long>(z.cells)) + " cells, " +
                                  with_commas(std::llround(z.area_km2)) + " km², " + with_commas(z.people) +
                                  " people; since year " + with_commas(static_cast<long long>(pol.founded / kTicksPerYear))});
        {
            std::string why;
            for (std::size_t k = 0; k < kPressureCount; ++k) {
                if (PolitySystem::kPressureWeight[k] * pol.pressure[k] < 0.05) continue;
                why += (why.empty() ? "" : ", ") + std::string(pressure_name(static_cast<Pressure>(k))) +
                       fmt(" %.0f%%", 100.0 * pol.pressure[k]);
            }
            f.push_back({"Stability", fmt("%.0f%%", 100.0 * pol.stability) +
                                          (pol.stability < PolitySystem::kFragile ? " (fragile)" : "") +
                                          (why.empty() ? "" : "; strained by " + why)});
        }
        if (pol.ruler != 0 && pol.ruler < s.rulers.size()) {
            const Ruler& r = s.rulers[pol.ruler];
            const int age = ruler_age(r, s.tick);
            std::string ways;
            auto add = [&](const char* word) { ways += (ways.empty() ? "" : ", ") + std::string(word); };
            if (r.ability > 0.7) add("able");
            if (r.ability < 0.3) add("feeble");
            if (r.martial > 0.7) add("warlike");
            if (r.martial < 0.3) add("peaceable");
            std::string house = r.dynasty < s.dynasties.size() ? s.dynasties[r.dynasty].name : "";
            f.push_back({"Ruler", ruler_name(r) + (house.empty() ? "" : " of the " + house) + ", aged " + std::to_string(age) +
                                      (ways.empty() ? "" : " (" + ways + ")") + ", since year " +
                                      with_commas(static_cast<long long>(r.reign_start / kTicksPerYear)) +
                                      (age < RulerSystem::kAdultAge ? "; a regent rules" : ""),
                         r.figure < s.figures.size() ? r.figure : 0u});
            if (r.dynasty < s.dynasties.size()) {
                std::size_t reigns = 0;
                for (const Ruler& o : s.rulers) reigns += o.dynasty == r.dynasty ? 1 : 0;
                const Dynasty& d = s.dynasties[r.dynasty];
                f.push_back({"House", d.name + ": " + std::to_string(reigns) + (reigns == 1 ? " ruler" : " rulers") +
                                          " since year " + with_commas(static_cast<long long>(d.founded / kTicksPerYear))});
            }
        }
        // Its province, if the realm has them.
        {
            const std::uint32_t v = s.province_of.size() == w.cell_count() ? s.province_of[c] : 0;
            if (v != 0 && v < s.provinces.size()) {
                const Province& pv = s.provinces[v];
                std::string who;
                std::uint32_t gov = 0;
                if (pv.governor != 0 && pv.governor < s.figures.size()) {
                    gov = pv.governor;
                    const Figure& g = s.figures[gov];
                    who = ", governor " + g.name + fmt(" (ambition %.0f%%)", 100.0 * g.ambition);
                }
                f.push_back({"Province", pv.name + who + fmt("; loyalty %.0f%%", 100.0 * pv.loyalty) +
                                             (pv.loyalty < PolitySystem::kRevoltBelow ? " (may break away)" : ""),
                             gov});
            } else {
                bool has = false;
                for (const Province& pv : s.provinces) has = has || (pv.id != 0 && pv.polity == ruler && pv.ended == kNever);
                if (has) f.push_back({"Province", "the crown's own land, ruled from the capital"});
            }
        }
        if (pol.institutions != 0) {
            std::string list;
            for (std::size_t k = 0; k < kInstitutionCount; ++k) {
                if (has_institution(pol.institutions, static_cast<Institution>(k))) {
                    list += (list.empty() ? "" : ", ") + std::string(institution_name(static_cast<Institution>(k)));
                }
            }
            f.push_back({"Institutions", list});
        }
        if (pol.religion != 0 && pol.religion < s.religions.size()) {
            f.push_back({"Rulers' faith", s.religions[pol.religion].name});
        }
        if (pol.overlord != 0 && pol.overlord < s.polities.size()) {
            f.push_back({"Overlord", std::string(pol.bond == Bond::Vassal ? "vassal of " : "pays tribute to ") +
                                         s.polities[pol.overlord].name + " since year " +
                                         with_commas(static_cast<long long>(pol.bond_since / kTicksPerYear))});
        }
        std::string subjects, allies;
        for (std::uint32_t q = 1; q < s.polities.size(); ++q) {
            const Polity& o = s.polities[q];
            if (o.ended != kNever) continue;
            if (o.overlord == ruler) {
                subjects += (subjects.empty() ? "" : ", ") + o.name + (o.bond == Bond::Vassal ? " (vassal)" : " (tributary)");
            }
            if (q != ruler && allied(s, ruler, q)) allies += (allies.empty() ? "" : ", ") + o.name;
        }
        if (!subjects.empty()) f.push_back({"Subjects", subjects});
        if (!allies.empty()) f.push_back({"Allies", allies});
        std::string wars;
        for (std::size_t i = 1; i < s.wars.size(); ++i) {
            const War& war = s.wars[i];
            if (war.ended != kNever) continue;
            const bool att = std::find(war.attackers.begin(), war.attackers.end(), ruler) != war.attackers.end();
            const bool def = std::find(war.defenders.begin(), war.defenders.end(), ruler) != war.defenders.end();
            if (!att && !def) continue;
            wars += (wars.empty() ? "" : "; ") + war.name + (att ? " (attacking" : " (defending") + ", since year " +
                    with_commas(static_cast<long long>(war.started / kTicksPerYear)) + ")";
        }
        if (!wars.empty()) f.push_back({"At war", wars});
    } else {
        f.push_back({"Polity", "none: bands, tribes and villages"});
    }
    mark("Economy");
    if (!s.urban.empty() && s.urban[c] > 0.001) {
        const double town = s.urban[c] * static_cast<double>(s.cell_population[c]);
        const std::uint32_t city = s.city_of.empty() ? 0 : s.city_of[c];
        f.push_back({"Towns", (city != 0 ? s.cities[city].name + ", " : std::string()) +
                                  with_commas(std::llround(town)) + fmt(" people (%.0f%%)", 100.0 * s.urban[c])});
    }

    mark("People");
    if (const std::int64_t held = bonded_people(s, c); held > 0) {
        std::map<std::uint32_t, std::int64_t> by_language;
        for (const Cohort& co : s.cohorts_in(c)) {
            by_language[co.language] += static_cast<std::int64_t>(co.bonded * static_cast<double>(co.total()) + 0.5);
        }
        std::uint32_t most = 0;
        std::int64_t most_n = -1;
        for (const auto& [l, k] : by_language) {
            if (k > most_n) most = l, most_n = k;
        }
        f.push_back({"Enslaved", with_commas(static_cast<long long>(held)) +
                                     fmt(" (%.1f%%), ", 100.0 * static_cast<double>(held) / static_cast<double>(s.cell_population[c])) +
                                     "most " + s.languages[most].name + " speakers; wanted here: " +
                                     fmt("%.0f%%", 100.0 * bondage_demand(w, s, c))});
    }
    if (s.last_arrival.size() == w.cell_count() && s.last_arrival[c] != 0 && s.last_arrival[c] <= s.migrations.size()) {
        const FolkMovement& move = s.migrations[s.last_arrival[c] - 1];
        static const char* const why[] = {"fleeing famine", "fleeing war", "fleeing raids", "pushed out by others",
                                          "a whole people on the move"};
        const auto cause = static_cast<std::size_t>(move.cause);
        std::string who = s.languages[move.language].name + " speakers";
        if (move.cause == MigrationCause::Wandering && move.polity < s.polities.size()) {
            who = "of the " + s.polities[move.polity].name + (s.polities[move.polity].nomadic ? " (nomads)" : " (a tribe)") +
                  ", " + who;
        }
        f.push_back({"Newcomers", with_commas(move.people) + " " + who + " in year " +
                                      with_commas(static_cast<long long>(move.tick / kTicksPerYear)) + ", " +
                                      (cause < std::size(why) ? why[cause] : "moving") +
                                      fmt(", from %.0f km away", w.grid.distance_km(move.from, move.to))});
    }
    mark("Rule");
    if (s.last_raid.size() == w.cell_count() && s.last_raid[c] != kNever) {
        f.push_back({"Last raided", "by nomads in year " + with_commas(static_cast<long long>(s.last_raid[c] / kTicksPerYear))});
    }
    // Strife this year.
    if (s.strife.size() == w.cell_count() && s.strife[c] != 0) {
        const char* what = s.strife[c] == 3 ? "raided by nomads" : s.strife[c] == 2 ? "taken in war" : "battles";
        const long long dead = s.war_deaths.size() == w.cell_count() ? s.war_deaths[c] : 0;
        f.push_back({"This year", std::string(what) + ", " + with_commas(dead) + " killed"});
    }

    mark("Economy");
    // Resources and trade.
    const std::size_t ng = content.goods.size();
    std::string deposits;
    for (std::size_t g = 0; g < ng; ++g) {
        const double d = w.deposit(c, g);
        if (d <= 0.0) continue;
        deposits += (deposits.empty() ? "" : ", ") + content.goods[g].name + (d > 0.75 ? " (rich)" : "");
    }
    if (!deposits.empty()) f.push_back({"Deposits", deposits});
    if (s.cell_population[c] > 0 && s.goods_available.size() == w.cell_count() && s.prices.size() == w.cell_count() * ng) {
        std::string market;
        for (std::size_t g = 0; g < ng; ++g) {
            if (g == content.food_good || !((s.goods_available[c] >> g) & 1u)) continue;
            const double ratio = s.prices[c * ng + g] / content.goods[g].base_price;
            market += (market.empty() ? "" : ", ") + content.goods[g].name +
                      (ratio < 0.6 ? " (cheap)" : ratio > 2.0 ? " (dear)" : "");
        }
        f.push_back({"Market", market.empty() ? "nothing beyond food" : market});
        std::string trade = fmt("%.0f worth a year", s.trade_value[c]);
        const double food = s.food_import[c];
        if (food >= 0.5) trade += "; food for " + with_commas(std::llround(food)) + " people brought in";
        if (food <= -0.5) trade += "; food for " + with_commas(std::llround(-food)) + " people sent out";
        f.push_back({"Trade", trade});
        for (const TradeRoute& r : trade_routes(w, s)) {
            if (std::binary_search(r.cells.begin(), r.cells.end(), c)) {
                f.push_back({"Trade route", r.name + fmt(", %.0f a year", r.value)});
                break;
            }
        }
    }

    mark("Technology");
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

    mark("Health");
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

    mark("Food");
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

    // Why it is as it is: what happened here, and to its rulers, lately.
    mark("History here");
    for (const TimelineEntry& e : timeline(sim, "", 0, c, kHistoryRows)) {
        f.push_back({"Year " + with_commas(static_cast<long long>(e.year)), e.text});
    }
    if (!s.polity_of.empty() && s.polity_of[c] != 0) {
        mark("Realm's history");
        for (const TimelineEntry& e : timeline(sim, "", s.polity_of[c], kInvalidCell, kHistoryRows)) {
            f.push_back({"Year " + with_commas(static_cast<long long>(e.year)), e.text});
        }
    }
    return f;
}
}  // namespace

std::vector<MapLabel> polity_labels(const Simulation& sim) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    std::vector<MapLabel> out;
    if (s.polity_of.size() != w.cell_count()) return out;
    const std::size_t np = s.polities.size();
    std::vector<double> area(np, 0.0);
    std::vector<Vec3> sum(np, Vec3{0, 0, 0});
    for (CellId c = 0; c < s.polity_of.size(); ++c) {
        const std::uint32_t p = s.polity_of[c];
        if (p == 0) continue;
        const double a = w.grid.area_km2(c);
        area[p] += a;
        const Vec3& v = w.grid.center(c);
        sum[p] = {sum[p].x + a * v.x, sum[p].y + a * v.y, sum[p].z + a * v.z};
    }
    std::vector<std::uint32_t> chosen;
    for (std::uint32_t p = 1; p < np; ++p) {
        if (area[p] <= 0.0 || s.polities[p].ended != kNever) continue;
        if (area[p] >= kLabelMinAreaKm2 || polity_has_writing(w, s, p)) chosen.push_back(p);
    }
    std::stable_sort(chosen.begin(), chosen.end(), [&](auto a, auto b) { return area[a] > area[b]; });
    if (chosen.size() > kMaxLabels) chosen.resize(kMaxLabels);
    for (std::uint32_t p : chosen) {
        // The middle of its land; if that falls outside (a crescent, a realm
        // split by sea), its cell nearest the middle.
        const Vec3 m = sum[p];
        const double len = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
        Vec3 at = len > 0.0 ? Vec3{m.x / len, m.y / len, m.z / len} : w.grid.center(s.polities[p].capital);
        CellId nearest = s.polities[p].capital;
        double best = -2.0;
        for (CellId c = 0; c < s.polity_of.size(); ++c) {
            if (s.polity_of[c] != p) continue;
            const Vec3& v = w.grid.center(c);
            const double d = v.x * at.x + v.y * at.y + v.z * at.z;
            if (d > best) best = d, nearest = c;
        }
        const double spacing_km = std::sqrt(w.grid.area_km2(nearest));
        if (std::acos(std::clamp(best, -1.0, 1.0)) * kPlanetRadiusKm > 1.5 * spacing_km) {
            at = w.grid.center(nearest);
        }
        MapLabel l;
        const Vec3& seat = w.grid.center(s.polities[p].capital);
        l.capital_u = (std::atan2(seat.y, seat.x) * 180.0 / kPi + 180.0) / 360.0;
        l.capital_v = (90.0 - std::asin(std::clamp(seat.z, -1.0, 1.0)) * 180.0 / kPi) / 180.0;
        l.name = s.polities[p].name;
        l.u = (std::atan2(at.y, at.x) * 180.0 / kPi + 180.0) / 360.0;
        l.v = (90.0 - std::asin(std::clamp(at.z, -1.0, 1.0)) * 180.0 / kPi) / 180.0;
        l.area_km2 = area[p];
        l.level = polity_level(w, s, p);
        l.polity = p;
        out.push_back(std::move(l));
    }
    return out;
}

namespace {

// Where on the flat map (u, v) a point on the sphere falls.
void sphere_uv(const Vec3& p, double& u, double& v) {
    u = (std::atan2(p.y, p.x) * 180.0 / kPi + 180.0) / 360.0;
    v = (90.0 - std::asin(std::clamp(p.z, -1.0, 1.0)) * 180.0 / kPi) / 180.0;
}

// The middle of some cells' land, or, if that falls outside them (a
// crescent), their cell nearest it.
Vec3 middle_of(const World& w, const std::vector<CellId>& cells) {
    Vec3 m{0, 0, 0};
    for (CellId c : cells) {
        const double a = w.grid.area_km2(c);
        const Vec3& v = w.grid.center(c);
        m = {m.x + a * v.x, m.y + a * v.y, m.z + a * v.z};
    }
    const double len = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
    if (len <= 0.0 || cells.empty()) return cells.empty() ? Vec3{0, 0, 1} : w.grid.center(cells.front());
    const Vec3 at{m.x / len, m.y / len, m.z / len};
    CellId nearest = cells.front();
    double best = -2.0;
    for (CellId c : cells) {
        const Vec3& v = w.grid.center(c);
        const double d = v.x * at.x + v.y * at.y + v.z * at.z;
        if (d > best) best = d, nearest = c;
    }
    const double spacing_km = std::sqrt(w.grid.area_km2(nearest));
    return std::acos(std::clamp(best, -1.0, 1.0)) * kPlanetRadiusKm > 1.5 * spacing_km ? w.grid.center(nearest) : at;
}

}  // namespace

std::vector<std::uint32_t> border_regions(const Simulation& sim, std::string_view mode_id) {
    const SimState& s = sim.state();
    std::vector<std::uint32_t> key = s.polity_of;
    if (map_mode(mode_id).id == "provinces" && s.province_of.size() == key.size()) {
        for (std::size_t c = 0; c < key.size(); ++c) {
            if (s.province_of[c] != 0) key[c] = 0x80000000u | s.province_of[c];
        }
    }
    return key;
}

std::vector<MapLabel> map_labels(const Simulation& sim, std::string_view mode_id) {
    const World& w = sim.world();
    const SimState& s = sim.state();
    const std::string id(map_mode(mode_id).id);
    const std::size_t n = w.cell_count();
    const bool peoples = id == "language" || id == "language_family" || id == "culture";
    const bool faith = id == "religion";
    const bool realms = id == "realms";
    if (!peoples && !faith && !realms) return polity_labels(sim);
    // The group each cell belongs to (0: none).
    std::vector<std::uint32_t> key(n, 0);
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) continue;
        if (peoples) {
            const MainPeople m = main_people(s, c);
            key[c] = id == "culture" ? m.culture : id == "language" ? m.language : s.languages[m.language].family;
        } else if (faith) {
            key[c] = s.faith_of.size() == n ? s.faith_of[c] : 0;
        } else {
            key[c] = s.polity_of.size() == n ? top_overlord(s, s.polity_of[c]) : 0;
        }
    }
    // Each connected stretch of a group's land.
    std::vector<std::uint8_t> seen(n, 0);
    std::vector<CellId> region, queue;
    std::vector<MapLabel> out;
    for (CellId start = 0; start < n; ++start) {
        if (key[start] == 0 || seen[start]) continue;
        region.clear();
        queue.assign(1, start);
        seen[start] = 1;
        double area = 0.0;
        for (std::size_t i = 0; i < queue.size(); ++i) {
            const CellId c = queue[i];
            region.push_back(c);
            area += w.grid.area_km2(c);
            for (CellId nb : w.grid.neighbors(c)) {
                if (!seen[nb] && key[nb] == key[start]) {
                    seen[nb] = 1;
                    queue.push_back(nb);
                }
            }
        }
        if (area < kGroupLabelMinAreaKm2) continue;
        const std::uint32_t k = key[start];
        MapLabel l;
        l.area_km2 = area;
        l.capital_u = l.capital_v = -1.0;
        if (id == "culture") {
            l.name = k < s.cultures.size() ? s.cultures[k].name : "";
        } else if (id == "language") {
            l.name = k < s.languages.size() ? s.languages[k].name : "";
        } else if (id == "language_family") {
            l.name = k < s.languages.size() ? family_name(s, k) : "";
        } else if (faith) {
            l.name = k < s.religions.size() ? s.religions[k].name : "";
        } else {
            if (k >= s.polities.size()) continue;
            l.name = s.polities[k].name;
            l.level = polity_level(w, s, k);
            l.polity = k;
            sphere_uv(w.grid.center(s.polities[k].capital), l.capital_u, l.capital_v);
        }
        if (l.name.empty()) continue;
        sphere_uv(middle_of(w, region), l.u, l.v);
        out.push_back(std::move(l));
    }
    std::stable_sort(out.begin(), out.end(), [](const MapLabel& a, const MapLabel& b) { return a.area_km2 > b.area_km2; });
    if (out.size() > kMaxGroupLabels) out.resize(kMaxGroupLabels);
    return out;
}

std::vector<MapMarker> capital_markers(const Simulation& sim, std::string_view mode_id) {
    std::vector<MapMarker> out;
    const std::string id(mode_id);
    const bool realms = id == "realms";
    if (!realms && id != "polity" && id != "stability" && id != "provinces") return out;
    const World& w = sim.world();
    const SimState& s = sim.state();
    for (std::uint32_t p = 1; p < s.polities.size(); ++p) {
        const Polity& pol = s.polities[p];
        if (pol.ended != kNever || pol.capital >= w.cell_count() || s.polity_of.size() != w.cell_count() ||
            s.polity_of[pol.capital] != p) {
            continue;
        }
        MapMarker m;
        m.cell = pol.capital;
        const bool state = polity_has_writing(w, s, p);
        if (realms && top_overlord(s, p) == p && (state || pol.nomadic)) {
            m.radius = 4.5;  // the seat of a state, empire or confederation answering to no one: gold
            m.fill = hex("#f2c14e");
            m.center = hex("#ffffff");
        } else if (realms && top_overlord(s, p) != p) {
            m.radius = 2.0;  // a subject's seat
        } else {
            m.radius = state ? 4.0 : 2.5;
        }
        out.push_back(m);
    }
    // Provinces: their seats, small.
    if (id == "provinces") {
        for (const Province& pv : s.provinces) {
            if (pv.id == 0 || pv.ended != kNever || pv.seat >= w.cell_count()) continue;
            MapMarker m;
            m.cell = pv.seat;
            m.radius = 2.0;
            m.fill = hex("#f0efec");
            out.push_back(m);
        }
    }
    // Larger dots last, so they sit on top.
    std::stable_sort(out.begin(), out.end(), [](const MapMarker& a, const MapMarker& b) { return a.radius < b.radius; });
    return out;
}

bool same_group_cells(const Simulation& sim, std::string_view mode_id, CellId cell, std::vector<std::uint8_t>& out,
                      Rgba* group_color) {
    out.clear();
    const World& w = sim.world();
    const SimState& s = sim.state();
    const std::size_t n = w.cell_count();
    if (cell >= n || s.cell_population[cell] == 0) return false;
    const std::string id(mode_id);
    if (id == "language" || id == "language_family" || id == "culture") {
        // Groups of people: the selected cell's main language (family,
        // culture) is the main one in some cells and a minority in others.
        auto key_of = [&](const Cohort& co) -> std::uint32_t {
            return id == "culture" ? co.culture : id == "language" ? co.language : s.languages[co.language].family;
        };
        const MainPeople picked = main_people(s, cell);
        const std::uint32_t key = id == "culture"    ? picked.culture
                                  : id == "language" ? picked.language
                                                     : s.languages[picked.language].family;
        if (key == 0) return false;
        if (group_color != nullptr) {
            *group_color = id == "culture"    ? culture_color(s, key)
                           : id == "language" ? language_color(s, key)
                                              : language_family_color(s, picked.language);
        }
        out.assign(n, 0);
        for (CellId c = 0; c < n; ++c) {
            if (s.cell_population[c] == 0) continue;
            const MainPeople m = main_people(s, c);
            const std::uint32_t main = id == "culture" ? m.culture : id == "language" ? m.language : s.languages[m.language].family;
            if (main == key) {
                out[c] = kFocusMain;
                continue;
            }
            std::int64_t members = 0;
            for (const Cohort& co : s.cohorts_in(c)) members += key_of(co) == key ? co.total() : 0;
            if (members == 0) continue;
            const double share = static_cast<double>(members) / static_cast<double>(s.cell_population[c]);
            out[c] = static_cast<std::uint8_t>(1 + std::lround(std::min(1.0, share) * 253.0));
        }
        return true;
    }
    std::vector<std::uint32_t> key(n, 0);
    if (id == "religion") {
        if (s.faith_of.size() != n) return false;
        key = s.faith_of;
    } else if (id == "polity" || id == "stability" || id == "realms") {
        if (s.polity_of.size() != n) return false;
        for (CellId c = 0; c < n; ++c) {
            key[c] = id == "realms" ? top_overlord(s, s.polity_of[c]) : s.polity_of[c];
        }
    } else {
        return false;
    }
    if (key[cell] == 0) return false;
    if (group_color != nullptr) {
        *group_color = id == "religion" ? religion_color(w, s, key[cell]) : polity_color(s, key[cell]);
    }
    out.assign(n, 0);
    for (CellId c = 0; c < n; ++c) out[c] = key[c] == key[cell] ? kFocusMain : 0;
    return true;
}

std::vector<CellField> describe_cell(const Simulation& sim, CellId c) {
    std::vector<std::pair<std::size_t, std::string>> marks;
    const std::vector<Row> rows = describe_cell_rows(sim, c, marks);
    std::vector<CellField> f;
    std::size_t m = 0;
    std::string section;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        while (m < marks.size() && marks[m].first <= i) section = marks[m++].second;
        f.push_back({rows[i].label, rows[i].value, section, rows[i].figure});
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
