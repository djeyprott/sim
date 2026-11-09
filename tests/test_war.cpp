// War, diplomacy and nomads (M3 slice 3).
#include <algorithm>
#include <string>
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/diplomacy.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/systems/war.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

// A world at its start with two polities set up by hand: polity 1 holds a
// cell and all but one of its inhabited neighbours, polity 2 that one.
struct Setup {
    Simulation sim;
    SimState s;
    CellId heart = kInvalidCell, victim = kInvalidCell;
};

Setup two_polities() {
    WorldParams p;
    p.grid_level = 4;
    Setup x{Simulation::create(p), {}, kInvalidCell, kInvalidCell};
    x.s = x.sim.state();
    const World& w = x.sim.world();
    for (CellId c = 0; c < w.cell_count() && x.heart == kInvalidCell; ++c) {
        if (x.s.cell_population[c] < 1000) continue;
        std::size_t inhabited = 0;
        for (CellId nb : w.grid.neighbors(c)) inhabited += x.s.cell_population[nb] >= 1000 ? 1 : 0;
        if (inhabited == w.grid.neighbors(c).size()) x.heart = c;
    }
    CHECK(x.heart != kInvalidCell);
    x.s.polities.resize(3);
    for (std::uint32_t id = 1; id <= 2; ++id) {
        Polity& pol = x.s.polities[id];
        pol.id = id;
        pol.name = id == 1 ? "Strong" : "Weak";
        pol.culture = x.s.cohorts_in(x.heart).front().culture;
    }
    x.s.polities[1].capital = x.heart;
    x.s.polity_of[x.heart] = 1;
    for (CellId nb : w.grid.neighbors(x.heart)) {
        if (x.victim == kInvalidCell) {
            x.victim = nb;
            x.s.polity_of[nb] = 2;
        } else {
            x.s.polity_of[nb] = 1;
        }
    }
    x.s.polities[2].capital = x.victim;
    // The victim's people are few.
    for (auto i = x.s.cohort_begin[x.victim]; i < x.s.cohort_begin[x.victim + 1]; ++i) {
        Cohort& co = x.s.cohorts[i];
        for (int b = 0; b < kAgeBins; ++b) co.female[b] /= 10, co.male[b] /= 10;
    }
    x.s.ledger.initial_population = 0;  // not checked here
    x.s.normalize_cohorts(w.cell_count());
    return x;
}

void run_war_system(const World& w, SimState& s, int years) {
    WarSystem war;
    for (int y = 0; y < years; ++y) {
        StepContext ctx{w, s, 7, s.tick};
        war.step(ctx);
        s.tick += kTicksPerYear;
    }
}

}  // namespace

TEST(war_sides_gather_allies_overlords_and_vassals) {
    Setup x = two_polities();
    SimState& s = x.s;
    // A third polity allied to the victim, a fourth the attacker's vassal.
    s.polities.resize(5);
    for (std::uint32_t id = 3; id <= 4; ++id) {
        s.polities[id].id = id;
        s.polities[id].name = id == 3 ? "Friend" : "Client";
    }
    s.alliances.push_back({2, 3, 0, kNever});
    s.polities[4].overlord = 1;
    s.polities[4].bond = Bond::Vassal;
    CHECK(allied(s, 3, 2));
    CHECK(!allied(s, 1, 2));
    CHECK_EQ(top_overlord(s, 4), 1u);
    CHECK_EQ(top_overlord(s, 1), 1u);
    const std::uint32_t id = start_war(x.sim.world(), s, 0, 1, 2, WarGoal::Conquest);
    const War& war = s.wars[id];
    CHECK_EQ(war.attackers.front(), 1u);
    CHECK_EQ(war.defenders.front(), 2u);
    CHECK(std::find(war.defenders.begin(), war.defenders.end(), 3u) != war.defenders.end());
    CHECK(std::find(war.attackers.begin(), war.attackers.end(), 4u) != war.attackers.end());
    CHECK_EQ(war_between(s, 1, 2), id);
    CHECK_EQ(war_between(s, 4, 3), id);  // on opposite sides of it
    CHECK_EQ(war_between(s, 1, 4), 0u);
    CHECK(!war.name.empty());
}

