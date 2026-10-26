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
};

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

// Orbital forcing in W/m^2 at a given year of the run, for a world seed.
double orbital_forcing(std::uint64_t seed, std::int64_t year);

}  // namespace histsim
