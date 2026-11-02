// histsim: headless runner for batch runs, determinism checks and CI.
//
//   histsim run  [--seed N] [--level L] [--ocean F] [--years Y]
//                [--report-every Y] [--save FILE] [--hash-only]
//   histsim resume FILE [--years Y] [--report-every Y] [--save FILE] [--hash-only]
//   histsim world [--seed N] [--level L] [--ocean F]
//   histsim languages [--seed N] [--level L] [--years Y] [--top N]
//   histsim farming [--seed N] [--level L] [--years Y]
//   histsim polities [--seed N] [--level L] [--years Y]
//   histsim trade [--seed N] [--level L] [--years Y]
//   histsim wars [--seed N] [--level L] [--years Y]
//   histsim migrations [--seed N] [--level L] [--years Y]
//   histsim religions [--seed N] [--level L] [--years Y]
//   histsim slavery [--seed N] [--level L] [--years Y]
//
// With no arguments (for example, double-clicked in Explorer) it runs a short
// demo and waits for Enter so the window stays open.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
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
#include "histsim/systems/economy.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/systems/bondage.hpp"
#include "histsim/systems/religion.hpp"
#include "histsim/systems/technology.hpp"
#include "histsim/systems/war.hpp"
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
                 "                 technology polity towns realms war stability trade resources\n"
                 "                 (polity borders are drawn on all, trade routes on trade\n"
                 "                 and resources, the last 50 years' folk migrations on\n"
                 "                 population, language, language_family and culture)\n"
                 "  histsim farming [--seed N] [--level L] [--years Y]\n"
                 "                 where and when species were domesticated, techniques\n"
                 "                 invented and diseases emerged, Bond events, and farmers,\n"
                 "                 techniques and disease deaths every 500 years\n"
                 "  histsim polities [--seed N] [--level L] [--years Y]\n"
                 "                 towns, cities and polities every 500 years, then the largest\n"
                 "                 polities alive\n"
                 "  histsim trade [--seed N] [--level L] [--years Y]\n"
                 "                 trade every 500 years, then the goods (where they are made\n"
                 "                 and sold, and at what price) and the busiest trade routes\n"
                 "  histsim wars [--seed N] [--level L] [--years Y]\n"
                 "                 war and diplomacy every 500 years, the deadliest wars, and\n"
                 "                 the largest realms with their subjects and allies\n"
                 "  histsim migrations [--seed N] [--level L] [--years Y]\n"
                 "                 colonists and folk migrations every 500 years, the largest\n"
                 "                 movements with the chains they set off, and how the language\n"
                 "                 families' lands changed\n"
                 "  histsim slavery [--seed N] [--level L] [--years Y]\n"
                 "                 captives and the enslaved every 500 years, where they live,\n"
                 "                 who they are, and the busiest slave routes\n"
                 "  histsim religions [--seed N] [--level L] [--years Y]\n"
                 "                 religions every 500 years, the largest faiths with their\n"
                 "                 followers, origins and sects, and the states that hold them\n"
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
    ro.borders = &sim.state().polity_of;
    const std::vector<histsim::view::RouteLine> routes = histsim::view::route_lines(sim);
    const std::vector<histsim::view::RouteLine> moves = histsim::view::migration_lines(sim);
    for (const auto& id : modes) {
        histsim::view::color_cells(sim, id, colors);
        ro.routes = id == "trade" || id == "resources" ? &routes : nullptr;
        const bool peoples = id == "language" || id == "language_family" || id == "culture" || id == "population";
        ro.migrations = peoples ? &moves : nullptr;
        const std::vector<histsim::view::RouteLine> slaving = id == "bondage" ? histsim::view::slave_route_lines(sim)
                                                                              : std::vector<histsim::view::RouteLine>{};
        if (id == "bondage") ro.migrations = &slaving;
        const std::vector<histsim::view::MapMarker> capitals = histsim::view::capital_markers(sim, id);
        ro.markers = capitals.empty() ? nullptr : &capitals;
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

// Towns, cities and polities: a timeline, then the largest polities now.
int polities(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    apply_due_now(sim, o, true);
    advance(sim, o.years, o, true);
    const histsim::SimState& s = sim.state();
    const histsim::World& w = sim.world();
    std::printf("Every 500 years:\n");
    for (const histsim::YearRecord& r : s.history) {
        if (r.year % 500 != 0) continue;
        std::printf("  year %5lld  population %11lld  in towns %5.1f%%  cities %4u  polities %4u (states %3u)  "
                    "largest %9.0f km^2, %9lld people\n",
                    static_cast<long long>(r.year), static_cast<long long>(r.population),
                    r.population > 0 ? 100.0 * static_cast<double>(r.townspeople) / static_cast<double>(r.population) : 0.0,
                    r.cities, r.polities, r.states, r.largest_polity_km2, static_cast<long long>(r.largest_polity_people));
    }
    std::vector<std::uint32_t> living;
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        if (s.polities[id].ended == histsim::kNever) living.push_back(id);
    }
    std::vector<histsim::PolitySize> size(s.polities.size());
    for (std::uint32_t id : living) size[id] = histsim::polity_size(w, s, id);
    std::stable_sort(living.begin(), living.end(), [&](auto a, auto b) { return size[a].people > size[b].people; });
    // How polities ended, and how long they lasted.
    std::map<std::string, std::size_t> fates;
    std::vector<double> lives;
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        const histsim::Polity& p = s.polities[id];
        if (p.ended == histsim::kNever) continue;
        ++fates[histsim::polity_fate_text(p)];
        lives.push_back(static_cast<double>(p.ended - p.founded) / histsim::kTicksPerYear);
    }
    std::sort(lives.begin(), lives.end());
    std::printf("\nHow the %zu polities that ended did so (median life %.0f years, a quarter outlived %.0f):\n",
                lives.size(), lives.empty() ? 0.0 : lives[lives.size() / 2],
                lives.empty() ? 0.0 : lives[lives.size() * 3 / 4]);
    std::vector<std::pair<std::size_t, std::string>> by_count;
    for (const auto& [text, count] : fates) by_count.emplace_back(count, text);
    std::stable_sort(by_count.begin(), by_count.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [count, text] : by_count) std::printf("  %6zu  %s\n", count, text.c_str());
    std::printf("\nLargest polities in year %lld (%zu living, %zu ever):\n", static_cast<long long>(sim.year()),
                living.size(), s.polities.size() - 1);
    for (std::size_t i = 0; i < living.size() && i < 15; ++i) {
        const histsim::Polity& p = s.polities[living[i]];
        const std::uint32_t city = s.city_of[p.capital];
        std::printf("  %-14s %-8s stability %3.0f%% %3zu cells %9.0f km^2 %9lld people  capital %s  since year %lld  (%s)\n",
                    p.name.c_str(), histsim::polity_level(w, s, living[i]), 100.0 * p.stability, size[living[i]].cells,
                    size[living[i]].area_km2, static_cast<long long>(size[living[i]].people),
                    city != 0 ? s.cities[city].name.c_str() : "(no city)",
                    static_cast<long long>(p.founded / histsim::kTicksPerYear), s.cultures[p.culture].name.c_str());
    }
    return 0;
}

