// Early technology (M2): content, requirements, effects, invention order.
#include <algorithm>

#include "histsim/simulation.hpp"
#include "histsim/systems/technology.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

std::size_t tech(const Content& c, const std::string& id) {
    for (std::size_t i = 0; i < c.techniques.size(); ++i) {
        if (c.techniques[i].id == id) return i;
    }
    throw std::runtime_error("no technique " + id);
}

std::size_t species(const Content& c, const std::string& id) {
    for (std::size_t i = 0; i < c.species.size(); ++i) {
        if (c.species[i].id == id) return i;
    }
    throw std::runtime_error("no species " + id);
}

std::uint64_t bit(std::size_t i) { return std::uint64_t{1} << i; }

}  // namespace

TEST(technique_pack_loads_with_prerequisites_in_order) {
    const Content& c = default_content();
    CHECK(c.techniques.size() >= 12);
    for (std::size_t i = 0; i < c.techniques.size(); ++i) {
        CHECK(c.techniques[i].prerequisites < bit(i));  // only earlier techniques
    }
    const TechniqueDef& irrigation = c.techniques[tech(c, "irrigation")];
    CHECK(irrigation.prerequisites & bit(tech(c, "sickles")));
    CHECK(irrigation.requires_crops);
    CHECK(irrigation.where == TechPlace::River);
}

TEST(technique_requirements_and_places) {
    WorldParams p;
    p.grid_level = 4;
    const World w = generate_test_world(p);
    const Content& c = *w.content;
    CellId river = kInvalidCell, dry_inland = kInvalidCell;
    for (CellId cell = 0; cell < w.cell_count(); ++cell) {
        if (!w.is_land[cell]) continue;
        if (is_river(w, cell) && river == kInvalidCell) river = cell;
        if (!is_river(w, cell) && w.coast_distance[cell] > 2 && dry_inland == kInvalidCell) dry_inland = cell;
    }
    CHECK(river != kInvalidCell && dry_inland != kInvalidCell);
    const TechniqueDef& irrigation = c.techniques[tech(c, "irrigation")];
    const std::uint32_t wheat = 1u << species(c, "wild_wheat_barley");
    const std::uint64_t sickles = bit(tech(c, "sickles"));
    CHECK(technique_usable(w, irrigation, river, sickles, wheat));
    CHECK(!technique_usable(w, irrigation, river, 0, wheat));        // needs sickles
    CHECK(!technique_usable(w, irrigation, river, sickles, 0));      // needs crops
    CHECK(!technique_usable(w, irrigation, dry_inland, sickles, wheat));  // needs a river
    // Dogs alone are not herds.
    const TechniqueDef& milk = c.techniques[tech(c, "secondary_products")];
    CHECK(!technique_usable(w, milk, river, 0, 1u << species(c, "wolves")));
    CHECK(technique_usable(w, milk, river, 0, 1u << species(c, "wild_sheep_goats")));
    // Effects add up, each only where it applies.
    const std::uint64_t both = sickles | bit(tech(c, "irrigation"));
    CHECK_NEAR(technique_effects(w, river, both).crop_yield, 0.1 + 1.5, 1e-12);
    CHECK_NEAR(technique_effects(w, dry_inland, both).crop_yield, 0.1, 1e-12);
    CHECK_EQ(technique_effects(w, river, 0).crop_yield, 0.0);
}