TEST(war_the_strong_take_the_weak_and_the_dead_are_counted) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const auto strengths = polity_strengths(w, s);
    CHECK(strengths[1] > 2.0 * strengths[2]);
    const std::int64_t people = s.total_population();
    const std::int64_t deaths = s.ledger.deaths;
    const std::uint32_t id = start_war(w, s, s.tick, 1, 2, WarGoal::Conquest);
    run_war_system(w, s, 60);
    const War& war = s.wars[id];
    CHECK(war.ended != kNever);
    CHECK(war.outcome == WarOutcome::AttackersWon);
    CHECK_EQ(s.polity_of[x.victim], 1u);  // a one-cell polity is annexed outright
    CHECK(war.terms == PeaceTerms::Annexed);
    CHECK(war.score >= 0.0 && war.score <= 1.0);
    CHECK(war.cells_a > 1 && war.cells_d == 1);
    CHECK(war.deaths > 0);
    // Deaths are in the ledger; refugees only moved.
    CHECK_EQ(s.ledger.deaths - deaths, s.war_deaths_total);
    CHECK_EQ(s.total_population(), people - s.war_deaths_total);
    // A truce follows (or the loser is gone).
    CHECK(war_between(s, 1, 2) == 0);
}

TEST(war_tributaries_pay_and_subjects_can_be_found_above) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    run_war_system(w, s, 1);  // sizes the war arrays
    s.polities[2].overlord = 1;
    s.polities[2].bond = Bond::Tributary;
    const std::size_t ng = w.content->goods.size();
    CHECK(s.stocks.size() == w.cell_count() * ng || s.stocks.empty());
    if (s.stocks.empty()) s.stocks.assign(w.cell_count() * ng, 0.0);
    std::size_t salt = 0;
    while (w.content->goods[salt].id != "salt") ++salt;
    s.stocks[x.victim * ng + salt] = 100.0;
    s.stocks[x.heart * ng + salt] = 0.0;
    // One cell against six: far too weak to rebel.
    run_war_system(w, s, 1);
    CHECK_EQ(s.polities[2].overlord, 1u);
    CHECK_NEAR(s.stocks[x.heart * ng + salt], 100.0 * WarSystem::kTribute, 1e-9);
    CHECK_NEAR(s.stocks[x.victim * ng + salt], 100.0 * (1.0 - WarSystem::kTribute), 1e-9);
    CHECK_EQ(top_overlord(s, 2), 1u);
}

TEST(war_nomad_confederations_reach_far_and_raise_big_levies) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const double settled_reach = polity_reach(w, s, 1);
    const double settled_strength = polity_strengths(w, s)[1];
    s.polities[1].nomadic = true;
    // Without bronze arms, kNomadReachEarly times as far; with them, kNomadReach.
    const double factor = nomad_reach_factor(w, s, 1);
    CHECK(factor == PolitySystem::kNomadReach || factor == PolitySystem::kNomadReachEarly);
    CHECK_NEAR(polity_reach(w, s, 1), factor * settled_reach, 1e-9);
    CHECK(polity_strengths(w, s)[1] > WarSystem::kNomadLevy * settled_strength);  // more levy, firmer control
    CHECK(std::string(polity_level(w, s, 1)) == "confederation");
}

