// Disease (M2): content, outbreak arithmetic, endemic burden, epidemics.
#include <cmath>

#include "histsim/portable_math.hpp"
#include "histsim/simulation.hpp"
#include "histsim/systems/disease.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

std::size_t disease_index(const Content& c, const std::string& id) {
    for (std::size_t i = 0; i < c.diseases.size(); ++i) {
        if (c.diseases[i].id == id) return i;
    }
    throw std::runtime_error("no disease " + id);
}

}  // namespace

TEST(pmath_exp_matches_known_values) {
    CHECK_NEAR(pmath::exp(0.0), 1.0, 0.0);
    CHECK_NEAR(pmath::exp(1.0), 2.718281828459045, 1e-15);
    CHECK_NEAR(pmath::exp(-2.5), 0.0820849986238988, 1e-17);
    CHECK_NEAR(pmath::exp(10.0) / 22026.465794806718, 1.0, 1e-15);
    CHECK_EQ(pmath::exp(-800.0), 0.0);
}

TEST(disease_outbreak_size_follows_sir_final_size) {
    CHECK_EQ(outbreak_size(0.9, 1.0), 0.0);    // below threshold: no epidemic
    CHECK_EQ(outbreak_size(2.0, 0.4), 0.0);    // herd immunity: r s < 1
    CHECK_NEAR(outbreak_size(2.0, 1.0), 0.7968121300200202, 1e-9);  // classic R0 = 2
    CHECK(outbreak_size(13.0, 1.0) > 0.999);    // measles in a virgin population
    CHECK(outbreak_size(3.0, 0.6) < outbreak_size(3.0, 1.0));
    const double a = outbreak_size(3.0, 0.8);  // the root satisfies the equation
    CHECK_NEAR(a, 0.8 * (1.0 - std::exp(-3.0 * a)), 1e-12);
}

TEST(disease_pack_loads) {
    const Content& c = default_content();
    CHECK(c.diseases.size() >= 6);
    const DiseaseDef& pox = c.diseases[disease_index(c, "pox")];
    CHECK(pox.kind == DiseaseKind::Epidemic);
    CHECK(pox.hosts != 0);
    CHECK(pox.immunity_years == -1.0);
    const DiseaseDef& malaria = c.diseases[disease_index(c, "malaria")];
    CHECK(malaria.kind == DiseaseKind::Endemic);
    CHECK(malaria.present_at_start);
    CHECK(malaria.has_niche);
}

TEST(disease_burden_falls_on_farmers_not_foragers) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(20);
    // Foragers' own diseases are in their life table: almost no extra deaths.
    const auto& h = sim.state().history;
    const double early = static_cast<double>(h.back().disease_deaths_total) / static_cast<double>(h.back().deaths_total);
    CHECK(early < 0.02);
    sim.step_years(2480);
    const SimState& s = sim.state();
    double farm_deaths = 0, farm_pop = 0, forage_deaths = 0, forage_pop = 0;
    for (CellId c = 0; c < sim.world().cell_count(); ++c) {
        if (s.cell_population[c] == 0) continue;
        if (s.env.farming[c] > 0.5) {
            farm_deaths += s.disease_deaths[c];
            farm_pop += s.cell_population[c];
        } else if (s.env.farming[c] < 0.05) {
            forage_deaths += s.disease_deaths[c];
            forage_pop += s.cell_population[c];
        }
    }
    CHECK(farm_pop > 0 && forage_pop > 0);
    CHECK(farm_deaths / farm_pop > 3.0 * forage_deaths / forage_pop);
}

TEST(disease_virgin_soil_epidemic_strikes_then_burns_out) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(2500);
    const Content& c = *sim.world().content;
    const std::size_t pox = disease_index(c, "pox");
    const std::size_t nd = c.diseases.size();
    CellId target = 0;
    for (CellId cell = 0; cell < sim.world().cell_count(); ++cell) {
        if (sim.state().cell_population[cell] > sim.state().cell_population[target]) target = cell;
    }
    Command cmd;
    cmd.type = CommandType::Outbreak;
    cmd.cell = target;
    cmd.amount = static_cast<double>(pox);
    CHECK(sim.apply(cmd).ok);
    const std::int64_t before = sim.state().disease_deaths_total;
    const std::int64_t people = sim.state().total_population();
    sim.step_years(1);
    // Nobody was immune: a large share of the cell catches it.
    CHECK(sim.state().susceptible[target * nd + pox] < 0.3);
    CHECK(sim.state().disease_deaths[target] > sim.state().cell_population[target] / 50);
    sim.step_years(99);
    std::size_t still = 0;
    for (CellId cell = 0; cell < sim.world().cell_count(); ++cell) still += sim.state().disease_present[cell * nd + pox];
    CHECK_EQ(still, std::size_t{0});  // no population is big enough to keep it
    const std::int64_t killed = sim.state().disease_deaths_total - before;
    CHECK(killed > people / 200);
    // Bad input is refused.
    cmd.amount = 99;
    CHECK(!sim.apply(cmd).ok);
}
