// Tests for interventions ("god mode").
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "histsim/simulation.hpp"
#include "histsim/systems/climate.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

Simulation small_sim(std::uint64_t seed = 4) {
    WorldParams p;
    p.seed = seed;
    p.grid_level = 4;
    return Simulation::create(p);
}

CellId busiest_cell(const Simulation& sim) {
    CellId best = 0;
    for (CellId c = 0; c < sim.world().cell_count(); ++c) {
        if (sim.state().cell_population[c] > sim.state().cell_population[best]) best = c;
    }
    return best;
}

CellId first_ocean_cell(const Simulation& sim) {
    for (CellId c = 0; c < sim.world().cell_count(); ++c) {
        if (!sim.world().is_land[c]) return c;
    }
    return kInvalidCell;
}

std::int64_t people_in(const Simulation& sim, const std::vector<WeightedCell>& cells) {
    std::int64_t n = 0;
    for (const auto& wc : cells) n += sim.state().cell_population[wc.cell];
    return n;
}

bool conserved(const Simulation& sim) {
    const Ledger& l = sim.state().ledger;
    return sim.state().total_population() == l.initial_population + l.births - l.deaths + l.added;
}

Command cmd(CommandType type, CellId cell = kInvalidCell, double radius = 0, double amount = 0, double amount2 = 0,
            std::int64_t years = 0) {
    Command c;
    c.type = type;
    c.cell = cell;
    c.radius_km = radius;
    c.amount = amount;
    c.amount2 = amount2;
    c.years = years;
    return c;
}

}  // namespace

TEST(intervention_cells_within) {
    Simulation sim = small_sim();
    const CellId c = busiest_cell(sim);
    const auto one = cells_within(sim.world(), c, 0.0, true);
    CHECK_EQ(one.size(), std::size_t{1});
    CHECK_EQ(one[0].cell, c);
    const auto small = cells_within(sim.world(), c, 700.0, true);  // level-4 neighbors are 441-526 km apart
    const auto big = cells_within(sim.world(), c, 1500.0, true);
    CHECK(small.size() > 1 && big.size() > small.size());
    for (const auto& wc : big) {
        CHECK(wc.weight >= 0.0 && wc.weight <= 1.0);
        CHECK(sim.world().grid.distance_km(c, wc.cell) <= 1500.0);
    }
    CHECK(cells_within(sim.world(), kInvalidCell, 100.0, false).empty());
}

TEST(intervention_eruption_cools_the_planet) {
    Simulation calm = small_sim();
    Simulation hit = small_sim();
    calm.step_years(10);
    hit.step_years(10);
    const auto r = hit.apply(cmd(CommandType::Eruption, kInvalidCell, 0, 10.0));
    CHECK(r.ok);
    calm.step_years(2);
    hit.step_years(2);
    CHECK(hit.state().climate.global_anomaly_c < calm.state().climate.global_anomaly_c - 1.0);
    bool logged = false;
    for (const Event& e : hit.state().events) logged |= e.type == EventType::Eruption && e.value == -10000;
    CHECK(logged);
}

TEST(intervention_forcing_reaches_equilibrium) {
    // Compared with the same world unforced, so natural forcing (orbital,
    // volcanic, solar) cancels: the extra warming should approach F / lambda.
    Simulation sim = small_sim();
    Simulation control = small_sim();
    CHECK(sim.apply(cmd(CommandType::ClimateForcing, kInvalidCell, 0, 3.9)).ok);
    sim.step_years(100);
    control.step_years(100);
    double extra = 0.0;  // averaged to smooth year-to-year noise
    for (int i = 0; i < 50; ++i) {
        sim.step_years(1);
        control.step_years(1);
        extra += (sim.state().climate.global_anomaly_c - control.state().climate.global_anomaly_c) / 50.0;
    }
    CHECK_NEAR(extra, 3.9 / 1.3, 0.3);
    // Setting it again replaces the old value.
    CHECK(sim.apply(cmd(CommandType::ClimateForcing, kInvalidCell, 0, 0.0)).ok);
    CHECK_EQ(sim.state().climate.imposed_forcing, 0.0);
}

TEST(intervention_plague_kills_about_the_share) {
    Simulation sim = small_sim();
    sim.step_years(20);
    const CellId c = busiest_cell(sim);
    const auto area = cells_within(sim.world(), c, 800.0, false);
    const auto before = people_in(sim, area);
    const auto r = sim.apply(cmd(CommandType::Plague, c, 800.0, 0.4));
    CHECK(r.ok);
    const auto after = people_in(sim, area);
    CHECK_EQ(before - after, r.people);
    CHECK_NEAR(static_cast<double>(r.people) / static_cast<double>(before), 0.4, 0.02);
    CHECK(conserved(sim));
}

TEST(intervention_wipe_out_empties_the_area) {
    Simulation sim = small_sim();
    const CellId c = busiest_cell(sim);
    const auto area = cells_within(sim.world(), c, 600.0, false);
    const auto r = sim.apply(cmd(CommandType::WipeOut, c, 600.0));
    CHECK(r.ok);
    CHECK_EQ(people_in(sim, area), std::int64_t{0});
    CHECK(conserved(sim));
    sim.step_years(30);  // neighbors move back in
    CHECK(people_in(sim, area) > 0);
}