TEST(war_plunder_keeps_a_confederations_followers) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    s.polities[1].nomadic = true;
    const double lean = polity_strengths(w, s)[1];
    s.polities[1].plunder = 1e12;  // more than enough for every follower
    CHECK_NEAR(polity_strengths(w, s)[1], lean * (1.0 + WarSystem::kPlunderStrength), 1e-6 * lean);
    // Plunder fades a year at a time.
    run_war_system(w, s, 1);
    CHECK(s.polities[1].plunder < 1e12 && s.polities[1].plunder >= 1e12 * (1.0 - WarSystem::kPlunderFade) * 0.999);
    // Tribute received counts as plunder.
    s.polities[1].plunder = 0.0;
    s.polities[2].overlord = 1;
    s.polities[2].bond = Bond::Tributary;
    const std::size_t ng = w.content->goods.size();
    if (s.stocks.empty()) s.stocks.assign(w.cell_count() * ng, 0.0);
    std::size_t salt = 0;
    while (w.content->goods[salt].id != "salt") ++salt;
    s.stocks[x.victim * ng + salt] = 100.0;
    run_war_system(w, s, 1);
    CHECK(s.polities[1].plunder > 0.0);
}

TEST(war_nomads_raid_the_richest_farmers_within_reach) {
    WorldParams p;
    p.grid_level = 5;
    const Simulation sim = Simulation::create(p);
    const World& w = sim.world();
    SimState s = sim.state();
    // Herders at one cell; a rich town two cells away, and nobody else farms.
    CellId camp = kInvalidCell, town = kInvalidCell;
    std::vector<CellId> near;
    for (CellId c = 0; c < w.cell_count() && town == kInvalidCell; ++c) {
        if (s.cell_population[c] < 500) continue;
        cells_within(w, c, WarSystem::kRaidKm, near);
        for (CellId x : near) {
            const double d = w.grid.distance_km(c, x);
            if (d > 300.0 && s.cell_population[x] >= 500) {
                camp = c;
                town = x;
                break;
            }
        }
    }
    CHECK(town != kInvalidCell);
    CHECK(std::find(near.begin(), near.end(), camp) != near.end());
    s.env.farming.assign(w.cell_count(), 0.0);
    s.env.pastoral.assign(w.cell_count(), 0.0);
    s.env.farming[town] = 1.0;
    s.urban[town] = 0.3;
    CHECK(raid_wealth(s, town) > 0.0);
    CHECK_EQ(raid_wealth(s, camp), 0.0);
    s.env.farming[camp] = 1.0;
    s.env.pastoral[camp] = 1.0;
    CHECK_EQ(raid_wealth(s, camp), 0.0);  // herders have little to take
    s.polities.resize(2);
    s.polities[1].id = 1;
    s.polities[1].name = "Riders";
    s.polities[1].capital = camp;
    s.polities[1].nomadic = true;
    s.polities[1].culture = s.cohorts_in(camp).front().culture;
    s.polity_of[camp] = 1;
    run_war_system(w, s, 200);
    CHECK(s.last_raid[town] != kNever);
    bool logged = false;
    for (const Event& e : s.events) logged |= e.type == EventType::Raid && e.cell == town && e.value == 1;
    CHECK(logged);
    // Only the rich town was worth raiding.
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (c != town) CHECK_EQ(s.last_raid[c], kNever);
    }
}

TEST(war_conquest_dynasties_settle_down) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    s.polities[1].nomadic = true;
    s.polities[1].settling_since = s.tick;
    PolitySystem polities;
    for (std::int64_t y = 0; y <= PolitySystem::kSettleYears; ++y) {
        CHECK(s.polities[1].nomadic);
        StepContext ctx{w, s, 3, s.tick};
        polities.step(ctx);
        s.tick += kTicksPerYear;
        if (s.polities[1].ended != kNever) break;  // fell meanwhile: nothing left to test
    }
    if (s.polities[1].ended == kNever) {
        CHECK(!s.polities[1].nomadic);
        CHECK_EQ(s.polities[1].settling_since, kNever);
    }
}