// Goods and trade: a timeline, the goods now, and the busiest routes.
int trade(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    apply_due_now(sim, o, true);
    advance(sim, o.years, o, true);
    const histsim::SimState& s = sim.state();
    const histsim::World& w = sim.world();
    const histsim::Content& content = *w.content;
    std::printf("Every 500 years:\n");
    for (const histsim::YearRecord& r : s.history) {
        if (r.year % 500 != 0) continue;
        std::printf("  year %5lld  population %11lld  goods traded %12.0f a year  food carried %9.0f people fed\n",
                    static_cast<long long>(r.year), static_cast<long long>(r.population), r.trade_value,
                    r.food_traded);
    }
    const std::size_t ng = content.goods.size();
    std::size_t markets = 0;
    for (histsim::CellId c = 0; c < w.cell_count(); ++c) markets += s.cell_population[c] > 0 ? 1 : 0;
    std::printf("\nGoods in year %lld (%zu markets):\n", static_cast<long long>(sim.year()), markets);
    std::printf("  %-28s %8s %8s %10s %12s %12s\n", "good", "deposits", "on sale", "base", "mean price", "stocks");
    for (std::size_t g = 0; g < ng && !s.goods_available.empty(); ++g) {
        std::size_t deposits = 0, sold = 0;
        double price = 0.0, stock = 0.0;
        for (histsim::CellId c = 0; c < w.cell_count(); ++c) {
            if (w.deposit(c, g) > 0.0) ++deposits;
            if (s.cell_population[c] == 0) continue;
            if ((s.goods_available[c] >> g) & 1u) ++sold;
            price += s.prices[c * ng + g];
            stock += s.stocks[c * ng + g];
        }
        std::printf("  %-28s %8zu %7.0f%% %10.1f %12.2f %12.0f\n", content.goods[g].name.c_str(), deposits,
                    markets > 0 ? 100.0 * static_cast<double>(sold) / static_cast<double>(markets) : 0.0,
                    content.goods[g].base_price, markets > 0 ? price / static_cast<double>(markets) : 0.0, stock);
    }
    const auto routes = histsim::trade_routes(w, s);
    std::printf("\nBusiest trade routes (%zu):\n", routes.size());
    for (std::size_t i = 0; i < routes.size() && i < 15; ++i) {
        const histsim::TradeRoute& r = routes[i];
        double km = 0.0;
        for (std::uint32_t e : r.edges) km += w.trade_edges[e].distance_km;
        std::printf("  %-30s %3zu markets %7.0f km of links  %10.0f a year\n", r.name.c_str(), r.cells.size(), km,
                    r.value);
    }
    return 0;
}

