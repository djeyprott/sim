// Forced labour and the slave trade.
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/bondage.hpp"
#include "histsim/systems/religion.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

struct Start {
    Simulation sim;
    SimState s;
    CellId a = kInvalidCell, b = kInvalidCell;  // two well-peopled cells of different lineages
};

Start start() {
    WorldParams p;
    p.grid_level = 4;
    Start x{Simulation::create(p), {}, kInvalidCell, kInvalidCell};
    x.s = x.sim.state();
    const World& w = x.sim.world();
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (x.s.cell_population[c] < 5000) continue;
        if (x.a == kInvalidCell) {
            x.a = c;
        } else if (x.s.cultures[x.s.cohorts_in(c).front().culture].root !=
                       x.s.cultures[x.s.cohorts_in(x.a).front().culture].root &&
                   w.grid.distance_km(c, x.a) < 1000.0) {
            x.b = c;
            break;
        }
    }
    CHECK(x.a != kInvalidCell && x.b != kInvalidCell);
    return x;
}

std::int64_t enslaved(const SimState& s) {
    double n = 0.0;
    for (const Cohort& co : s.cohorts) n += co.bonded * static_cast<double>(co.total());
    return static_cast<std::int64_t>(n + 0.5);
}

// A polity ruling cell `c` from it, of that cell's people.
std::uint32_t rule(SimState& s, std::size_t cells, CellId c) {
    if (s.polities.empty()) s.polities.emplace_back();
    if (s.polity_of.size() != cells) s.polity_of.assign(cells, 0);
    Polity p;
    p.id = static_cast<std::uint32_t>(s.polities.size());
    p.capital = c;
    p.culture = s.cohorts_in(c).front().culture;
    p.language = s.cohorts_in(c).front().language;
    s.polities.push_back(p);
    s.polity_of[c] = p.id;
    return p.id;
}

}  // namespace

TEST(bondage_captives_are_carried_off_keeping_their_language) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const std::int64_t people = s.total_population();
    const std::uint32_t language = s.cohorts_in(x.b).front().language;
    RngStream rng(1, 0, Stream::Bondage, 0);
    std::vector<Cohort> out;
    const std::int64_t taken = take_captives(s, x.b, x.a, 0.2, rng, out);
    CHECK(taken > 0);
    for (const Cohort& co : out) {
        CHECK_EQ(co.cell, x.a);
        CHECK(co.bonded == 1.0);
    }
    s.cohorts.insert(s.cohorts.end(), out.begin(), out.end());
    s.normalize_cohorts(w.cell_count());
    CHECK_EQ(s.total_population(), people);  // carried off, not killed
    CHECK(std::abs(enslaved(s) - taken) <= 1);
    bool kept = false;
    for (const Cohort& co : s.cohorts_in(x.a)) kept |= co.language == language && co.bonded > 0.99;
    CHECK(kept);
}

TEST(bondage_kin_and_fellow_believers_are_spared) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const std::uint32_t captor = rule(s, w.cell_count(), x.a);
    const double share = BondageSystem::kWarCaptives;
    CHECK(captive_share(w, s, captor, x.b, share) == share);  // strangers
    CHECK(captive_share(w, s, captor, x.a, share) == share * BondageSystem::kKinCaptives);  // own lineage
    // A universal faith held by captors and victims alike protects them.
    ReligionSystem religion;
    StepContext ctx{w, s, 5, s.tick};
    religion.step(ctx);
    std::uint8_t universal = 0;
    for (std::size_t k = 0; k < w.content->religion_kinds.size(); ++k) {
        if (w.content->religion_kinds[k].missionary > 0.0) universal = static_cast<std::uint8_t>(k);
    }
    Religion faith;
    faith.id = static_cast<std::uint32_t>(s.religions.size());
    faith.kind = universal;
    faith.name = "Teaching of Test";
    s.religions.push_back(faith);
    s.faith_of[x.a] = s.faith_of[x.b] = faith.id;
    s.polities[captor].religion = faith.id;
    CHECK(captive_share(w, s, captor, x.b, share) == share * BondageSystem::kFaithCaptives);
}

TEST(bondage_surplus_captives_are_sold_where_wanted_and_some_go_free) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    // A market town at b that wants bound labour, and captives held at a,
    // where no one wants them.
    s.urban.assign(w.cell_count(), 0.0);
    s.urban[x.b] = 0.3;
    s.trade_value.assign(w.cell_count(), 1.0);
    CHECK(bondage_demand(w, s, x.b) > 0.05);
    CHECK(bondage_demand(w, s, x.a) < 0.02);
    for (auto i = s.cohort_begin[x.a]; i < s.cohort_begin[x.a + 1]; ++i) s.cohorts[i].bonded = 0.1;
    const std::int64_t before = enslaved(s);
    const std::int64_t people = s.total_population();
    BondageSystem bondage;
    StepContext ctx{w, s, 5, s.tick};
    bondage.step(ctx);
    CHECK(s.sold > 0);
    CHECK_EQ(s.total_population(), people);
    std::int64_t at_b = 0;
    for (const Cohort& co : s.cohorts_in(x.b)) at_b += static_cast<std::int64_t>(co.bonded * static_cast<double>(co.total()) + 0.5);
    CHECK(at_b > 0);
    bool route = false;
    for (const SlaveRoute& r : s.slave_routes) route |= r.from == x.a && r.to == x.b && r.people == s.sold;
    CHECK(route);
    // Some went free this year (manumission), none were lost.
    CHECK(enslaved(s) < before);
    CHECK(enslaved(s) > before * 9 / 10);
}

TEST(bondage_merging_groups_keeps_the_enslaved) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    Cohort copy = s.cohorts[s.cohort_begin[x.a]];
    copy.bonded = 1.0;
    s.cohorts[s.cohort_begin[x.a]].bonded = 0.0;
    const std::int64_t expected = copy.total();
    s.cohorts.push_back(copy);  // the same key: merged by normalize
    s.normalize_cohorts(w.cell_count());
    CHECK(std::abs(enslaved(s) - expected) <= 1);
}
