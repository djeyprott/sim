// Culture and language change (M2).
//
// Culture and language are separate, linked systems. Every cohort carries
// its local version of its culture's traits and a dialect point. Each year:
//
//   - traits drift at random, are pulled toward the same culture nearby and,
//     more weakly (more for open cultures), toward neighbouring cultures, and
//     adapt to the land (data/culture.json);
//   - dialects drift and are pulled toward speakers of the same language in
//     the cell and next door, so separated groups grow apart;
//   - in a cell with several languages, speakers of the smaller ones shift
//     to the largest (keeping their customs), and minorities slowly adopt
//     the larger culture (keeping their language);
//   - every language changes its words and sounds (language.hpp).
//
// Every 20 years, a language whose dialects have spread too far apart
// splits: the outlying dialects become a daughter language, so families grow
// trees. Cultures split the same way on their traits. A language or culture
// with no one left is extinct.
#pragma once

#include <array>
#include <span>
#include <string>
#include <utility>

#include "histsim/scheduler.hpp"

namespace histsim {

class CultureSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "culture"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::CultureTech; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Pull per year toward the population-weighted mean of the same culture
    // in this and neighbouring cells, and toward other cultures there (times
    // 0.5 + openness).
    static constexpr double kCultureCohesion = 0.01;
    static constexpr double kCultureContact = 0.004;
    // Dialects: pull toward the same language nearby, and random drift (per
    // year, per dimension, standard deviation).
    static constexpr double kDialectCohesion = 0.005;
    static constexpr double kDialectDrift = 0.01;
    // Splits: when the outlying dialect (or local culture) is this far from
    // the language's (culture's) average.
    static constexpr double kLanguageSplitDistance = 0.5;
    static constexpr double kCultureSplitDistance = 0.3;
    static constexpr std::int64_t kSplitCheckYears = 20;
    static constexpr std::int64_t kMinSplitPopulation = 150;
    // A new daughter language has been diverging for a while before it is
    // recognized as one: it gets this many years of change at once.
    static constexpr int kSplitCatchUpYears = 300;
    // Shift: share of a minority's people who switch per year, times the gap
    // between the largest group's share of the cell and their own.
    static constexpr double kLanguageShiftRate = 0.03;
    static constexpr double kAssimilationRate = 0.015;
};

// The circumstances that push culture traits at a cell, each 0-1 (see
// data/culture.json).
std::array<double, kCultureVarCount> culture_variables(const World& world, const SimState& state, CellId cell);

// Where a trait settles under those circumstances.
double trait_target(const CultureTrait& trait, const std::array<double, kCultureVarCount>& vars);

// Found a new people on `cells` (a starting group, or settlers): a new
// culture with traits suited to those cells plus a random character of its
// own, and a new language family.
//
// With `history_years` > 0 the family already has a past: its proto-language
// split again and again over that many years before the run, so it starts
// with about one language per kInitialAreaPerLanguageKm2 of land, each on a
// contiguous region, and its extinct ancestors ("Proto-...") are kept for the
// family tree. With 0 (settlers) it is a single new language.
struct FoundedPeople {
    std::uint32_t culture = 0;
    std::uint32_t family = 0;                  // the family's root language
    std::vector<std::uint32_t> cell_language;  // language spoken on each of `cells`
    std::array<double, kMaxTraits> traits{};   // the culture's starting traits
};
FoundedPeople found_people(const World& world, SimState& state, std::uint64_t seed, std::uint32_t entity,
                           std::span<const CellId> cells, double history_years);

// Starting families have been diversifying for 3,000-7,000 years.
inline constexpr double kFamilyHistoryMinYears = 3000.0;
inline constexpr double kFamilyHistoryRangeYears = 4000.0;
// Land per language at the start of a run (about 6 cells at grid level 5).
inline constexpr double kInitialAreaPerLanguageKm2 = 300000.0;
// Cohesion and drift above are for cells of this area; smaller cells pull
// harder on their neighbours and drift faster, so languages and cultures
// cover the same land at every grid level.
inline constexpr double kReferenceCellAreaKm2 = 50000.0;

// Counts of living languages, families and cultures.
struct PeopleCounts {
    std::uint32_t languages = 0, families = 0, cultures = 0;
};
PeopleCounts count_peoples(const SimState& state);

}  // namespace histsim
