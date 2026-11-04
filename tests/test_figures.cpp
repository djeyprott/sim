#include <cstdio>
#include <string>

#include "histsim/figures.hpp"
#include "histsim/simulation.hpp"
#include "histsim/view/figures.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

// Level 4 runs ahead of level 5: states with writing by year 3,500.
const Simulation& run() {
    static const Simulation sim = [] {
        WorldParams p;
        p.grid_level = 4;
        Simulation s = Simulation::create(p);
        s.step_years(4500);
        return s;
    }();
    return sim;
}

}  // namespace

TEST(figures_every_reign_has_a_person) {
    const SimState& s = run().state();
    CHECK(s.rulers.size() > 10);
    for (std::size_t i = 1; i < s.rulers.size(); ++i) {
        const Ruler& r = s.rulers[i];
        CHECK(r.figure != 0 && r.figure < s.figures.size());
        const Figure& f = s.figures[r.figure];
        CHECK_EQ(f.ruler, r.id);
        CHECK(f.roles & kRoleRuler);
        CHECK_EQ(f.born, r.born);
        CHECK(f.name == r.name);
        CHECK_EQ(f.ability, r.ability);
        // A ruler who died on the throne died then; one still reigning is alive.
        if (r.end == ReignEnd::Died) CHECK_EQ(f.died, r.reign_end);
        if (r.end == ReignEnd::Reigning) CHECK(figure_alive(f, s.tick));
        if (r.end == ReignEnd::RealmFell) CHECK(f.died > r.reign_end);
    }
}

TEST(figures_families_hang_together) {
    const SimState& s = run().state();
    std::size_t with_parents = 0, married = 0;
    for (const Figure& f : s.figures) {
        if (f.id == 0) continue;
        CHECK_EQ(f.id, static_cast<std::uint32_t>(&f - s.figures.data()));
        CHECK(f.died == kNever || f.died > f.born);
        CHECK(f.born <= s.tick);
        CHECK(!f.name.empty());
        for (std::uint32_t parent : {f.father, f.mother}) {
            if (parent == 0) continue;
            CHECK(parent < s.figures.size());
            const Figure& p = s.figures[parent];
            CHECK(p.born + 12 * kTicksPerYear <= f.born);  // parents are grown when children are born
            ++with_parents;
        }
        if (f.father != 0) CHECK(!s.figures[f.father].female);
        if (f.mother != 0) CHECK(s.figures[f.mother].female);
        if (f.spouse != 0) {
            CHECK(f.spouse < s.figures.size());
            CHECK_EQ(s.figures[f.spouse].spouse, f.id);
            CHECK(s.figures[f.spouse].female != f.female);
            ++married;
        }
    }
    CHECK(with_parents > 10);
    CHECK(married > 10);
}

TEST(figures_claimants_inventors_and_prophets) {
    const Simulation& sim = run();
    const SimState& s = sim.state();
    std::size_t claims = 0;
    for (const Event& e : s.events) {
        if (e.type != EventType::SuccessionDisputed) continue;
        CHECK(e.value > 0 && static_cast<std::size_t>(e.value) < s.figures.size());
        CHECK(s.figures[static_cast<std::size_t>(e.value)].roles & kRoleClaimant);
        ++claims;
    }
    CHECK(claims > 0);
    CHECK(!s.inventions.empty());
    for (const Invention& inv : s.inventions) {
        CHECK(inv.figure != 0 && inv.figure < s.figures.size());
        CHECK(s.figures[inv.figure].roles & kRoleInventor);
        CHECK(figure_alive(s.figures[inv.figure], inv.tick));
    }
    for (const Religion& r : s.religions) {
        if (r.founder == 0) continue;
        CHECK(s.figures[r.founder].roles & kRoleProphet);
        CHECK(r.name.find(s.figures[r.founder].name) != std::string::npos);
    }
    // Pages: every notable figure has one, with a title and a life.
    const auto notable = view::notable_figures(sim, 10);
    CHECK(!notable.empty());
    for (std::uint32_t id : notable) {
        const auto page = view::figure_page(sim, id);
        CHECK_EQ(page.id, id);
        CHECK(!page.title.empty());
        CHECK(!page.life.empty());
    }
}

// Rulers' traits scale their realms' chances: the reigning person is found,
// and the factor runs from 0.5 to 1.5 (1 without a ruler).
TEST(figures_rulers_traits_scale_chances) {
    const SimState& s = run().state();
    std::size_t found = 0;
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        const Polity& p = s.polities[id];
        const Figure* f = reigning_person(s, id);
        if (p.ended != kNever || p.ruler == 0) continue;
        CHECK(f != nullptr);
        if (f == nullptr) continue;
        CHECK_EQ(f->ruler, p.ruler);
        const double k = trait_factor(f, &Figure::ambition);
        CHECK(k >= 0.5 && k <= 1.5);
        ++found;
    }
    CHECK(found > 3);
    CHECK_EQ(trait_factor(nullptr, &Figure::piety), 1.0);
}

TEST(figures_are_saved_and_restored) {
    const Simulation& sim = run();
    const std::string path = "figures_roundtrip.hsim";
    sim.save(path);
    const Simulation back = Simulation::load(path);
    std::remove(path.c_str());
    CHECK_EQ(back.state_hash(), sim.state_hash());
    CHECK_EQ(back.state().figures.size(), sim.state().figures.size());
}
