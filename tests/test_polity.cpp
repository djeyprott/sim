// Towns, cities and polities (M3 slice 1).
#include <algorithm>
#include <set>

#include "histsim/simulation.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/systems/war.hpp"
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
    CHECK_NEAR(with_writing, 1.5 * PolitySystem::kBaseReachKm * 1.03, 1e-6);  // writing: +0.5 administration, +0.1 contact
    s.techniques[c] = writing | bureaucracy;
    CHECK(polity_reach_km(w, s, c) > with_writing);
}

TEST(polity_chiefdoms_cycle_and_states_form_with_writing) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(5200);
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
        // No continent-spanning empires yet. (Level 4 runs somewhat ahead of
        // level 5 and its cells are 200,000 km^2, so the bound is loose: its
        // first states come about 2,000 years before this, and by then the
        // largest empires in history held some 5 million km^2.)
        CHECK(z.area_km2 < 8.0e6);
    }
    CHECK(living > 5);
    CHECK(ended > living);  // chiefdoms rise and fall
    // War and diplomacy: wars were fought and ended properly; no polity is
    // its own overlord, directly or through a chain; subjects serve living
    // overlords; alliances join living polities.
    CHECK(s.wars.size() > 1);
    std::size_t over = 0;
    for (std::size_t i = 1; i < s.wars.size(); ++i) {
        const War& war = s.wars[i];
        CHECK_EQ(war.id, i);
        CHECK(!war.attackers.empty() && !war.defenders.empty());
        if (war.ended != kNever) {
            ++over;
            CHECK(war.ended >= war.started);
            CHECK(war.outcome != WarOutcome::Ongoing);
        }
    }
    CHECK(over > 0);
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        const Polity& pol = s.polities[id];
        if (pol.ended != kNever || pol.overlord == 0) continue;
        CHECK(pol.overlord != id);
        CHECK(pol.bond != Bond::None);
        CHECK_EQ(s.polities[pol.overlord].ended, kNever);
        const std::uint32_t top = top_overlord(s, id);
        CHECK(top != id && s.polities[top].overlord == 0);
    }
    for (const Alliance& a : s.alliances) {
        CHECK(a.a < a.b);
        if (a.ended == kNever) CHECK(s.polities[a.a].ended == kNever && s.polities[a.b].ended == kNever);
    }
    CHECK_EQ(s.ledger.initial_population + s.ledger.births - s.ledger.deaths + s.ledger.added, s.total_population());
    CHECK(s.history.back().states == states);
    // States (with writing) have arisen by now, though one may have fallen
    // just before this year.
    std::uint32_t most_states = 0;
    for (const YearRecord& r : s.history) most_states = std::max(most_states, r.states);
    CHECK(most_states >= 1);
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

namespace {

// A world at its start with one polity ruling a cell and its neighbours.
struct OnePolity {
    Simulation sim;
    SimState s;
    CellId capital = kInvalidCell;
};

OnePolity one_polity() {
    WorldParams p;
    p.grid_level = 4;
    OnePolity x{Simulation::create(p), {}, kInvalidCell};
    x.s = x.sim.state();
    const World& w = x.sim.world();
    for (CellId c = 0; c < w.cell_count() && x.capital == kInvalidCell; ++c) {
        if (x.s.cell_population[c] < 1000) continue;
        bool all = true;
        for (CellId nb : w.grid.neighbors(c)) all = all && x.s.cell_population[nb] >= 1000;
        if (all) x.capital = c;
    }
    x.s.polities.resize(2);
    Polity& pol = x.s.polities[1];
    pol.id = 1;
    pol.name = "Testland";
    pol.capital = x.capital;
    pol.culture = x.s.cohorts_in(x.capital).front().culture;
    x.s.polity_of[x.capital] = 1;
    for (CellId nb : w.grid.neighbors(x.capital)) x.s.polity_of[nb] = 1;
    return x;
}

void run_polities(const World& w, SimState& s, int years) {
    PolitySystem polities;
    for (int y = 0; y < years; ++y) {
        StepContext ctx{w, s, 11, s.tick};
        polities.step(ctx);
        s.tick += kTicksPerYear;
    }
}

}  // namespace

