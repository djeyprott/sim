// Domestication and farming (M2).
//
// Domestication grows out of foraging. Where a domesticable species grows
// wild, people whose ways lean toward it (plant foods, storage, settled life,
// experiment) and who are pressed for food cultivate or herd it, and over
// centuries of this it becomes domesticated (Zeder 2011; Fuller 2007: wild
// cereals took 1,000-2,000 years of cultivation to domesticate). Progress
// fades when the effort stops, so only steady engagement gets there.
//
// A cell's people know the species domesticated there and those learned from
// farming neighbours or brought by farming migrants. Each cohort gets a share
// of its food from farming, which grows when foraging alone could not feed
// it (the Neolithic ratchet: farming raises the population, which then needs
// farming) and shrinks when harvests fail. Farmland feeds about ten times
// more people per km^2 than foraging, depending on the best crop and herd
// the people know for the place, its soil, floodplain and season, and this
// year's rain and warmth. Farmers have more children and move less
// (demography and migration).
#pragma once

#include "histsim/scheduler.hpp"

namespace histsim {

class FarmingSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "farming"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Production; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Domestication progress per year = rate x wild abundance x
    // domesticability x engagement x food pressure, minus kForgetting x
    // progress.
    static constexpr double kDomesticationRate = 0.005;
    static constexpr double kForgetting = 0.002;
    // Farmland: people per km^2 of a whole cell with the best crop, soil and
    // season. A regional average for early farming: only part of the land was
    // cleared and early crops yielded little (Neolithic Europe ~0.5-2 per
    // km^2 regionally). Later techniques will raise it.
    static constexpr double kCropDensity = 0.6;
    static constexpr double kHerdDensity = 0.15;
    static constexpr double kFloodplainBonus = 1.0;
    // Learning a domesticate from a neighbour that knows it, per year (x 0.5
    // + openness, for 50,000 km^2 cells): farming spreads ~1 km a year, as
    // across Neolithic Europe.
    static constexpr double kLearning = 0.0015;
    // A domestication is a new, independent origin only if nobody within
    // this distance already knows the species.
    static constexpr double kIndependentOriginKm = 1000.0;
    // Farming share: growth per year, times the strain on foraging; and loss
    // per year when farming yields less than foraging would.
    static constexpr double kAdoption = 0.01;
    static constexpr double kAbandonment = 0.03;
    // A species must do at least this well in a place to be grown there.
    static constexpr double kMinSuitability = 0.05;
};

// Farmers' effects on demography and migration.
inline constexpr double kFarmerExtraFertility = 0.25;  // fertility x (1 + 0.25 x farming share)
inline constexpr double kFarmerStay = 0.6;             // migration x (1 - 0.6 x farming share)

// People per km^2 a cell's land would feed if fully farmed with the given
// domesticates, in a normal year.
// Techniques raise crop and herd yields.
double farm_density(const World& world, CellId cell, std::uint32_t domesticates, std::uint64_t techniques = 0);

// This year's harvest relative to a normal year, from rainfall and warmth;
// stores (granaries, irrigation) make good part of a shortfall.
double harvest_factor(const World& world, const SimState& state, CellId cell);

}  // namespace histsim
