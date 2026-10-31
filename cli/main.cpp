// histsim: headless runner for batch runs, determinism checks and CI.
//
//   histsim run  [--seed N] [--level L] [--ocean F] [--years Y]
//                [--report-every Y] [--save FILE] [--hash-only]
//   histsim resume FILE [--years Y] [--report-every Y] [--save FILE] [--hash-only]
//   histsim world [--seed N] [--level L] [--ocean F]
//   histsim languages [--seed N] [--level L] [--years Y] [--top N]
//   histsim farming [--seed N] [--level L] [--years Y]
//
// With no arguments (for example, double-clicked in Explorer) it runs a short
// demo and waits for Enter so the window stays open.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define HISTSIM_ISATTY _isatty
#define HISTSIM_FILENO _fileno
#else
#include <unistd.h>
#define HISTSIM_ISATTY isatty
#define HISTSIM_FILENO fileno
#endif

#include "histsim/simulation.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/view/map_modes.hpp"
#include "histsim/view/png.hpp"
#include "histsim/view/raster.hpp"

namespace {

using histsim::Simulation;

struct Options {
    histsim::WorldParams params;
    std::int64_t years = 100;
    std::int64_t report_every = 0;
    std::string save_path;
    bool hash_only = false;
    // map command
    std::string mode = "all";
    std::string out = "map";
    int width = 2048;
    bool coastlines = true;
    bool rivers = true;
    // languages command: how many families to print (0 = all)
    std::int64_t top = 0;
    // --at YEAR:ACTION:...: interventions, applied when the run reaches YEAR
    std::vector<std::pair<std::int64_t, std::string>> at;
};

[[noreturn]] void usage(const char* message = nullptr) {
    if (message) {
        std::fprintf(stderr, "error: %s\n\n", message);
    }
    std::fprintf(stderr,
                 "usage:\n"
                 "  histsim run    [--seed N] [--level L] [--ocean F] [--years Y]\n"
                 "                 [--report-every Y] [--save FILE] [--hash-only]\n"
                 "  histsim resume FILE [--years Y] [--report-every Y] [--save FILE] [--hash-only]\n"
                 "  histsim world  [--seed N] [--level L] [--ocean F]\n"
                 "  histsim map    [--seed N] [--level L] [--ocean F] [--years Y] [--mode M|all]\n"
                 "                 [--width W] [--out PREFIX] [--no-coastlines] [--no-rivers]\n"
                 "                 writes PREFIX-<mode>.png; modes: terrain biome population\n"
                 "                 crowding temperature rainfall rain_season growing soil\n"
                 "                 species capacity game temp_anomaly median_age language\n"
                 "                 language_family culture farming domesticates disease\n"
                 "                 technology\n"
                 "  histsim farming [--seed N] [--level L] [--years Y]\n"
                 "                 where and when species were domesticated, techniques\n"
                 "                 invented and diseases emerged, Bond events, and farmers,\n"
                 "                 techniques and disease deaths every 500 years\n"
                 "  histsim languages [--seed N] [--level L] [--years Y] [--top N]\n"
                 "                 family trees of the languages alive after Y years, largest\n"
                 "                 families first, with speakers and sample words\n"
                 "  histsim --version\n"
                 "\n"
                 "run, resume and map accept interventions, applied when the run reaches YEAR\n"
                 "(years since it began). CELL is a cell id or LAT,LON in degrees.\n"
                 "  --at YEAR:eruption:W            volcanic eruption, W/m^2 of cooling (Pinatubo ~3)\n"
                 "  --at YEAR:forcing:W             set a persistent forcing, W/m^2 (CO2 x2 ~ +3.7)\n"
                 "  --at YEAR:climate:CELL:KM:DEG:RAIN:YEARS\n"
                 "                                  regional event, e.g. drought 0:-0.5:20\n"
                 "  --at YEAR:plague:CELL:KM:SHARE  share (0-1) of people there die\n"
                 "  --at YEAR:wipeout:CELL:KM       everyone there dies\n"
                 "  --at YEAR:settlers:CELL:COUNT   place a new group\n"
                 "  --at YEAR:game:CELL:KM:LEVEL    set game abundance (0-1)\n"
                 "  --at YEAR:bond:DEG:YEARS        start a Bond event: north cools by up to DEG C\n"
                 "  --at YEAR:outbreak:CELL:KM:ID   bring a disease (malaria, plague, influenza, pox,\n"
                 "                                  measles, ...; see data/diseases.json)\n"
                 "\n"
                 "With no arguments, runs a 1,000-year demo.\n");
    std::exit(2);
}

std::int64_t parse_int(std::string_view s) {
    char* end = nullptr;
    const std::string copy(s);
    const long long v = std::strtoll(copy.c_str(), &end, 10);
    if (end == copy.c_str() || *end != '\0') {
        usage("expected an integer");
    }
    return v;
}

double parse_double(std::string_view s) {
    char* end = nullptr;
    const std::string copy(s);
    const double v = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || *end != '\0') {
        usage("expected a number");
    }
    return v;
}