// Field armies: each war's leaders raise one; an army stands on land, is
// living only while its war goes on, and breaks below kBroken of its men;
// battles are between armies of the two sides of a war.
TEST(war_field_armies_march_fight_and_disband) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const std::uint32_t id = start_war(w, s, s.tick, 1, 2, WarGoal::Conquest);
    run_war_system(w, s, 60);
    const War& war = s.wars[id];
    CHECK(war.army_a != 0 && war.army_a < s.armies.size());
    CHECK_EQ(s.armies[war.army_a].polity, 1u);
    CHECK(war.ended != kNever);
    for (std::size_t i = 1; i < s.armies.size(); ++i) CHECK(s.armies[i].to != kNever);  // the war is over

    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(5000);
    const SimState& t = sim.state();
    CHECK(t.armies.size() > 10);
    CHECK(!t.battles.empty());
    for (std::size_t i = 1; i < t.armies.size(); ++i) {
        const Army& a = t.armies[i];
        CHECK_EQ(a.id, static_cast<std::uint32_t>(i));
        CHECK(a.cell < sim.world().cell_count() && sim.world().is_land[a.cell]);
        CHECK(a.raised > 0.0 && a.men <= a.raised + 1e-9);
        CHECK(a.war != 0 && a.war < t.wars.size());
        if (a.to == kNever) {
            CHECK_EQ(t.wars[a.war].ended, kNever);
            CHECK(a.men >= WarSystem::kBroken * a.raised);
            CHECK(t.wars[a.war].army_a == i || t.wars[a.war].army_d == i);
        }
    }
    for (const Battle& b : t.battles) {
        CHECK(b.winner != 0 && b.winner < t.armies.size() && b.loser != 0 && b.loser < t.armies.size());
        CHECK_EQ(t.armies[b.winner].war, b.war);
        CHECK_EQ(t.armies[b.loser].war, b.war);
        CHECK(t.armies[b.winner].polity != t.armies[b.loser].polity);
        CHECK(b.dead >= 0);
    }
}

TEST(diplomacy_neighbours_of_one_culture_and_faith_think_well_of_each_other) {
    Setup x = two_polities();
    SimState& s = x.s;
    const std::vector<std::vector<std::uint32_t>> borders{{}, {2}, {1}};
    update_relations(x.sim.world(), s, s.tick, borders);
    CHECK_EQ(s.relations.size(), std::size_t{1});
    const Relation* r = find_relation(s, 2, 1);
    CHECK(r != nullptr);
    CHECK(r->opinion > 0.3);  // culture and faith shared, met this year
    CHECK(!cause_for_war(s, 1, 2));
    // A war begun sours them at once, and its memory lingers after peace.
    const double before = r->opinion;
    note_war_begun(s, 1, 2, s.tick);
    CHECK(opinion_between(s, 1, 2) < before - 0.25);
    note_war_ended(s, 1, 2, s.tick);
    const double remembered = opinion_target(x.sim.world(), s, *find_relation(s, 1, 2), s.tick);
    const double forgotten = opinion_target(x.sim.world(), s, *find_relation(s, 1, 2), s.tick + 200 * kTicksPerYear);
    CHECK(remembered < forgotten - 0.5);
}

TEST(diplomacy_lost_land_is_claimed_until_won_back_or_forgotten) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const std::vector<std::vector<std::uint32_t>> borders{{}, {2}, {1}};
    update_relations(w, s, s.tick, borders);
    const double calm = opinion_between(s, 1, 2);
    // Polity 2 loses a cell to polity 1.
    CellId lost = kInvalidCell;
    for (CellId nb : w.grid.neighbors(x.victim)) {
        if (s.polity_of[nb] == 1 && nb != x.heart) lost = nb;
    }
    CHECK(lost != kInvalidCell);
    s.polity_of[lost] = 2;
    note_land_lost(s, lost, 2, s.tick);
    s.polity_of[lost] = 1;
    s.tick += kTicksPerYear;
    update_relations(w, s, s.tick, borders);
    CHECK_EQ(claims_on(s, 2, 1), 1u);
    CHECK_EQ(claims_on(s, 1, 2), 0u);
    CHECK(cause_for_war(s, 2, 1));
    CHECK(!cause_for_war(s, 1, 2));
    for (int y = 0; y < 30; ++y) {
        s.tick += kTicksPerYear;
        update_relations(w, s, s.tick, borders);
    }
    CHECK(opinion_between(s, 1, 2) < calm - 0.1);
    // Won back, the claim is dropped.
    s.polity_of[lost] = 2;
    s.tick += kTicksPerYear;
    update_relations(w, s, s.tick, borders);
    CHECK(s.claims.empty());
    CHECK_EQ(claims_on(s, 2, 1), 0u);
    // A claim not pressed lapses.
    s.polity_of[lost] = 1;
    note_land_lost(s, lost, 2, s.tick);
    s.tick += (Diplomacy::kClaimYears + 1) * kTicksPerYear;
    update_relations(w, s, s.tick, borders);
    CHECK(s.claims.empty());
}