TEST(polity_stability_falls_in_famine_and_names_the_cause) {
    OnePolity x = one_polity();
    const World& w = x.sim.world();
    SimState fed = x.s;
    run_polities(w, fed, 1);
    CHECK_EQ(fed.polities[1].pressure[static_cast<std::size_t>(Pressure::Famine)], 0.0);
    // The land feeds only half its people.
    SimState hungry = x.s;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (hungry.polity_of[c] == 1) hungry.env.capacity[c] = 0.5 * hungry.cell_population[c];
    }
    run_polities(w, hungry, 1);
    CHECK(hungry.polities[1].pressure[static_cast<std::size_t>(Pressure::Famine)] > 0.5);
    CHECK(hungry.polities[1].stability < fed.polities[1].stability);
    // Kept hungry, it breaks up, and the famine is named as the cause.
    for (int y = 0; y < 200 && hungry.polities[1].ended == kNever; ++y) {
        for (CellId c = 0; c < w.cell_count(); ++c) {
            if (hungry.cell_population[c] > 0) hungry.env.capacity[c] = 0.5 * hungry.cell_population[c];
        }
        run_polities(w, hungry, 1);
    }
    const Polity& pol = hungry.polities[1];
    CHECK(pol.ended != kNever);
    CHECK(pol.fate == PolityFate::Collapsed);
    CHECK_EQ(pol.fate_cause, static_cast<std::uint8_t>(Pressure::Famine));
    CHECK(polity_fate_text(pol) == "collapsed under famine");
    // A chiefdom dissolves: nobody rules its land.
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (hungry.polity_of[c] != 0) CHECK(hungry.polities[hungry.polity_of[c]].founded > pol.founded);
    }
}

TEST(polity_a_broken_state_leaves_a_core_under_its_capital) {
    OnePolity x = one_polity();
    const World& w = x.sim.world();
    SimState& s = x.s;
    const auto& techs = w.content->techniques;
    for (std::size_t k = 0; k < techs.size(); ++k) {
        if (techs[k].id == "writing") s.techniques[x.capital] |= std::uint64_t{1} << k;
    }
    CHECK(polity_has_writing(w, s, 1));
    s.polities[1].stability = 0.0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (s.polity_of[c] == 1) s.env.capacity[c] = 0.2 * s.cell_population[c];
    }
    for (int y = 0; y < 100 && s.polities[1].ended == kNever; ++y) run_polities(w, s, 1);
    CHECK(s.polities[1].fate == PolityFate::Collapsed);
    const std::uint32_t heir = s.polity_of[x.capital];
    CHECK(heir > 1);
    CHECK_EQ(s.polities[heir].parent, 1u);
    CHECK_EQ(s.polities[heir].capital, x.capital);
    CHECK_EQ(s.polities[heir].ended, kNever);
}

TEST(polity_rulers_reign_die_and_are_succeeded) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(3000);
    const SimState& s = sim.state();
    // Every living polity (founded before this year) has a reigning ruler of
    // its own, of a house.
    std::size_t ruled = 0;
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        const Polity& pol = s.polities[id];
        if (pol.ended != kNever || pol.founded + kTicksPerYear >= s.tick) continue;
        CHECK(pol.ruler != 0 && pol.ruler < s.rulers.size());
        const Ruler& r = s.rulers[pol.ruler];
        CHECK_EQ(r.polity, id);
        CHECK(r.end == ReignEnd::Reigning);
        CHECK(r.dynasty != 0 && r.dynasty < s.dynasties.size());
        CHECK(r.born <= r.reign_start);
        ++ruled;
    }
    CHECK(ruled > 3);
    // Rulers die and heirs succeed: some reigns are over, with heirs of the
    // same house and, now and then, usurpers founding new ones.
    std::size_t over = 0, successions = 0, usurpations = 0;
    for (std::size_t i = 1; i < s.rulers.size(); ++i) {
        if (s.rulers[i].end != ReignEnd::Reigning) {
            ++over;
            CHECK(s.rulers[i].reign_end >= s.rulers[i].reign_start);
        }
    }
    for (const Event& e : s.events) {
        successions += e.type == EventType::Succession ? 1 : 0;
        usurpations += e.type == EventType::Usurpation ? 1 : 0;
    }
    CHECK(over > 10);
    CHECK(successions > usurpations);
    CHECK(usurpations > 0);
}