Options parse(const std::vector<std::string_view>& args, std::size_t first) {
    Options o;
    for (std::size_t i = first; i < args.size(); ++i) {
        const std::string_view a = args[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= args.size()) usage("missing value after an option");
            return args[++i];
        };
        if (a == "--seed") o.params.seed = static_cast<std::uint64_t>(parse_int(value()));
        else if (a == "--level") o.params.grid_level = static_cast<int>(parse_int(value()));
        else if (a == "--ocean") o.params.ocean_fraction = parse_double(value());
        else if (a == "--years") o.years = parse_int(value());
        else if (a == "--report-every") o.report_every = parse_int(value());
        else if (a == "--save") o.save_path = std::string(value());
        else if (a == "--hash-only") o.hash_only = true;
        else if (a == "--mode") o.mode = std::string(value());
        else if (a == "--out") o.out = std::string(value());
        else if (a == "--width") o.width = static_cast<int>(parse_int(value()));
        else if (a == "--no-coastlines") o.coastlines = false;
        else if (a == "--no-rivers") o.rivers = false;
        else if (a == "--at") {
            const std::string spec(value());
            const auto colon = spec.find(':');
            if (colon == std::string::npos) usage("--at needs YEAR:ACTION...");
            o.at.emplace_back(parse_int(std::string_view(spec).substr(0, colon)), spec.substr(colon + 1));
        }
        else if (a == "--top") o.top = parse_int(value());
        else usage("unknown option");
    }
    if (o.years < 0) usage("--years must be >= 0");
    return o;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const auto pos = s.find(sep, start);
        out.push_back(s.substr(start, pos - start));
        if (pos == std::string::npos) return out;
        start = pos + 1;
    }
}

