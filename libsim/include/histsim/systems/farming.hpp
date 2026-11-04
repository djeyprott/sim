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
    // A river's floodplain (World::floodplain_share) is watered by the river
    // whatever the rain, its silt renewed each flood (fertility
    // kSiltFertility), and yields kFloodplainYield times rain-fed land of
    // the same crop: so the Nile and the Euphrates feed narrow, crowded
    // strips through the desert.
    static constexpr double kSiltFertility = 0.9;
    static constexpr double kFloodplainYield = 3.0;
    // Rain-fed crops fail often in dry land: away from rivers, crops x
    // clamp((rainfall - kDryFarmingMm) / kDryFarmingSpanMm, kDryFloor, 1).
    // Where the grass is good but the rain is not, herding wins: the steppe.
    static constexpr double kDryFarmingMm = 200.0;
    static constexpr double kDryFarmingSpanMm = 400.0;
    static constexpr double kDryFloor = 0.05;
    // Learning a domesticate from a neighbour that knows it, per year (x 0.5
    // + openness, for 50,000 km^2 cells): farming spreads ~1 km a year, as
    // across Neolithic Europe.
    static constexpr double kLearning = 0.0005;
    // Herd animals travel with mobile herders, who meet their neighbours
    // across wide seasonal ranges: learning one is x (1 + kHerdMobility x
    // mobility x herding), herding being the share of the people's food
    // from herds. Horses crossed the steppe in a few centuries.
    static constexpr double kHerdMobility = 12.0;
    // A domestication is a new, independent origin only if nobody within
    // this distance already knows the species.
    static constexpr double kIndependentOriginKm = 1000.0;
    // Farming share: growth per year, times the strain on foraging; and loss
    // per year when farming yields less than foraging would.
    static constexpr double kAdoption = 0.01;
    static constexpr double kAbandonment = 0.03;
    // Shifting cultivation: away from rivers, rainforest soils give out
    // after a few harvests and the plot lies fallow for a decade or more, so
    // only a part of the land bears crops at any time (crops x kRainforestFallow;
    // savanna x kSavannaFallow). The floodplains of big rivers (rice paddies,
    // the varzea; World::floodplain_share) are farmed every year.
    static constexpr double kRainforestFallow = 0.35;
    static constexpr double kSavannaFallow = 0.75;
    // A species must do at least this well in a place to be grown there.
    static constexpr double kMinSuitability = 0.05;
    // Crops follow the climate people are used to (Environment::normal_shift_c):
    // where it has moved by a step, each species' suitability and the growing
    // season are worked out again, so a cooling drives wheat south and
    // shortens the summers, and a warming opens the north and parches the
    // dry margins.
    static constexpr double kCropShiftStepC = 0.25;
    static constexpr double kCropRainStep = 0.025;
    // Mixed crops, famine foods and kin make good the harvest buffer's share
    // of a shortfall against what people are used to, but at most this much
    // of a normal harvest: enough for a bad year, not for a climate shock
    // (and a lasting change changes the crops instead).
    static constexpr double kMaxMadeGood = 0.2;
};

// Farmers' effects on demography and migration.
inline constexpr double kFarmerExtraFertility = 0.25;  // on empty land, fertility x (1 + 0.25 x farming share)
inline constexpr double kFarmerStay = 0.6;             // migration x (1 - 0.6 x farming share x (1 - pastoral))

// People per km^2 a cell's land would feed if fully farmed with the given
// domesticates, in a normal year.
// Techniques raise crop and herd yields.
// With a state, under the climate its people are used to (its crop
// suitability and seasons); without, under the world's own climate.
double farm_density(const World& world, CellId cell, std::uint32_t domesticates, std::uint64_t techniques = 0,
                    const SimState* state = nullptr);

// The same, split into what crops and herds feed (farm_density = crops +
// half the herds where there are crops, else the herds alone).
struct FarmParts {
    double crops = 0.0, herds = 0.0;
};
FarmParts farm_parts(const World& world, CellId cell, std::uint32_t domesticates, std::uint64_t techniques = 0,
                     const SimState* state = nullptr);

// How well a domesticate can be raised in the cell (rain-fed, or on its
// floodplain) under the climate its people are used to.
[[nodiscard]] double raisable(const World& world, const SimState& state, CellId cell, std::size_t species);

// How good a biome's grazing is for herds (x the herd yield): open
// grassland best, then shrubland and savanna; forests and deserts poor.
double pasture_quality(Biome biome);

// Share of a cell's farm food that comes from herds (1 = pastoralists).
[[nodiscard]] inline double pastoral_share(const FarmParts& p) {
    const double total = p.crops > 0.0 ? p.crops + 0.5 * p.herds : p.herds;
    return total > 0.0 ? (p.crops > 0.0 ? 0.5 * p.herds : p.herds) / total : 0.0;
}

// This year's harvest relative to a normal year (the climate people are
// used to), from rainfall and warmth; mixed crops, granaries and kin make
// good part of a shortfall (at most kMaxMadeGood of a normal harvest).
double harvest_factor(const World& world, const SimState& state, CellId cell);

}  // namespace histsim
