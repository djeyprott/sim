// Content packs: the data that defines what exists in a world (species now;
// crops, goods, technologies, diseases and institutions in later milestones).
//
// The default pack lives in data/*.json and is compiled into the library, so
// every build carries exactly the content it was tested with. Its hash goes
// into the world hash, so a snapshot made with different content is refused.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace histsim {

// [min, best_from, best_to, max]: 0 outside min..max, 1 in best_from..best_to,
// linear in between.
struct Trapezoid {
    double lo = 0.0, best_lo = 0.0, best_hi = 0.0, hi = 0.0;

    [[nodiscard]] double operator()(double x) const noexcept {
        if (x <= lo || x >= hi) return 0.0;
        if (x < best_lo) return (x - lo) / (best_lo - lo);
        if (x > best_hi) return (hi - x) / (hi - best_hi);
        return 1.0;
    }
};

enum class SpeciesKind : std::uint8_t { Plant, Animal };

struct SpeciesDef {
    std::string id;
    std::string name;
    SpeciesKind kind = SpeciesKind::Plant;
    std::string food;
    Trapezoid coldest_month_c;
    Trapezoid warmest_month_c;
    Trapezoid annual_precip_mm;
    Trapezoid rain_season;
    Trapezoid elevation_m;
    double river_affinity = 0.0;
    int native_regions = 1;
    double range_km = 2000.0;
    double domesticability = 0.5;
    double food_value = 0.5;
};

inline constexpr std::size_t kMaxSpecies = 32;  // presence is a 32-bit mask per cell

struct Content {
    std::vector<SpeciesDef> species;
    std::uint64_t hash = 0;  // of the source text
};

// Parse and validate a species pack (data/species.json format). Throws
// std::runtime_error with the file position or field path on any problem.
Content load_content(std::string_view species_json, std::string_view source_name = "species.json");

// The pack compiled into this build (parsed once).
const Content& default_content();

}  // namespace histsim
