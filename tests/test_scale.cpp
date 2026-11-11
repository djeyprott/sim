// Grid scale: rules tuned at level 5 give the same history at other levels.
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/polity.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

World world_at(int level) {
    WorldParams p;
    p.grid_level = level;
    return generate_test_world(p);
}

}  // namespace

TEST(scale_factors_are_exactly_one_at_level_five) {
    const World w = world_at(5);
    CHECK(w.cell_scale() == 1.0);
    CHECK(w.per_step(0.03) == 0.03);
    CHECK(w.per_area(0.003) == 0.003);
    CHECK(w.per_step_diffusion(0.005) == 0.005);
    CHECK_EQ(w.cells_for(6), std::size_t{6});
    CHECK_EQ(w.reach_steps(), 1);
    CHECK_EQ(w.hinterland_steps(), 0);
    // Each hinterland is just the cell.
    for (CellId c = 0; c < w.cell_count(); c += 97) {
        std::size_t count = 0;
        w.for_hinterland(c, [&](CellId h) {
            CHECK_EQ(h, c);
            ++count;
        });
        CHECK_EQ(count, std::size_t{1});
    }
}

TEST(scale_finer_grids_scale_by_powers_of_two) {
    const World w6 = world_at(6);
    CHECK(w6.cell_scale() == 0.5);
    CHECK(w6.per_step(0.03) == 0.06);
    CHECK(w6.per_area(0.004) == 0.001);
    CHECK_EQ(w6.cells_for(6), std::size_t{24});
    CHECK_EQ(w6.reach_steps(), 2);
    CHECK_EQ(w6.hinterland_steps(), 1);
    const World w4 = world_at(4);
    CHECK(w4.cell_scale() == 2.0);
    CHECK(w4.per_step(0.03) == 0.015);
    CHECK_EQ(w4.reach_steps(), 1);
}

TEST(scale_cells_within_counts_rings) {
    const World w = world_at(6);
    std::vector<std::uint8_t> mark(w.cell_count(), 0);
    std::vector<std::pair<CellId, int>> ball;
    // Away from the twelve pentagons a ball of radius r has 1 + 3r(r + 1) cells.
    CellId hex = 0;
    while (w.grid.neighbors(hex).size() != 6) ++hex;
    w.grid.cells_within(hex, 0, ball, mark);
    CHECK_EQ(ball.size(), std::size_t{1});
    w.grid.cells_within(hex, 1, ball, mark);
    CHECK_EQ(ball.size(), std::size_t{7});
    CHECK_EQ(ball.front().first, hex);
    CHECK_EQ(ball.front().second, 0);
    for (std::size_t i = 1; i < ball.size(); ++i) CHECK_EQ(ball[i].second, 1);
    for (std::uint8_t m : mark) CHECK_EQ(m, 0);  // scratch left clean
    // Level-6 hinterlands are a cell and its neighbours: about one level-5
    // cell's area.
    double area = 0.0;
    w.for_hinterland(hex, [&](CellId h) { area += w.grid.area_km2(h); });
    CHECK(area > 60000.0 && area < 120000.0);
}

TEST(scale_hinterland_people_sum_the_cells_around) {
    WorldParams p;
    p.grid_level = 6;
    Simulation sim = Simulation::create(p);
    const World& w = sim.world();
    const SimState& s = sim.state();
    std::vector<double> people, town;
    hinterland_people(w, s, people, town);
    // Seven cells counted as a level-5 cell's area (four level-6 cells).
    CHECK(w.hinterland_weight() == 4.0 / 7.0);
    for (CellId c = 0; c < w.cell_count(); c += 211) {
        double sum = 0.0;
        for (CellId nb : w.grid.neighbors(c)) sum += static_cast<double>(s.cell_population[nb]);
        sum += static_cast<double>(s.cell_population[c]);
        CHECK(people[c] == sum * w.hinterland_weight());
        CHECK(town[c] == 0.0);  // no towns yet
    }
}