// A cell id, or "LAT,LON" in degrees resolved to the nearest cell center.
histsim::CellId resolve_cell(const histsim::World& w, const std::string& token) {
    const auto comma = token.find(',');
    if (comma == std::string::npos) {
        const auto id = parse_int(token);
        if (id < 0 || static_cast<std::size_t>(id) >= w.cell_count()) usage("cell id out of range");
        return static_cast<histsim::CellId>(id);
    }
    // Input only: the chosen id is what gets recorded, so libm here is fine.
    const double lat = parse_double(token.substr(0, comma)) * histsim::kPi / 180.0;
    const double lon = parse_double(token.substr(comma + 1)) * histsim::kPi / 180.0;
    const histsim::Vec3 p{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
    histsim::CellId best = 0;
    double best_dot = -2.0;
    for (histsim::CellId c = 0; c < w.cell_count(); ++c) {
        const auto& v = w.grid.center(c);
        const double d = v.x * p.x + v.y * p.y + v.z * p.z;
        if (d > best_dot) {
            best_dot = d;
            best = c;
        }
    }
    return best;
}

histsim::Command parse_command(const histsim::World& w, const std::string& spec) {
    using histsim::CommandType;
    const auto parts = split(spec, ':');
    const std::string& action = parts[0];
    auto need = [&](std::size_t n) {
        if (parts.size() != n + 1) usage(("wrong number of values for --at ...:" + action).c_str());
    };
    histsim::Command c;
    if (action == "eruption") {
        need(1);
        c.type = CommandType::Eruption;
        c.amount = parse_double(parts[1]);
    } else if (action == "forcing") {
        need(1);
        c.type = CommandType::ClimateForcing;
        c.amount = parse_double(parts[1]);
    } else if (action == "climate") {
        need(5);
        c.type = CommandType::RegionalClimate;
        c.cell = resolve_cell(w, parts[1]);
        c.radius_km = parse_double(parts[2]);
        c.amount = parse_double(parts[3]);
        c.amount2 = parse_double(parts[4]);
        c.years = parse_int(parts[5]);
    } else if (action == "plague") {
        need(3);
        c.type = CommandType::Plague;
        c.cell = resolve_cell(w, parts[1]);
        c.radius_km = parse_double(parts[2]);
        c.amount = parse_double(parts[3]);
    } else if (action == "wipeout") {
        need(2);
        c.type = CommandType::WipeOut;
        c.cell = resolve_cell(w, parts[1]);
        c.radius_km = parse_double(parts[2]);
    } else if (action == "settlers") {
        need(2);
        c.type = CommandType::Settlers;
        c.cell = resolve_cell(w, parts[1]);
        c.amount = parse_double(parts[2]);
    } else if (action == "outbreak") {
        need(3);
        c.type = CommandType::Outbreak;
        c.cell = resolve_cell(w, parts[1]);
        c.radius_km = parse_double(parts[2]);
        const std::string& id = parts[3];
        const auto& diseases = w.content->diseases;
        std::size_t d = 0;
        while (d < diseases.size() && diseases[d].id != id) ++d;
        if (d == diseases.size()) usage(("unknown disease: " + id).c_str());
        c.amount = static_cast<double>(d);
    } else if (action == "bond") {
        need(2);
        c.type = CommandType::BondEvent;
        c.amount = parse_double(parts[1]);
        c.years = parse_int(parts[2]);
    } else if (action == "game") {
        need(3);
        c.type = CommandType::SetGame;
        c.cell = resolve_cell(w, parts[1]);
        c.radius_km = parse_double(parts[2]);
        c.amount = parse_double(parts[3]);
    } else {
        usage(("unknown intervention: " + action).c_str());
    }
    return c;
}

// Step to `target` year, applying any interventions scheduled on the way.
void advance(Simulation& sim, std::int64_t target, const Options& o, bool quiet) {
    while (sim.year() < target) {
        std::int64_t next = target;
        for (const auto& [year, spec] : o.at) {
            if (year > sim.year() && year < next) next = year;
        }
        sim.step_years(next - sim.year());
        for (const auto& [year, spec] : o.at) {
            if (year != sim.year()) continue;
            const histsim::InterventionResult r = sim.apply(parse_command(sim.world(), spec));
            if (!r.ok) {
                std::fprintf(stderr, "error: year %lld: %s\n", static_cast<long long>(year), r.message.c_str());
                std::exit(1);
            }
            if (!quiet) std::printf("year %6lld  %s\n", static_cast<long long>(year), r.message.c_str());
        }
    }
}

// Interventions scheduled at the current year (e.g. year 0) apply before stepping.
void apply_due_now(Simulation& sim, const Options& o, bool quiet) {
    for (const auto& [year, spec] : o.at) {
        if (year != sim.year()) continue;
        const histsim::InterventionResult r = sim.apply(parse_command(sim.world(), spec));
        if (!r.ok) {
            std::fprintf(stderr, "error: year %lld: %s\n", static_cast<long long>(year), r.message.c_str());
            std::exit(1);
        }
        if (!quiet) std::printf("year %6lld  %s\n", static_cast<long long>(year), r.message.c_str());
    }
}

void print_report(const Simulation& sim) {
    const histsim::RunSummary r = sim.summary();
    double capacity = 0.0;
    for (double k : sim.state().env.capacity) capacity += k;
    const histsim::PeopleCounts p = histsim::count_peoples(sim.state());
    const auto& h = sim.state().history;
    const double farmers = h.empty() ? 0.0 : static_cast<double>(h.back().farmers);
    const double anomaly = sim.state().climate.global_anomaly_c + sim.state().climate.bond_anomaly_c;
    std::printf("year %6lld  population %11lld  farmers %5.1f%%  capacity %11.0f (%5.1f%%)  inhabited %6zu  "
                "languages %4u (%3u families)  cultures %4u  climate %+5.2f C%s  hash %016llx\n",
                static_cast<long long>(sim.year()), static_cast<long long>(r.population),
                r.population > 0 ? 100.0 * farmers / static_cast<double>(r.population) : 0.0, capacity,
                capacity > 0 ? 100.0 * static_cast<double>(r.population) / capacity : 0.0, r.inhabited_cells,
                p.languages, p.families, p.cultures, anomaly, sim.state().climate.bond.years > 0 ? " (Bond)" : "",
                static_cast<unsigned long long>(r.state_hash));
}

int run(Simulation sim, const Options& o) {
    if (!o.hash_only) {
        print_report(sim);
    }
    const std::int64_t start = sim.year();
    apply_due_now(sim, o, o.hash_only);
    std::int64_t done = 0;
    while (done < o.years) {
        const std::int64_t chunk = o.report_every > 0 ? std::min(o.report_every, o.years - done) : o.years - done;
        advance(sim, start + done + chunk, o, o.hash_only);
        done += chunk;
        if (!o.hash_only && (o.report_every > 0 || done == o.years)) {
            print_report(sim);
        }
    }
    if (!o.save_path.empty()) {
        sim.save(o.save_path);
        if (!o.hash_only) {
            std::printf("saved %s\n", o.save_path.c_str());
        }
    }
    if (o.hash_only) {
        std::printf("%016llx\n", static_cast<unsigned long long>(sim.state_hash()));
    }
    return 0;
}

int map_images(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    apply_due_now(sim, o, false);
    advance(sim, o.years, o, false);
    const int height = o.width / 2;
    const auto raster = histsim::view::MapRaster::build(sim.world().grid, o.width, height);

    std::vector<std::string> modes;
    if (o.mode == "all") {
        for (const auto& m : histsim::view::map_modes()) modes.push_back(m.id);
    } else {
        modes.push_back(histsim::view::map_mode(o.mode).id);  // throws if unknown
    }
    std::vector<histsim::view::Rgba> colors;
    std::vector<std::uint8_t> rgba;
    histsim::view::RenderOptions ro;
    ro.coastlines = o.coastlines;
    ro.rivers = o.rivers;
    for (const auto& id : modes) {
        histsim::view::color_cells(sim, id, colors);
        histsim::view::render(raster, sim.world(), colors, rgba, ro);
        const std::string path = o.out + "-" + id + ".png";
        histsim::view::write_png(path, o.width, height, rgba);
        std::printf("wrote %s (year %lld)\n", path.c_str(), static_cast<long long>(sim.year()));
    }
    return 0;
}

// Family trees: each language with its speakers (or when it split or died
// out), living languages with a few words.
int languages(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    apply_due_now(sim, o, true);
    advance(sim, o.years, o, true);
    const histsim::SimState& s = sim.state();
    const std::size_t nl = s.languages.size();
    std::vector<std::int64_t> speakers(nl, 0), subtree(nl, 0);
    for (const histsim::Cohort& c : s.cohorts) speakers[c.language] += c.total();
    std::vector<std::vector<std::uint32_t>> children(nl);
    for (std::uint32_t i = 1; i < nl; ++i) {
        if (s.languages[i].parent != 0) children[s.languages[i].parent].push_back(i);
    }
    // Speakers of each language and all its descendants (children have
    // higher ids than parents, so one backward pass suffices).
    for (std::uint32_t i = static_cast<std::uint32_t>(nl); i-- > 1;) {
        subtree[i] += speakers[i];
        if (s.languages[i].parent != 0) subtree[s.languages[i].parent] += subtree[i];
    }
    std::vector<std::uint32_t> roots;
    for (std::uint32_t i = 1; i < nl; ++i) {
        if (s.languages[i].parent == 0 && subtree[i] > 0) roots.push_back(i);
    }
    std::stable_sort(roots.begin(), roots.end(), [&](auto a, auto b) { return subtree[a] > subtree[b]; });
    const histsim::PeopleCounts p = histsim::count_peoples(s);
    std::printf("year %lld: %u languages in %u families, %u cultures\n\n", static_cast<long long>(sim.year()),
                p.languages, p.families, p.cultures);
    auto when = [](histsim::Tick t) {
        const long long y = t / histsim::kTicksPerYear;
        return y < 0 ? std::to_string(-y) + " years before the run" : "year " + std::to_string(y);
    };
    auto print = [&](auto&& self, std::uint32_t id, const std::string& indent) -> void {
        if (subtree[id] == 0) return;  // whole branch extinct: skip
        const histsim::Language& l = s.languages[id];
        if (l.extinct == histsim::kNever) {
            std::printf("%s%s  %s speakers  [%s]\n", indent.c_str(), l.name.c_str(),
                        std::to_string(speakers[id]).c_str(), histsim::view::sample_words(sim, id, 4).c_str());
        } else {
            std::printf("%s%s  (split %s)\n", indent.c_str(), l.name.c_str(), when(l.extinct).c_str());
        }
        auto kids = children[id];
        std::stable_sort(kids.begin(), kids.end(), [&](auto a, auto b) { return subtree[a] > subtree[b]; });
        for (std::uint32_t k : kids) self(self, k, indent + "  ");
    };
    std::size_t shown = 0;
    for (std::uint32_t r : roots) {
        if (o.top > 0 && static_cast<std::int64_t>(shown) >= o.top) break;
        std::string family = s.languages[r].name;
        if (family.rfind("Proto-", 0) == 0) family = family.substr(6);
        std::printf("%s family, %s speakers\n", family.c_str(), std::to_string(subtree[r]).c_str());
        print(print, r, "  ");
        std::printf("\n");
        ++shown;
    }
    return 0;
}

// Where and when species were domesticated, then how farming spread.
int farming(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    apply_due_now(sim, o, true);
    advance(sim, o.years, o, true);
    const histsim::SimState& s = sim.state();
    const histsim::World& w = sim.world();
    const auto& species = w.content->species;
    std::printf("Independent domestications in %lld years:\n", static_cast<long long>(sim.year()));
    for (const histsim::Domestication& d : s.domestications) {
        const double lat = w.grid.latitude_deg_display(d.cell), lon = w.grid.longitude_deg_display(d.cell);
        std::printf("  year %5lld  %-20s %5.1f%c %6.1f%c  by the %s (%s speakers)\n",
                    static_cast<long long>(d.tick / histsim::kTicksPerYear), species[d.species].domesticated.c_str(),
                    std::fabs(lat), lat >= 0 ? 'N' : 'S', std::fabs(lon), lon >= 0 ? 'E' : 'W',
                    s.cultures[d.culture].name.c_str(), s.languages[d.language].name.c_str());
    }
    std::printf("\nBond events:\n");
    for (const histsim::BondEvent& b : s.climate.bond_history) {
        std::printf("  years %5lld-%-5lld  up to %.1f C colder in the north\n", static_cast<long long>(b.start_year),
                    static_cast<long long>(b.start_year + b.years), b.strength_c);
    }
    std::printf("\nIndependent inventions:\n");
    for (const histsim::Invention& v : s.inventions) {
        const double lat = w.grid.latitude_deg_display(v.cell), lon = w.grid.longitude_deg_display(v.cell);
        std::printf("  year %5lld  %-26s %5.1f%c %6.1f%c  by the %s\n", static_cast<long long>(v.tick / histsim::kTicksPerYear),
                    w.content->techniques[v.technique].name.c_str(), std::fabs(lat), lat >= 0 ? 'N' : 'S', std::fabs(lon),
                    lon >= 0 ? 'E' : 'W', s.cultures[v.culture].name.c_str());
    }
    std::printf("\nDiseases that emerged:\n");
    for (const histsim::DiseaseOrigin& d : s.disease_origins) {
        const double lat = w.grid.latitude_deg_display(d.cell), lon = w.grid.longitude_deg_display(d.cell);
        std::printf("  year %5lld  %-20s %5.1f%c %6.1f%c\n", static_cast<long long>(d.tick / histsim::kTicksPerYear),
                    w.content->diseases[d.disease].name.c_str(), std::fabs(lat), lat >= 0 ? 'N' : 'S', std::fabs(lon),
                    lon >= 0 ? 'E' : 'W');
    }
    std::printf("\nFarmers by century:\n");
    for (std::size_t i = 0; i < s.history.size(); ++i) {
        const histsim::YearRecord& r = s.history[i];
        if (r.year % 500 != 0) continue;
        const std::int64_t disease = r.disease_deaths_total - (i > 0 ? s.history[i - 1].disease_deaths_total : 0);
        std::printf("  year %5lld  population %11lld  farmers %5.1f%%  domesticates %2u  techniques %2u  "
                    "disease deaths %6lld/yr\n",
                    static_cast<long long>(r.year), static_cast<long long>(r.population),
                    r.population > 0 ? 100.0 * static_cast<double>(r.farmers) / static_cast<double>(r.population) : 0.0,
                    r.domesticated, r.techniques, static_cast<long long>(disease));
    }
    return 0;
}

int world_info(const Options& o) {
    const histsim::World w = histsim::generate_test_world(o.params);
    std::size_t land = 0;
    double land_area = 0.0, capacity = 0.0;
    for (histsim::CellId c = 0; c < w.cell_count(); ++c) {
        if (w.is_land[c]) {
            ++land;
            land_area += w.grid.area_km2(c);
            capacity += w.forager_capacity[c];
        }
    }
    std::printf("seed            %llu\n", static_cast<unsigned long long>(o.params.seed));
    std::printf("grid level      %d\n", w.grid.level());
    std::printf("cells           %zu\n", w.cell_count());
    std::printf("mean cell area  %.0f km^2\n", histsim::kPlanetAreaKm2 / static_cast<double>(w.cell_count()));
    std::printf("land cells      %zu (%.1f%%)\n", land, 100.0 * static_cast<double>(land) / static_cast<double>(w.cell_count()));
    std::printf("land area       %.1f million km^2\n", land_area / 1e6);
    std::printf("forager capacity %.2f million people\n", capacity / 1e6);
    std::printf("world hash      %016llx\n", static_cast<unsigned long long>(w.hash()));
    return 0;
}

// No arguments: show the world and 1,000 years of history, then explain usage.
int demo() {
    std::printf("histsim %s: human history simulator (milestone M2 in progress, Neolithic)\n\n", HISTSIM_VERSION);
    Options o;
    o.years = 1000;
    o.report_every = 100;
    world_info(o);
    std::printf("\nSimulating %lld years of forager history on this world...\n\n",
                static_cast<long long>(o.years));
    const int rc = run(Simulation::create(o.params), o);
    std::printf(
        "\nThis is the command-line runner; the map and globe interface is the histsim viewer.\n"
        "Run it from a terminal for more options, for example:\n"
        "  histsim run --seed 7 --level 6 --years 2000 --report-every 200\n"
        "  histsim run --years 500 --save run.hsim\n"
        "  histsim resume run.hsim --years 500\n"
        "  histsim world --seed 7\n");
    if (HISTSIM_ISATTY(HISTSIM_FILENO(stdin))) {
        std::printf("\nPress Enter to close.");
        std::fflush(stdout);
        (void)std::getchar();
    }
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv, argv + argc);
    if (args.size() < 2) {
        try {
            return demo();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    if (args[1] == "--version" || args[1] == "version") {
        std::printf("histsim %s\n", HISTSIM_VERSION);
        return 0;
    }
    if (args[1] == "--help" || args[1] == "-h" || args[1] == "help") {
        usage();
    }
    try {
        const std::string_view cmd = args[1];
        if (cmd == "run") {
            const Options o = parse(args, 2);
            return run(Simulation::create(o.params), o);
        }
        if (cmd == "resume") {
            if (args.size() < 3) usage("resume needs a snapshot file");
            const Options o = parse(args, 3);
            return run(Simulation::load(std::string(args[2])), o);
        }
        if (cmd == "farming") {
            return farming(parse(args, 2));
        }
        if (cmd == "languages") {
            Options o = parse(args, 2);
            return languages(o);
        }
        if (cmd == "world") {
            return world_info(parse(args, 2));
        }
        if (cmd == "map") {
            Options o = parse(args, 2);
            if (o.width < 64 || o.width > 16384) usage("--width must be between 64 and 16384");
            return map_images(o);
        }
        usage("unknown command");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
