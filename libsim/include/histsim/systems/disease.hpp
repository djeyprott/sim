// Disease (M2).
//
// Endemic diseases (malaria, worms, livestock fevers, tuberculosis) add to
// the yearly chance of death where they are present, more so for farmers and
// dense populations: the price of settled life. Epidemic diseases (plague,
// influenza, pox, measles) arrive in a cell, sweep through it in one outbreak
// whose size follows from R0 and the share still susceptible (the SIR final
// size), kill a share of the infected by age, leave survivors immune, and
// spread to neighbours with the size of the outbreak. Only a population above
// the disease's critical size keeps it circulating; elsewhere it burns out and
// waits to be brought back, so crowd diseases need crowds (Bartlett 1957).
//
// The system sets each cell's extra chance of death by age this year
// (Environment::disease_mortality); demography applies it with the other
// deaths, so it runs just before demography.
//
// Zoonoses emerge where people keep their animal hosts (data/diseases.json).
// A population that has never met a disease is fully susceptible, so its
// first epidemic is the worst: virgin-soil epidemics follow from the model.
#pragma once

#include "histsim/scheduler.hpp"

namespace histsim {

class DiseaseSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "disease"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Demography; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // New susceptibles each year as a share of the immune (births, mostly).
    static constexpr double kSusceptibleInflow = 0.04;
    // Chance an outbreak seeds a neighbour this year, per unit of attack rate.
    static constexpr double kEpidemicSpread = 4.0;
    // Chance per year an endemic disease reaches a suitable neighbour, per
    // unit of burden.
    static constexpr double kEndemicSpread = 0.01;
    // A new emergence counts as an origin if no cell this close has it and
    // it never emerged this close before.
    static constexpr double kOriginKm = 1500.0;
};

// How well a disease's niche fits a cell (1 if it has none).
double disease_suitability(const World& world, const DiseaseDef& disease, CellId cell);

// Share of a population of fully susceptible fraction `s` that an epidemic
// with reproduction number `r` infects before it ends: the root of
// a = s (1 - e^(-r a)). 0 when r s <= 1.
double outbreak_size(double r, double s);

// R0 scaled by population density: closer living, more contacts.
double contact_factor(double people_per_km2);

}  // namespace histsim
