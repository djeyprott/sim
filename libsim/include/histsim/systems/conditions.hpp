// Conditions: what people are living through, gathered once a year.
#pragma once

#include "histsim/scheduler.hpp"

namespace histsim {

// Gathers, for every cell, what the other systems produced this year into
// SimState::conditions: food per head after trade, stores and relief
// (economy), deaths in war and raids (war, last year's), food taken by the
// annona and tribute (economy, war), the share held in bondage (cohorts),
// disease deaths (demography), the people the land could still feed (room)
// and the townspeople the surplus would feed beyond those there
// (settlement). It changes nothing else: systems that respond to people's
// circumstances read these instead of keeping thresholds of their own
// (design: "Conditions and migration design").
//
// Runs after demography and before migration, so movers respond to this
// year's harvest, trade and disease and last year's wars.
class ConditionsSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "conditions"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Migration; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Slow averages close this share of the gap each year (about 20 years).
    static constexpr double kSlowRate = 0.05;
    // Food per head is recorded up to this many times need.
    static constexpr double kMaxFood = 4.0;
};

}  // namespace histsim
