// Placeholder forager systems for M0.
//
// They exist to exercise the scheduler, RNG, double buffering, conservation
// and replay with something that behaves like a population: logistic growth
// toward carrying capacity and diffusion into open land. M1 replaces them with
// cohort demography (age-sex pyramids, life tables) and push-pull migration.
#pragma once

#include "histsim/scheduler.hpp"

namespace histsim {

// Yearly births and deaths. Birth rate 3.5%; death rate 3.0% plus a crowding
// term that equals the birth rate at carrying capacity, so N settles near K.
class ForagerDemography final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "forager_demography"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Demography; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kBirthRate = 0.035;
    static constexpr double kBaseDeathRate = 0.030;
};

// Yearly moves to neighboring land. Crowded cells send more people, and
// neighbors with spare capacity receive more. Moves conserve people exactly.
class ForagerMigration final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "forager_migration"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Migration; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kMaxEmigrationRate = 0.05;
};

}  // namespace histsim
