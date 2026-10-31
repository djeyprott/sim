// Towns, cities and polities (M3 slice 1).
#include <set>

#include "histsim/simulation.hpp"
#include "histsim/systems/polity.hpp"
#include "testing.hpp"

using namespace histsim;

TEST(polity_control_fades_with_distance) {
    CHECK_EQ(polity_control(0.0, 100.0), 1.0);
    CHECK_NEAR(polity_control(100.0, 100.0), 0.5, 1e-12);
    CHECK(polity_control(300.0, 100.0) < polity_control(200.0, 100.0));
    CHECK(polity_control(300.0, 200.0) > polity_control(300.0, 100.0));  // more reach, more control
}

TEST(polity_reach_grows_with_administration) {
    WorldParams p;
    p.grid_level = 3;
    const Simulation sim = Simulation::create(p);
    SimState s = sim.state();
    const World& w = sim.world();
    CellId c = 0;
    while (!w.is_land[c]) ++c;
    s.techniques[c] = 0;
    CHECK_NEAR(polity_reach_km(w, s, c), PolitySystem::kBaseReachKm, 1e-9);
    const auto& techs = w.content->techniques;
    std::uint64_t writing = 0, bureaucracy = 0;
    for (std::size_t k = 0; k < techs.size(); ++k) {
        if (techs[k].id == "writing") writing = std::uint64_t{1} << k;
        if (techs[k].id == "bureaucracy") bureaucracy = std::uint64_t{1} << k;
    }
    s.techniques[c] = writing;
    const double with_writing = polity_reach_km(w, s, c);
    CHECK_NEAR(with_writing, 2.0 * PolitySystem::kBaseReachKm * 1.03, 1e-6);  // writing: +1 administration, +0.1 contact
    s.techniques[c] = writing | bureaucracy;
    CHECK(polity_reach_km(w, s, c) > with_writing);
}

TEST(polity_chiefdoms_cycle_and_states_form_with_writing) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(3500);
    const SimState& s = sim.state();
    const World& w = sim.world();
    // Consistency: every ruled cell is inhabited and ruled by a living
    // polity; every living polity holds its capital.
    std::set<std::uint32_t> ruling;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (s.polity_of[c] == 0) continue;
        CHECK(s.cell_population[c] > 0);
        CHECK_EQ(s.polities[s.polity_of[c]].ended, kNever);
        ruling.insert(s.polity_of[c]);
    }
    std::size_t living = 0, ended = 0, states = 0;
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        const Polity& pol = s.polities[id];
        CHECK_EQ(pol.id, id);
        if (pol.ended != kNever) {
            ++ended;
            CHECK(pol.ended >= pol.founded);
            continue;
        }
        ++living;
        // A capital can fall in the same year the polity is checked; allow it
        // to be either held or empty until next year's move.
        CHECK(s.polity_of[pol.capital] == id || ruling.count(id) == 1);
        if (polity_has_writing(w, s, id)) ++states;
        const PolitySize z = polity_size(w, s, id);
        CHECK(z.area_km2 < 3.0e6);  // no empires without later techniques
    }
    CHECK(living > 5);
    CHECK(ended > living);  // chiefdoms rise and fall
    CHECK(states >= 1);
    CHECK(s.history.back().states == states);
    // Cities: unique names, enough townspeople where they stand.
    std::set<std::string> names;
    std::size_t cities = 0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (s.city_of[c] == 0) continue;
        ++cities;
        CHECK(s.urban[c] * s.cell_population[c] >= SettlementSystem::kCityPeople / 3.0);
        CHECK_EQ(s.cities[s.city_of[c]].cell, c);
    }
    for (std::size_t i = 1; i < s.cities.size(); ++i) names.insert(s.cities[i].name);
    CHECK(cities > 10);
    CHECK_EQ(names.size(), s.cities.size() - 1);
    // Towns only where farming feeds people.
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (s.urban[c] > 0.02) CHECK(s.env.farming[c] > 0.3);
    }
}