// War and diplomacy: a timeline, the deadliest wars, the largest realms.
int wars(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    apply_due_now(sim, o, true);
    advance(sim, o.years, o, true);
    const histsim::SimState& s = sim.state();
    const histsim::World& w = sim.world();
    std::printf("Every 500 years:\n");
    std::int64_t last_deaths = 0, last_year = 0;
    std::int64_t raids_since = 0;
    for (const histsim::YearRecord& r : s.history) {
        raids_since += r.raids;
        if (r.year % 500 != 0) continue;
        const double per_year = r.year > last_year ? static_cast<double>(r.war_deaths_total - last_deaths) /
                                                         static_cast<double>(r.year - last_year)
                                                   : 0.0;
        std::printf("  year %5lld  polities %4u (states %3u, nomads %3u)  wars %3u  war deaths %8.0f/yr  "
                    "alliances %3u  subjects %3u  raids %4lld in 500 yrs  largest %9.0f km^2\n",
                    static_cast<long long>(r.year), r.polities, r.states, r.nomads, r.wars, per_year, r.alliances,
                    r.subjects, static_cast<long long>(raids_since), r.largest_polity_km2);
        raids_since = 0;
        last_deaths = r.war_deaths_total;
        last_year = r.year;
    }
    std::vector<std::size_t> order;
    for (std::size_t i = 1; i < s.wars.size(); ++i) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return s.wars[a].deaths > s.wars[b].deaths; });
    std::printf("\nDeadliest wars (%zu in all):\n", s.wars.size() - 1);
    for (std::size_t i = 0; i < order.size() && i < 15; ++i) {
        const histsim::War& war = s.wars[order[i]];
        const long long from = war.started / histsim::kTicksPerYear;
        const long long to = war.ended == histsim::kNever ? static_cast<long long>(sim.year())
                                                          : war.ended / histsim::kTicksPerYear;
        std::printf("  %-36s years %5lld-%5lld  %-12s %zu vs %zu  %9lld dead  %3u cells taken, %3u lost  %s\n",
                    war.name.c_str(), from, to, histsim::war_goal_name(war.goal), war.attackers.size(),
                    war.defenders.size(), static_cast<long long>(war.deaths), war.cells_taken, war.cells_lost,
                    histsim::war_outcome_name(war.outcome));
    }
    // Nomads: conquest dynasties, and the confederations alive now.
    std::size_t dynasties = 0, ever = 0, now = 0;
    for (const histsim::Event& e : s.events) dynasties += e.type == histsim::EventType::ConquestDynasty ? 1 : 0;
    for (const histsim::Polity& p : s.polities) {
        if (!p.nomadic && p.settling_since == histsim::kNever) continue;
        ++ever;
        now += p.ended == histsim::kNever ? 1 : 0;
    }
    std::printf("\nNomads: %zu confederations founded (%zu alive), %zu conquest dynasties.\n", ever, now, dynasties);
    // Realms: each free polity with its subjects.
    std::vector<std::int64_t> people(s.polities.size(), 0);
    std::vector<double> area(s.polities.size(), 0.0);
    for (histsim::CellId c = 0; c < w.cell_count(); ++c) {
        const std::uint32_t top = histsim::top_overlord(s, s.polity_of[c]);
        if (top == 0) continue;
        people[top] += s.cell_population[c];
        area[top] += w.grid.area_km2(c);
    }
    std::vector<std::uint32_t> realms;
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        if (s.polities[id].ended == histsim::kNever && s.polities[id].overlord == 0 && people[id] > 0) realms.push_back(id);
    }
    std::stable_sort(realms.begin(), realms.end(), [&](auto a, auto b) { return area[a] > area[b]; });
    std::printf("\nLargest realms in year %lld (* nomads):\n", static_cast<long long>(sim.year()));
    for (std::size_t i = 0; i < realms.size() && i < 12; ++i) {
        const std::uint32_t id = realms[i];
        std::string subjects, allies;
        for (std::uint32_t q = 1; q < s.polities.size(); ++q) {
            const histsim::Polity& p = s.polities[q];
            if (p.ended == histsim::kNever && p.overlord == id) {
                subjects += (subjects.empty() ? "" : ", ") + p.name +
                            (p.bond == histsim::Bond::Vassal ? " (vassal)" : " (tributary)");
            }
            if (q != id && histsim::allied(s, id, q)) allies += (allies.empty() ? "" : ", ") + p.name;
        }
        std::printf("  %-14s %-13s %9.0f km^2 %10lld people%s%s%s%s\n",
                    (s.polities[id].name + (s.polities[id].nomadic ? "*" : "")).c_str(),
                    histsim::polity_level(w, s, id), area[id], static_cast<long long>(people[id]),
                    subjects.empty() ? "" : "  subjects: ", subjects.c_str(), allies.empty() ? "" : "  allies: ",
                    allies.c_str());
    }
    return 0;
}

