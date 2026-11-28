// Conditions: what people live through, gathered from the other systems.
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/conditions.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

Simulation run(int years) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(years);
    return sim;
}

void gather(const World& w, SimState& s) {
    ConditionsSystem conditions;
    StepContext ctx{w, s, 1, s.tick};
    conditions.step(ctx);
}

}  // namespace

TEST(conditions_measure_every_peopled_cell) {
    const Simulation sim = run(400);
    const World& w = sim.world();
    const SimState& s = sim.state();
    const Conditions& k = s.conditions;
    const std::size_t n = w.cell_count();
    CHECK_EQ(k.food.size(), n);
    CHECK_EQ(k.slow_sickness.size(), n);
    bool hungry = false, sick = false, room = false;
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) continue;
        CHECK(k.food[c] >= 0.0f && k.food[c] <= static_cast<float>(ConditionsSystem::kMaxFood));
        for (float x : {k.danger[c], k.burden[c], k.bondage[c], k.sickness[c]}) CHECK(x >= 0.0f && x <= 1.0f);
        hungry |= k.food[c] < 1.0f;
        sick |= k.sickness[c] > 0.0f;
        room |= k.room[c] > 0.0f;
    }
    // Forager bands fill their land and some go short; endemic disease kills
    // farmers; open land waits beyond the frontier.
    CHECK(hungry);
    CHECK(sick);
    CHECK(room);
}

TEST(conditions_follow_war_bondage_and_tribute) {
    Simulation sim = run(50);
    const World& w = sim.world();
    SimState s = sim.state();
    CellId c = 0;
    while (s.cell_population[c] < 1000) ++c;
    const double people = static_cast<double>(s.cell_population[c]);
    s.war_deaths.assign(w.cell_count(), 0);
    s.war_deaths[c] = static_cast<std::uint32_t>(people / 10.0);
    for (auto i = s.cohort_begin[c]; i < s.cohort_begin[c + 1]; ++i) s.cohorts[i].bonded = 0.25;
    s.food_taken.assign(w.cell_count(), 0.0);
    const double own = s.env.capacity[c] - (s.food_import.empty() ? 0.0 : s.food_import[c]);
    s.food_taken[c] = own;  // as much taken as was left
    gather(w, s);
    const Conditions& k = s.conditions;
    CHECK_NEAR(k.danger[c], static_cast<double>(s.war_deaths[c]) / people, 1e-6);
    CHECK_NEAR(k.bondage[c], 0.25, 1e-6);
    CHECK_NEAR(k.burden[c], 0.5, 1e-6);
    CHECK_NEAR(k.slow_danger[c], ConditionsSystem::kSlowRate * k.danger[c], 1e-6);
    // What was taken is counted afresh next time.
    CHECK_EQ(s.food_taken[c], 0.0);
}