TEST(diplomacy_an_overlord_at_war_elsewhere_abandons_its_subject) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    // Polity 3 pays tribute to polity 2; polity 4 is a stranger.
    s.polities.resize(5);
    for (std::uint32_t id = 3; id <= 4; ++id) {
        s.polities[id].id = id;
        s.polities[id].name = id == 3 ? "Payer" : "Stranger";
        s.polities[id].capital = x.heart;
    }
    s.polities[3].overlord = 2;
    s.polities[3].bond = Bond::Tributary;
    // Attacked while its overlord is at peace, the subject is defended.
    const std::uint32_t first = start_war(w, s, 0, 4, 3, WarGoal::Conquest);
    CHECK_EQ(war_between(s, 4, 2), first);
    CHECK(find_relation(s, 3, 2)->trust > 0.5);
    s.wars[first].ended = 0;
    // Its overlord now at war with polity 1, the subject is left alone, and remembers.
    start_war(w, s, 0, 1, 2, WarGoal::Conquest);
    const double opinion = opinion_between(s, 3, 2);
    const std::uint32_t second = start_war(w, s, 0, 4, 3, WarGoal::Conquest);
    CHECK(std::find(s.wars[second].defenders.begin(), s.wars[second].defenders.end(), 2u) == s.wars[second].defenders.end());
    CHECK(find_relation(s, 3, 2)->trust < 0.4);
    CHECK(opinion_between(s, 3, 2) < opinion);
    CHECK(s.events.back().type == EventType::Abandoned || s.events[s.events.size() - 2].type == EventType::Abandoned);
}

TEST(diplomacy_withheld_tribute_is_cause_for_war_for_a_generation) {
    Setup x = two_polities();
    SimState& s = x.s;
    Relation& r = relation_of(s, 1, 2, s.tick);
    CHECK(!cause_for_war(s, 1, 2));
    r.withheld = s.tick;
    r.withheld_by = 2;
    CHECK(tribute_withheld(s, 1, 2));
    CHECK(!tribute_withheld(s, 2, 1));
    CHECK(cause_for_war(s, 1, 2));
    CHECK(!cause_for_war(s, 2, 1));
    s.tick += static_cast<Tick>(Diplomacy::kGrievanceYears + 1) * kTicksPerYear;
    CHECK(!cause_for_war(s, 1, 2));
}

TEST(diplomacy_peace_gives_back_land_taken_in_the_war_but_not_before) {
    Setup x = two_polities();
    SimState& s = x.s;
    const World& w = x.sim.world();
    CellId old = kInvalidCell, taken = kInvalidCell;
    for (CellId nb : w.grid.neighbors(x.heart)) {
        if (s.polity_of[nb] != 1) continue;
        if (old == kInvalidCell) {
            old = nb;
        } else if (taken == kInvalidCell) {
            taken = nb;
        }
    }
    CHECK(taken != kInvalidCell);
    // Polity 2 lost `old` long ago and `taken` in this war.
    note_land_lost(s, old, 2, s.tick);
    s.tick += 10 * kTicksPerYear;
    const Tick began = s.tick;
    s.tick += kTicksPerYear;
    note_land_lost(s, taken, 2, s.tick);
    CHECK(!return_war_gains(s, began, {1}, {2}));  // nothing of polity 1's was taken
    CHECK(return_war_gains(s, began, {2}, {1}));
    CHECK_EQ(s.polity_of[taken], 2u);
    CHECK_EQ(s.polity_of[old], 1u);
}