// Colonists, folk migrations and chain reactions; language families' lands.
int migrations(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    // Each cell's main language family at the start.
    auto families = [&]() {
        const histsim::SimState& st = sim.state();
        std::vector<std::uint32_t> fam(sim.world().cell_count(), 0);
        for (histsim::CellId c = 0; c < fam.size(); ++c) {
            const histsim::Cohort* lead = nullptr;
            for (const histsim::Cohort& co : st.cohorts_in(c)) {
                if (lead == nullptr || co.total() > lead->total()) lead = &co;
            }
            if (lead != nullptr) fam[c] = st.languages[lead->language].family;
        }
        return fam;
    };
    const std::vector<std::uint32_t> start = families();
    apply_due_now(sim, o, true);
    advance(sim, o.years, o, true);
    const histsim::SimState& s = sim.state();
    const histsim::World& w = sim.world();
    std::printf("Every 500 years:\n");
    std::int64_t colonists = 0, movers = 0;
    std::size_t next = 0;
    std::array<std::size_t, 5> by_cause{};
    for (const histsim::YearRecord& r : s.history) {
        colonists += r.colonists;
        movers += r.folk_movers;
        if (r.year % 500 != 0) continue;
        by_cause.fill(0);
        while (next < s.migrations.size() && s.migrations[next].tick < r.year * histsim::kTicksPerYear) {
            ++by_cause[static_cast<std::size_t>(s.migrations[next].cause)];
            ++next;
        }
        std::printf("  year %5lld  colonists %9lld  folk movers %9lld  folk migrations: famine %3zu, war %3zu, raids %3zu, "
                    "pushed on %3zu, wandering peoples %2zu\n",
                    static_cast<long long>(r.year), static_cast<long long>(colonists), static_cast<long long>(movers),
                    by_cause[0], by_cause[1], by_cause[2], by_cause[3], by_cause[4]);
        colonists = movers = 0;
    }
    // The longest chains: movements that pushed others on.
    std::vector<std::size_t> depth(s.migrations.size(), 0);
    for (std::size_t i = 0; i < s.migrations.size(); ++i) {
        const std::uint32_t up = s.migrations[i].pushed_by;
        if (up != 0) depth[i] = depth[up - 1] + 1;
    }
    std::vector<std::size_t> order(s.migrations.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](auto a, auto b) { return s.migrations[a].people > s.migrations[b].people; });
    static const char* const causes[] = {"famine", "war", "raids", "pushed on", "wandering"};
    std::printf("\nLargest folk migrations (%zu in all):\n", s.migrations.size());
    for (std::size_t k = 0; k < order.size() && k < 12; ++k) {
        const histsim::FolkMovement& m = s.migrations[order[k]];
        std::printf("  year %5lld  %8lld %-14s %-10s %5.0f km  %s\n", static_cast<long long>(m.tick / histsim::kTicksPerYear),
                    static_cast<long long>(m.people), s.languages[m.language].name.c_str(),
                    causes[static_cast<std::size_t>(m.cause)], w.grid.distance_km(m.from, m.to),
                    m.pushed_by != 0 ? ("pushed by the " + s.languages[s.migrations[m.pushed_by - 1].language].name).c_str()
                                     : "");
    }
    std::size_t chains = 0, longest = 0;
    for (std::size_t d : depth) {
        chains += d > 0 ? 1 : 0;
        longest = std::max(longest, d);
    }
    std::printf("  %zu were pushed on by others; the longest chain had %zu links.\n", chains, longest + 1);
    // Tribes and confederations that moved as a whole.
    std::size_t tribes = 0, hordes = 0;
    double tribe_km = 0.0, horde_km = 0.0;
    for (const histsim::FolkMovement& m : s.migrations) {
        if (m.cause != histsim::MigrationCause::Wandering || m.polity >= s.polities.size()) continue;
        const bool nomads = s.polities[m.polity].nomadic;
        (nomads ? hordes : tribes) += 1;
        (nomads ? horde_km : tribe_km) += w.grid.distance_km(m.from, m.to);
    }
    std::printf("\nWandering peoples: %zu tribes (%.0f km on average) and %zu nomad confederations (%.0f km) moved as a "
                "whole:\n",
                tribes, tribes ? tribe_km / static_cast<double>(tribes) : 0.0, hordes,
                hordes ? horde_km / static_cast<double>(hordes) : 0.0);
    std::size_t shown = 0;
    for (std::size_t k = 0; k < order.size() && shown < 12; ++k) {
        const histsim::FolkMovement& m = s.migrations[order[k]];
        if (m.cause != histsim::MigrationCause::Wandering || m.polity >= s.polities.size()) continue;
        const histsim::Polity& pol = s.polities[m.polity];
        std::size_t pushed = 0;
        for (const histsim::FolkMovement& later : s.migrations) pushed += later.pushed_by == order[k] + 1 ? 1 : 0;
        // How long on the road: until it settled (its PolityWandered event).
        std::string road = "still on the road";
        if (pol.trek_record != order[k] + 1) {
            road = "settled";
            for (const histsim::Event& e : s.events) {
                if (e.type == histsim::EventType::PolityWandered && e.value == m.polity && e.tick >= m.tick) {
                    road = std::to_string((e.tick - m.tick) / histsim::kTicksPerYear) + " years on the road";
                    break;
                }
            }
        }
        std::printf("  year %5lld  the %-14s %-13s %8lld people %5.0f km, %s  (%s speakers%s; pushed on %zu peoples)\n",
                    static_cast<long long>(m.tick / histsim::kTicksPerYear), pol.name.c_str(),
                    pol.nomadic ? "(nomads)" : "(tribe)", static_cast<long long>(m.people), w.grid.distance_km(m.from, m.to),
                    road.c_str(), s.languages[m.language].name.c_str(), pol.ended != histsim::kNever ? ", since gone" : "", pushed);
        ++shown;
    }
    // Famine migrations in Bond events (cold, dry centuries) against the rest.
    std::int64_t bond_years = 0;
    for (const histsim::BondEvent& b : s.climate.bond_history) bond_years += b.years;
    std::size_t famine = 0, famine_in_bond = 0;
    for (const histsim::FolkMovement& m : s.migrations) {
        if (m.cause != histsim::MigrationCause::Famine && m.cause != histsim::MigrationCause::Displaced) continue;
        ++famine;
        const std::int64_t year = m.tick / histsim::kTicksPerYear;
        for (const histsim::BondEvent& b : s.climate.bond_history) {
            if (year >= b.start_year && year < b.start_year + b.years) {
                ++famine_in_bond;
                break;
            }
        }
    }
    if (sim.year() > 0 && famine > 0) {
        std::printf("  Bond events covered %.0f%% of the years and saw %.0f%% of the hunger migrations.\n",
                    100.0 * static_cast<double>(bond_years) / static_cast<double>(sim.year()),
                    100.0 * static_cast<double>(famine_in_bond) / static_cast<double>(famine));
    }
    // Language families: land then and now.
    const std::vector<std::uint32_t> now = families();
    std::map<std::uint32_t, std::pair<std::size_t, std::size_t>> land;  // family -> (start, now)
    std::size_t same = 0, total = 0;
    for (std::size_t c = 0; c < now.size(); ++c) {
        if (start[c] != 0) ++land[start[c]].first;
        if (now[c] != 0) ++land[now[c]].second;
        if (start[c] != 0 && now[c] != 0) {
            ++total;
            same += start[c] == now[c] ? 1 : 0;
        }
    }
    std::vector<std::pair<std::uint32_t, std::pair<std::size_t, std::size_t>>> fams(land.begin(), land.end());
    std::stable_sort(fams.begin(), fams.end(), [](const auto& a, const auto& b) {
        return static_cast<long>(a.second.second) - static_cast<long>(a.second.first) >
               static_cast<long>(b.second.second) - static_cast<long>(b.second.first);
    });
    std::printf("\nLanguage families: %.0f%% of inhabited land speaks the same family as at the start.\n",
                total > 0 ? 100.0 * static_cast<double>(same) / static_cast<double>(total) : 0.0);
    std::printf("Families that spread most (cells at the start -> now):\n");
    for (std::size_t k = 0; k < fams.size() && k < 8; ++k) {
        std::printf("  %-16s %4zu -> %4zu\n", s.languages[fams[k].first].name.c_str(), fams[k].second.first,
                    fams[k].second.second);
    }
    return 0;
}

