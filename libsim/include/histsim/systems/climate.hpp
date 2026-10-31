// Climate and environment systems.
//
// Climate: a one-box global energy balance driven by orbital cycles, solar
// wander and volcanic eruptions, plus year-to-year regional anomalies on a
// coarse grid. Each cell's temperature and rainfall this year are its
// baseline plus those anomalies.
//
// Environment: turns this year's climate into foraging capacity, scaled by a
// game stock that regrows and that heavy hunting depletes.
//
// Human forcing (CO2, land use, aerosols) arrives with the industrial era (M4).
#pragma once

#include "histsim/scheduler.hpp"

namespace histsim {

class ClimateSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "climate"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Climate; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Energy balance: C dT/dt = F - lambda T. C is an ocean mixed layer
    // (~8 W yr m^-2 K^-1); lambda = 1.3 W m^-2 K^-1 gives ~3 K per CO2 doubling.
    static constexpr double kHeatCapacity = 8.0;
    static constexpr double kFeedback = 1.3;
    static constexpr double kInternalNoiseC = 0.05;    // K per year
    static constexpr double kEruptionChance = 1.0 / 60.0;  // large eruptions per year
    static constexpr double kVolcanicDecay = 0.37;     // aerosols clear in ~1 year
    static constexpr double kPolarAmplification = 0.8; // extra warming at the poles, x z^2
    static constexpr double kRegionalTempSd = 0.6;     // K, year-to-year
    static constexpr double kRegionalTempPersistence = 0.6;
    static constexpr double kRegionalPrecipSd = 0.15;  // fraction, year-to-year
    static constexpr double kRegionalPrecipPersistence = 0.4;
    static constexpr double kPrecipPerDegree = 0.03;   // global rainfall +3% per K

    // Bond events: about every 1,470 years (+-500), lasting 150-400 years,
    // cooling high northern latitudes by 0.8-2 degrees C at their peak and
    // weakening monsoons (summer rain at low latitudes) by up to ~30%.
    static constexpr double kBondPacingYears = 1470.0;
    static constexpr double kBondPacingJitterYears = 1000.0;  // full width
    static constexpr double kBondFirstWithinYears = 1500.0;
};

// How strong a Bond event is in a given year of it: 0 before and after,
// rising over its first quarter, holding to 60%, then fading.
double bond_intensity(const BondEvent& bond, std::int64_t year);

// Its effect on one cell this year: degrees C added and the factor rainfall
// is multiplied by.
struct BondEffect {
    double temp_c = 0.0;
    double rain_factor = 1.0;
};
BondEffect bond_effect(const World& world, const BondEvent& bond, std::int64_t year, CellId cell);

// When the natural cycle brings the first Bond event of a run.
std::int64_t first_bond_year(std::uint64_t seed);

class EnvironmentSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "environment"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Environment; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kGameRegrowth = 0.25;     // logistic regrowth per year
    static constexpr double kGameInflow = 0.01;       // animals wandering in from elsewhere
    static constexpr double kHuntingPressure = 0.10;  // depletion per unit crowding above...
    static constexpr double kSustainableCrowding = 0.6;  // ...this share of capacity
    static constexpr double kGameShareOfCapacity = 0.6;  // capacity = climate x (0.4 + 0.6 game)
};

// People a land cell can feed by foraging alone this year, from its climate
// and game stock.
double forage_capacity(const World& world, const SimState& state, CellId cell);

// People it can feed this year: foraging for the share of food not farmed,
// plus farmland for the share that is (state.env.farming, farm_capacity).
// Used by the environment and farming systems and by interventions.
double environment_capacity(const World& world, const SimState& state, CellId cell);

// Orbital forcing in W/m^2 at a given year of the run, for a world seed.
double orbital_forcing(std::uint64_t seed, std::int64_t year);

}  // namespace histsim