TEST(technology_follows_farming_and_its_prerequisites) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    // Techniques some peoples know from the start (canoes, bows) count as
    // known from year 0.
    std::uint64_t at_start = 0;
    for (std::uint64_t k : sim.state().techniques) at_start |= k;
    sim.step_years(3000);
    const SimState& s = sim.state();
    const Content& c = *sim.world().content;
    CHECK(!s.inventions.empty());
    Tick first_crop = -1;
    for (const Domestication& d : s.domestications) {
        if (c.species[d.species].kind == SpeciesKind::Plant && (first_crop < 0 || d.tick < first_crop)) first_crop = d.tick;
    }
    CHECK(first_crop >= 0);
    // Farming techniques only after crops; each invention after its prerequisites.
    std::vector<Tick> first(c.techniques.size(), -1);
    for (std::size_t k = 0; k < c.techniques.size(); ++k) {
        if (at_start & bit(k)) first[k] = 0;
    }
    for (const Invention& v : s.inventions) {
        if (first[v.technique] < 0) first[v.technique] = v.tick;
    }
    for (const Invention& v : s.inventions) {
        const TechniqueDef& t = c.techniques[v.technique];
        if (t.requires_crops) CHECK(v.tick >= first_crop);
        for (std::size_t k = 0; k < c.techniques.size(); ++k) {
            if (t.prerequisites & bit(k)) CHECK(first[k] >= 0 && first[k] <= v.tick);
        }
    }
    // Farming techniques are in use, and every cell's knowledge is usable or
    // fading.
    std::size_t with_sickles = 0;
    for (CellId cell = 0; cell < sim.world().cell_count(); ++cell) {
        with_sickles += (s.techniques[cell] >> tech(c, "sickles")) & 1u;
        if (s.cell_population[cell] == 0) CHECK_EQ(s.techniques[cell], std::uint64_t{0});
    }
    CHECK(with_sickles > 10);
    CHECK(s.history.back().techniques >= 8);
    // Know-how is kept only toward techniques the people do not know, below 1.
    const std::size_t nk = c.techniques.size();
    CHECK_EQ(s.know_how.size(), sim.world().cell_count() * nk);
    bool building = false;
    for (CellId cell = 0; cell < sim.world().cell_count(); ++cell) {
        for (std::size_t k = 0; k < nk; ++k) {
            const float x = s.know_how[cell * nk + k];
            CHECK(x >= 0.0f && x < 1.0f);
            if ((s.techniques[cell] >> k) & 1u) CHECK_EQ(x, 0.0f);
            building |= x > 0.1f;
        }
    }
    CHECK(building);
}

// What people do: surplus handled is accounted for most at a realm's seat,
// rule only within a realm.
TEST(technology_practices_follow_what_people_do) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    const World& w = sim.world();
    const SimState& s = sim.state();
    CellId cell = 0;
    while (s.cell_population[cell] == 0) ++cell;
    auto doing = [&](bool ruled, std::uint8_t seat) {
        return practices(w, s, cell, PracticeInputs{50000.0, 5000.0, 1000.0, 0.0, 20.0, ruled, seat});
    };
    const auto R = static_cast<std::size_t>(TechPractice::Redistribution);
    const auto Rule = static_cast<std::size_t>(TechPractice::Rule);
    const auto Towns = static_cast<std::size_t>(TechPractice::Towns);
    const auto Trade = static_cast<std::size_t>(TechPractice::Trade);
    const auto unruled = doing(false, 0), ruled = doing(true, 0), at_seat = doing(true, 1), temple = doing(true, 2);
    CHECK(unruled[R] > 0.0);
    CHECK_NEAR(ruled[R] / unruled[R], TechnologySystem::kRuled / TechnologySystem::kUnruled, 1e-9);
    CHECK_NEAR(at_seat[R] / ruled[R], 1.0 / TechnologySystem::kRuled, 1e-9);
    CHECK(temple[R] > at_seat[R]);
    CHECK_EQ(unruled[Rule], 0.0);
    CHECK_NEAR(at_seat[Rule], 20.0 / TechnologySystem::kRealmCells, 1e-12);
    CHECK_NEAR(ruled[Rule], at_seat[Rule] * TechnologySystem::kRuled, 1e-12);
    CHECK_NEAR(unruled[Towns], 0.5, 1e-12);
    CHECK_NEAR(unruled[Trade], 2.0, 1e-12);
    for (double x : at_seat) CHECK(x >= 0.0 && x <= TechnologySystem::kMaxPractice);
}

TEST(technology_riding_needs_horses_or_camels) {
    WorldParams p;
    p.grid_level = 4;
    const World w = generate_test_world(p);
    const Content& c = *w.content;
    const TechniqueDef& riding = c.techniques[tech(c, "horse_riding")];
    auto species = [&](const char* id) {
        for (std::size_t s = 0; s < c.species.size(); ++s) {
            if (c.species[s].id == id) return std::uint32_t{1} << s;
        }
        return std::uint32_t{0};
    };
    CellId land = 0;
    while (!w.is_land[land]) ++land;
    CHECK(!technique_usable(w, riding, land, 0, 0));
    CHECK(technique_usable(w, riding, land, 0, species("wild_horses")));
    CHECK(technique_usable(w, riding, land, 0, species("wild_camels")));
    CHECK(!technique_usable(w, riding, land, 0, species("aurochs")));
}