TEST(intervention_settlers_found_a_new_group) {
    Simulation sim = small_sim();
    const CellId c = busiest_cell(sim);
    const auto before = sim.state().total_population();
    const auto languages = sim.state().languages.size();
    const auto cultures = sim.state().cultures.size();
    const auto r = sim.apply(cmd(CommandType::Settlers, c, 0, 2500));
    CHECK(r.ok);
    CHECK_EQ(sim.state().total_population(), before + 2500);
    CHECK_EQ(sim.state().ledger.added, std::int64_t{2500});
    // A new people: its own language family and culture.
    CHECK_EQ(sim.state().languages.size(), languages + 1);
    CHECK_EQ(sim.state().cultures.size(), cultures + 1);
    const auto lang = static_cast<std::uint32_t>(languages);
    CHECK_EQ(sim.state().languages[lang].family, lang);
    bool found = false;
    for (const Cohort& co : sim.state().cohorts_in(c)) {
        found |= co.language == lang && co.culture == cultures && co.total() == 2500;
    }
    CHECK(found);
    CHECK(conserved(sim));
    sim.step_years(10);
    CHECK(conserved(sim));
}

TEST(intervention_set_game_changes_capacity_now) {
    Simulation sim = small_sim();
    const CellId c = busiest_cell(sim);
    const double before = sim.state().env.capacity[c];
    const double game_before = sim.state().env.game[c];
    CHECK(sim.apply(cmd(CommandType::SetGame, c, 0, 0.05)).ok);
    CHECK_NEAR(sim.state().env.game[c], 0.05, 1e-12);
    // Capacity is recomputed at once; only the game share drops (fish and
    // plants stay), so the loss is the game share times the land foods.
    CHECK_NEAR(sim.state().env.capacity[c], environment_capacity(sim.world(), sim.state(), c), 1e-9);
    CHECK(game_before > 0.3);
    CHECK(sim.state().env.capacity[c] < before - 1e-6);
}

TEST(intervention_regional_drought_lasts_its_years) {
    Simulation sim = small_sim();
    const CellId c = busiest_cell(sim);
    const double baseline = sim.world().precipitation_mm[c];
    CHECK(sim.apply(cmd(CommandType::RegionalClimate, c, 1000.0, 0.0, -0.6, 5)).ok);
    CHECK(sim.state().env.precipitation_mm[c] < 0.6 * baseline);  // at once
    for (int y = 0; y < 4; ++y) {  // the remaining four years
        sim.step_years(1);
        CHECK(sim.state().env.precipitation_mm[c] < 0.75 * baseline);
    }
    CHECK(sim.state().climate.modifiers.empty());
    sim.step_years(1);  // over: back to normal year-to-year variation
    CHECK(sim.state().env.precipitation_mm[c] > 0.6 * baseline);
}

TEST(intervention_bad_input_is_rejected_and_not_logged) {
    Simulation sim = small_sim();
    const auto ocean = first_ocean_cell(sim);
    CHECK(!sim.apply(cmd(CommandType::Settlers, ocean, 0, 100)).ok);
    CHECK(!sim.apply(cmd(CommandType::Plague, kInvalidCell, 100, 0.5)).ok);
    CHECK(sim.state().commands.empty());
    CHECK_EQ(sim.state().ledger.added, std::int64_t{0});
}

TEST(intervention_runs_replay_exactly) {
    auto scripted = [] {
        Simulation sim = small_sim(9);
        sim.step_years(15);
        const CellId c = busiest_cell(sim);
        sim.apply(cmd(CommandType::Plague, c, 700.0, 0.3));
        sim.step_ticks(7);  // mid-year
        sim.apply(cmd(CommandType::Eruption, kInvalidCell, 0, 8.0));
        sim.apply(cmd(CommandType::Settlers, c, 0, 900));
        sim.apply(cmd(CommandType::RegionalClimate, c, 1200.0, -4.0, -0.3, 12));
        sim.step_years(30);
        return sim;
    };
    Simulation a = scripted();
    Simulation b = scripted();
    CHECK_EQ(a.state_hash(), b.state_hash());
    CHECK_EQ(a.state().commands.size(), std::size_t{4});
    CHECK(conserved(a));

    // Save with commands and active modifiers, reload, continue: same as straight.
    const std::string path = (std::filesystem::temp_directory_path() / "histsim_interventions.hsim").string();
    Simulation c = small_sim(9);
    c.step_years(15);
    const CellId cell = busiest_cell(c);
    c.apply(cmd(CommandType::Plague, cell, 700.0, 0.3));
    c.step_ticks(7);
    c.apply(cmd(CommandType::Eruption, kInvalidCell, 0, 8.0));
    c.apply(cmd(CommandType::Settlers, cell, 0, 900));
    c.apply(cmd(CommandType::RegionalClimate, cell, 1200.0, -4.0, -0.3, 12));
    c.step_years(5);
    c.save(path);
    Simulation d = Simulation::load(path);
    CHECK_EQ(d.state().commands.size(), std::size_t{4});
    CHECK(!d.state().climate.modifiers.empty());
    d.step_years(25);
    CHECK_EQ(d.state_hash(), a.state_hash());
    std::filesystem::remove(path);
}
