// Religion: folk beliefs, temple cults, rulers' faiths and schisms.
#include <algorithm>
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/religion.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

struct Start {
    Simulation sim;
    SimState s;
    CellId home = kInvalidCell;
};

// A world at its start and a well-peopled cell whose neighbours are all
// peopled land.
Start start() {
    WorldParams p;
    p.grid_level = 4;
    Start x{Simulation::create(p), {}, kInvalidCell};
    x.s = x.sim.state();
    const World& w = x.sim.world();
    for (CellId c = 0; c < w.cell_count() && x.home == kInvalidCell; ++c) {
        if (x.s.cell_population[c] < 8000) continue;
        bool peopled = true;
        for (CellId nb : w.grid.neighbors(c)) peopled = peopled && x.s.cell_population[nb] > 0;
        if (peopled) x.home = c;
    }
    CHECK(x.home != kInvalidCell);
    return x;
}

void believe(const World& w, SimState& s, int years = 1) {
    ReligionSystem religion;
    for (int y = 0; y < years; ++y) {
        StepContext ctx{w, s, 5, s.tick};
        religion.step(ctx);
        s.tick += kTicksPerYear;
    }
}

std::uint32_t main_root(const SimState& s, CellId c) {
    const Cohort* lead = nullptr;
    for (const Cohort& co : s.cohorts_in(c)) {
        if (lead == nullptr || co.total() > lead->total()) lead = &co;
    }
    return lead == nullptr ? 0 : s.cultures[lead->culture].root;
}

// A polity ruling a cell and its neighbours from it.
std::uint32_t rule(const World& w, SimState& s, CellId capital) {
    const std::size_t n = w.cell_count();
    if (s.polities.empty()) s.polities.emplace_back();
    if (s.polity_of.size() != n) s.polity_of.assign(n, 0);
    Polity p;
    p.id = static_cast<std::uint32_t>(s.polities.size());
    p.capital = capital;
    p.name = "Testland";
    s.polities.push_back(p);
    s.polity_of[capital] = p.id;
    for (CellId nb : w.grid.neighbors(capital)) s.polity_of[nb] = p.id;
    return p.id;
}

// A universal faith founded at a cell.
std::uint32_t found_universal(const World& w, SimState& s, CellId at) {
    const auto& kinds = w.content->religion_kinds;
    std::uint8_t kind = 0;
    for (std::size_t k = 0; k < kinds.size(); ++k) {
        if (kinds[k].missionary > 0.0) kind = static_cast<std::uint8_t>(k);
    }
    Religion r;
    r.id = static_cast<std::uint32_t>(s.religions.size());
    r.kind = kind;
    r.name = "Teaching of Test";
    r.origin = at;
    s.religions.push_back(r);
    s.faith_of[at] = r.id;
    return r.id;
}

}  // namespace

TEST(religion_every_people_has_its_own_folk_beliefs_which_do_not_spread) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    believe(w, s);
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (s.cell_population[c] == 0) {
            CHECK_EQ(s.faith_of[c], 0u);
            continue;
        }
        const std::uint32_t f = s.faith_of[c];
        CHECK(f != 0);
        CHECK(!religion_organized(w, s, f));
        CHECK_EQ(s.religions[f].culture_root, main_root(s, c));
    }
    // Plant a neighbouring people's folk faith here: it neither spreads nor
    // lasts, since the people here have their own.
    CellId other = kInvalidCell;
    for (CellId c = 0; c < w.cell_count() && other == kInvalidCell; ++c) {
        if (s.faith_of[c] != 0 && s.faith_of[c] != s.faith_of[x.home]) other = c;
    }
    CHECK(other != kInvalidCell);
    const std::uint32_t foreign = s.faith_of[other];
    const std::uint32_t own = s.faith_of[x.home];
    s.faith_of[x.home] = foreign;
    believe(w, s, 300);
    CHECK_EQ(s.faith_of[x.home], own);
    for (CellId nb : w.grid.neighbors(x.home)) {
        CHECK(s.faith_of[nb] != foreign || main_root(s, nb) == s.religions[foreign].culture_root);
    }
}

TEST(religion_a_ritual_minded_city_state_founds_a_temple_cult) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const std::uint32_t p = rule(w, s, x.home);
    // A town of 5,000+ whose people love ritual.
    s.urban.assign(w.cell_count(), 0.0);
    s.urban[x.home] = 6000.0 / static_cast<double>(s.cell_population[x.home]);
    for (auto i = s.cohort_begin[x.home]; i < s.cohort_begin[x.home + 1]; ++i) {
        for (std::size_t t = 0; t < w.content->traits.size(); ++t) {
            if (w.content->traits[t].id == "ritual") s.cohorts[i].traits[t] = 0.9f;
        }
    }
    const std::uint32_t folk = [&] {
        believe(w, s);
        return s.faith_of[x.home];
    }();
    for (int year = 0; year < 3000 && s.faith_of[x.home] == folk; ++year) believe(w, s);
    const std::uint32_t cult = s.faith_of[x.home];
    CHECK(cult != folk);
    CHECK(religion_organized(w, s, cult));
    CHECK_EQ(s.religions[cult].parent, folk);
    CHECK_EQ(s.religions[cult].polity, p);
    CHECK_EQ(s.polities[p].religion, cult);
    bool logged = false;
    for (const Event& e : s.events) logged |= e.type == EventType::ReligionFounded && e.value == cult;
    CHECK(logged);
}

TEST(religion_rulers_bring_their_realm_to_their_faith) {
    // The same world twice: with a ruler holding a universal faith, and with
    // the faith but no ruler.
    auto converted = [](bool ruled) {
        Start x = start();
        SimState& s = x.s;
        const World& w = x.sim.world();
        s.techniques.assign(w.cell_count(), ~std::uint64_t{0});  // a wide reach
        believe(w, s);
        if (ruled) rule(w, s, x.home);
        const std::uint32_t faith = found_universal(w, s, x.home);
        believe(w, s, 200);
        std::size_t held = 0;
        for (CellId nb : w.grid.neighbors(x.home)) held += s.faith_of[nb] == faith ? 1 : 0;
        return held;
    };
    const std::size_t ruled = converted(true);
    CHECK(ruled >= 4);
    CHECK(ruled > converted(false));
}

TEST(religion_a_faith_ruled_far_from_where_it_began_splits) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    believe(w, s);
    // A universal faith that began far away holds a whole realm.
    CellId far = kInvalidCell;
    for (CellId c = 0; c < w.cell_count() && far == kInvalidCell; ++c) {
        if (w.grid.distance_km(c, x.home) > 2.0 * ReligionSystem::kSchismKm && s.cell_population[c] > 0) far = c;
    }
    CHECK(far != kInvalidCell);
    const std::uint32_t faith = found_universal(w, s, far);
    const std::uint32_t p = rule(w, s, x.home);
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (s.polity_of[c] == p) s.faith_of[c] = faith;
    }
    for (int year = 0; year < 3000 && s.faith_of[x.home] == faith; ++year) believe(w, s);
    const std::uint32_t sect = s.faith_of[x.home];
    CHECK(sect != faith);
    CHECK_EQ(s.religions[sect].parent, faith);
    CHECK_EQ(religion_family(s, sect), faith);
    CHECK_EQ(s.polities[p].religion, sect);
    bool logged = false;
    for (const Event& e : s.events) logged |= e.type == EventType::Schism && e.value == sect;
    CHECK(logged);
}