// Religions: how many, how widely organized faiths are held, the largest.
int religions(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    apply_due_now(sim, o, true);
    advance(sim, o.years, o, true);
    const histsim::SimState& s = sim.state();
    const histsim::World& w = sim.world();
    std::printf("Every 500 years:\n");
    std::size_t next = 0, founded = 0, schisms = 0, conversions = 0;
    for (const histsim::YearRecord& r : s.history) {
        while (next < s.events.size() && s.events[next].tick <= r.year * histsim::kTicksPerYear) {
            const histsim::EventType t = s.events[next].type;
            founded += t == histsim::EventType::ReligionFounded ? 1 : 0;
            schisms += t == histsim::EventType::Schism ? 1 : 0;
            conversions += t == histsim::EventType::StateConversion ? 1 : 0;
            ++next;
        }
        if (r.year % 500 != 0) continue;
        std::printf("  year %5lld  religions %4u  in organized faiths %5.1f%%  founded %3zu  schisms %3zu  "
                    "states converted %3zu\n",
                    static_cast<long long>(r.year), r.religions,
                    r.population > 0 ? 100.0 * static_cast<double>(r.organized_faithful) / static_cast<double>(r.population)
                                     : 0.0,
                    founded, schisms, conversions);
        founded = schisms = conversions = 0;
    }
    // Followers and lands of each religion now.
    std::vector<std::int64_t> people(s.religions.size(), 0);
    std::vector<std::size_t> cells(s.religions.size(), 0), states(s.religions.size(), 0);
    for (histsim::CellId c = 0; c < s.faith_of.size(); ++c) {
        people[s.faith_of[c]] += s.cell_population[c];
        ++cells[s.faith_of[c]];
    }
    for (std::uint32_t p = 1; p < s.polities.size(); ++p) {
        if (s.polities[p].ended == histsim::kNever && s.polities[p].religion < states.size()) ++states[s.polities[p].religion];
    }
    std::vector<std::uint32_t> order;
    for (std::uint32_t r = 1; r < s.religions.size(); ++r) {
        if (people[r] > 0) order.push_back(r);
    }
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return people[a] > people[b]; });
    // Organized faiths first: folk beliefs are each people's own.
    std::stable_partition(order.begin(), order.end(), [&](auto r) { return histsim::religion_organized(w, s, r); });
    std::printf("\nLargest religions, organized first (%zu with followers, %zu ever):\n", order.size(), s.religions.size() - 1);
    for (std::size_t k = 0; k < order.size() && k < 15; ++k) {
        const histsim::Religion& r = s.religions[order[k]];
        const std::uint32_t family = histsim::religion_family(s, r.id);
        std::printf("  %-40s %-16s %10lld people %4zu lands %3zu states  since year %lld",
                    r.name.c_str(), histsim::religion_kind_name(w, s, r.id).c_str(),
                    static_cast<long long>(people[r.id]), cells[r.id], states[r.id],
                    static_cast<long long>(r.founded / histsim::kTicksPerYear));
        if (family != r.id && r.parent != 0) std::printf("  (from %s)", s.religions[r.parent].name.c_str());
        std::printf("\n");
    }
    return 0;
}

