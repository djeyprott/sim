// People systems: cohort demography and migration (M1 foragers).
#pragma once

#include "histsim/scheduler.hpp"

namespace histsim {

// Yearly births, deaths and aging for every cohort.
//
// Mortality follows the average hunter-gatherer life table (Gurven & Kaplan
// 2007; life expectancy 31 at birth, 38 more at 15) and fertility a natural-
// fertility age pattern with total fertility 5.4 (Marlowe 2005). Both come
// from tools/vital_rates.py. Crowding links them to the land: as a cell fills
// toward its foraging capacity, fertility falls to replacement (poorer
// nutrition, longer birth spacing); beyond capacity, famine raises mortality.
// So populations grow ~0.7% a year on empty land and settle near capacity.
class DemographySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "demography"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Demography; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kFamineMortalityPerCrowding = 2.5;  // mortality x(1 + 2.5 (crowding - 1))
    static constexpr double kMaxMortalityMultiplier = 6.0;
    static constexpr double kMinFertilityMultiplier = 0.2;
};

// Yearly moves between neighboring cells.
//
// Some people move every year (0.5%: bands splitting off to explore); crowded
// cells send more (4.5% a year at capacity, rising with the square of
// crowding); a cell with nothing to eat empties. Each cohort's
// movers travel together, as a band, to one neighbor chosen by spare
// capacity and ease of travel (elevation change). Young adults move most.
// Afterwards, groups smaller than 20 people or 10% of their cell join the
// cell's largest group, so arrivals into settled land are absorbed while
// bands entering empty land carry their group with them.
class MigrationSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "migration"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Migration; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kExploratoryRate = 0.005;
    static constexpr double kRateAtCapacity = 0.04;
    static constexpr double kMaxRate = 0.15;
    static constexpr double kClimbCostPerKm = 2.0;  // a 500 m climb halves a destination's appeal
    static constexpr std::int64_t kMinGroupSize = 20;
    static constexpr double kMinGroupShare = 0.10;
};

// Appends one YearRecord per year (last in the tick).
class HistorySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "history"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Events; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;
};

// A cohort of n people in the stationary age structure of the life table,
// split between the sexes by the sex ratio at birth. Integer-exact.
Cohort make_stable_cohort(CellId cell, std::uint32_t group, std::uint32_t n);

// Life-table helpers exposed for tests and the inspector.
double forager_death_probability(int age_bin);
double forager_fertility(int age_bin);
double stable_age_share(int age_bin);
double female_share_of_births();
double replacement_fertility_multiplier();

}  // namespace histsim