TEST(polity_institutions_are_founded_and_inherited) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(4000);
    const SimState& s = sim.state();
    const World& w = sim.world();
    // When writing was first invented, if it was.
    Tick writing = kNever;
    for (const Invention& v : s.inventions) {
        if (w.content->techniques[v.technique].id == "writing") writing = std::min(writing, v.tick);
    }
    std::size_t founded = 0;
    for (const Event& e : s.events) {
        if (e.type != EventType::InstitutionFounded) continue;
        ++founded;
        const auto id = static_cast<std::size_t>(e.value / 16);
        const auto k = static_cast<std::size_t>(e.value % 16);
        CHECK(id > 0 && id < s.polities.size());
        CHECK(k < kInstitutionCount);
        // Law codes and standing armies need writing.
        if (k == static_cast<std::size_t>(Institution::LawCode) || k == static_cast<std::size_t>(Institution::Army)) {
            CHECK(e.tick >= writing);
        }
    }
    CHECK(founded > 0);
}

// Provinces: their land is their realm's, their seats are held, their
// governors are figures; provinces that broke away founded polities whose
// first ruler was their governor.
TEST(polity_provinces_governors_and_revolts) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(5000);
    const SimState& s = sim.state();
    const World& w = sim.world();
    CHECK_EQ(s.province_of.size(), w.cell_count());
    std::size_t living = 0, revolts = 0;
    for (const Province& pv : s.provinces) {
        if (pv.id == 0) continue;
        CHECK_EQ(pv.id, static_cast<std::uint32_t>(&pv - s.provinces.data()));
        CHECK(pv.loyalty >= 0.0 && pv.loyalty <= 1.0);
        CHECK(!pv.name.empty());
        if (pv.ended == kNever) {
            ++living;
            CHECK(pv.polity != 0 && s.polities[pv.polity].ended == kNever);
            CHECK_EQ(s.polity_of[pv.seat], pv.polity);
            CHECK(pv.governor != 0 && pv.governor < s.figures.size());
            CHECK(s.figures[pv.governor].roles & kRoleGovernor);
        }
        if (pv.became != 0) {
            ++revolts;
            CHECK(pv.ended != kNever);
            CHECK(pv.became < s.polities.size());
            const Polity& rebel = s.polities[pv.became];
            CHECK_EQ(rebel.parent, pv.polity);
            // Founded by its governor, or by the leader of a peasant uprising.
            CHECK(rebel.founder == pv.governor ||
                  (rebel.founder != 0 && (s.figures[rebel.founder].roles & kRoleRebel)));
            // The governor reigned, unless he died before his first year.
            if (rebel.ruler != 0 && rebel.founder != 0 && s.rulers[rebel.ruler].figure == rebel.founder) {
                CHECK(s.figures[rebel.founder].roles & kRoleRuler);
            }
        }
    }
    for (CellId c = 0; c < w.cell_count(); ++c) {
        const std::uint32_t v = s.province_of[c];
        if (v == 0) continue;
        CHECK(v < s.provinces.size());
        CHECK_EQ(s.provinces[v].polity, s.polity_of[c]);
        CHECK(s.provinces[v].ended == kNever);
    }
    CHECK(living > 0);
    CHECK(revolts > 0);
}

// Peasant uprisings: each has a leader (a figure) and an outcome; a
// province that broke away is a polity founded by the leader; unrest
// stays in 0-1; the dead are counted.
TEST(polity_peasant_uprisings) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(5000);
    const SimState& s = sim.state();
    CHECK(!s.uprisings.empty());
    std::size_t crushed = 0, won = 0;
    for (const Uprising& u : s.uprisings) {
        CHECK(u.polity != 0 && u.polity < s.polities.size());
        CHECK(u.leader != 0 && u.leader < s.figures.size());
        CHECK(s.figures[u.leader].roles & kRoleRebel);
        CHECK(u.rebels > 0);
        CHECK(u.dead >= 0);
        if (u.outcome == UprisingOutcome::Crushed) ++crushed;
        if (u.outcome == UprisingOutcome::BrokeAway) {
            ++won;
            CHECK(u.became != 0 && u.became < s.polities.size());
            CHECK_EQ(s.polities[u.became].founder, u.leader);
            CHECK_EQ(s.polities[u.became].parent, u.polity);
        }
    }
    CHECK(crushed > 0);
    for (const Polity& q : s.polities) CHECK(q.unrest >= 0.0 && q.unrest <= 1.0);
    for (const Province& v : s.provinces) CHECK(v.unrest >= 0.0 && v.unrest <= 1.0);
    CHECK_EQ(s.ledger.initial_population + s.ledger.births - s.ledger.deaths + s.ledger.added, s.total_population());
}