// Forced labour and the slave trade.
int slavery(const Options& o) {
    Simulation sim = Simulation::create(o.params);
    apply_due_now(sim, o, true);
    advance(sim, o.years, o, true);
    const histsim::SimState& s = sim.state();
    const histsim::World& w = sim.world();
    std::printf("Every 500 years:\n");
    std::int64_t captives = 0, sold = 0;
    for (const histsim::YearRecord& r : s.history) {
        captives += r.captives;
        sold += r.sold;
        if (r.year % 500 != 0) continue;
        std::printf("  year %5lld  enslaved %9lld (%4.1f%% of people)  captives taken %9lld  sold on %9lld\n",
                    static_cast<long long>(r.year), static_cast<long long>(r.enslaved),
                    r.population > 0 ? 100.0 * static_cast<double>(r.enslaved) / static_cast<double>(r.population) : 0.0,
                    static_cast<long long>(captives), static_cast<long long>(sold));
        captives = sold = 0;
    }
    // Where the enslaved are now, and who they are.
    std::vector<std::pair<std::int64_t, histsim::CellId>> places;
    std::map<std::uint32_t, std::int64_t> by_language;
    for (histsim::CellId c = 0; c < w.cell_count(); ++c) {
        const std::int64_t b = histsim::bonded_people(s, c);
        if (b > 0) places.emplace_back(b, c);
    }
    for (const histsim::Cohort& co : s.cohorts) {
        if (co.bonded > 0.0) by_language[co.language] += static_cast<std::int64_t>(co.bonded * static_cast<double>(co.total()) + 0.5);
    }
    std::stable_sort(places.begin(), places.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::printf("\nWhere the enslaved live (%zu lands):\n", places.size());
    for (std::size_t k = 0; k < places.size() && k < 10; ++k) {
        const histsim::CellId c = places[k].second;
        const std::uint32_t city = s.city_of.empty() ? 0 : s.city_of[c];
        const std::uint32_t ruler = s.polity_of.empty() ? 0 : s.polity_of[c];
        std::printf("  %8lld  (%4.1f%% of its people)  %-14s ruled by %s\n", static_cast<long long>(places[k].first),
                    100.0 * static_cast<double>(places[k].first) / static_cast<double>(std::max<std::uint32_t>(1, s.cell_population[c])),
                    city != 0 ? s.cities[city].name.c_str() : "(countryside)", ruler != 0 ? s.polities[ruler].name.c_str() : "no one");
    }
    std::vector<std::pair<std::int64_t, std::uint32_t>> langs;
    for (const auto& [l, n] : by_language) langs.emplace_back(n, l);
    std::stable_sort(langs.begin(), langs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::printf("\nWho they are (by language):\n");
    for (std::size_t k = 0; k < langs.size() && k < 8; ++k) {
        std::printf("  %8lld  %s speakers\n", static_cast<long long>(langs[k].first), s.languages[langs[k].second].name.c_str());
    }
    std::vector<const histsim::SlaveRoute*> routes;
    for (const histsim::SlaveRoute& r : s.slave_routes) routes.push_back(&r);
    std::stable_sort(routes.begin(), routes.end(), [](auto a, auto b) { return a->people > b->people; });
    std::printf("\nBusiest slave routes (%zu in all):\n", routes.size());
    for (std::size_t k = 0; k < routes.size() && k < 10; ++k) {
        const histsim::SlaveRoute& r = *routes[k];
        const std::uint32_t to_city = s.city_of.empty() ? 0 : s.city_of[r.to];
        std::printf("  %8lld people  %5.0f km  to %-14s years %lld-%lld  (last: %s speakers)\n", static_cast<long long>(r.people),
                    w.grid.distance_km(r.from, r.to), to_city != 0 ? s.cities[to_city].name.c_str() : "(countryside)",
                    static_cast<long long>(r.first / histsim::kTicksPerYear), static_cast<long long>(r.last / histsim::kTicksPerYear),
                    s.languages[r.language].name.c_str());
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
        if (cmd == "polities") {
            return polities(parse(args, 2));
        }
        if (cmd == "migrations") {
            return migrations(parse(args, 2));
        }
        if (cmd == "slavery") {
            return slavery(parse(args, 2));
        }
        if (cmd == "religions") {
            return religions(parse(args, 2));
        }
        if (cmd == "wars") {
            return wars(parse(args, 2));
        }
        if (cmd == "trade") {
            return trade(parse(args, 2));
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
