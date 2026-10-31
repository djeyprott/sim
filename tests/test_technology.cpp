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
}
