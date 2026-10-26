#include "histsim/world.hpp"
#include "testing.hpp"

using namespace histsim;

TEST(world_land_share_matches_ocean_fraction) {
    for (double ocean : {0.5, 0.7, 0.9}) {
        WorldParams p;
        p.ocean_fraction = ocean;
        const World w = generate_test_world(p);
        std::size_t land = 0;
        for (std::uint8_t l : w.is_land) land += l;
        CHECK_NEAR(static_cast<double>(land) / static_cast<double>(w.cell_count()), 1.0 - ocean, 0.002);
    }
}

TEST(world_fields_are_consistent) {
    const World w = generate_test_world({});
    const std::size_t n = w.cell_count();
    CHECK_EQ(w.elevation_m.size(), n);
    CHECK_EQ(w.temperature_c.size(), n);
    CHECK_EQ(w.precipitation_mm.size(), n);
    CHECK_EQ(w.forager_capacity.size(), n);
    for (CellId c = 0; c < n; ++c) {
        CHECK_EQ(w.is_land[c] == 1, w.elevation_m[c] > 0.0);
        CHECK(w.precipitation_mm[c] >= 0.0);
        CHECK(w.forager_capacity[c] >= 0.0);
        if (!w.is_land[c]) {
            CHECK_EQ(w.coast_distance[c], 0u);
            CHECK_EQ(w.forager_capacity[c], 0.0);
        } else {
            CHECK(w.coast_distance[c] >= 1u);
        }
    }
}

TEST(world_poles_are_colder_than_equator) {
    const World w = generate_test_world({});
    double polar = 0.0, equatorial = 0.0;
    int np = 0, ne = 0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        const double z = w.grid.center(c).z;
        if (z > 0.95 || z < -0.95) { polar += w.temperature_c[c]; ++np; }
        if (z > -0.1 && z < 0.1) { equatorial += w.temperature_c[c]; ++ne; }
    }
    CHECK(np > 0 && ne > 0);
    CHECK(polar / np + 30.0 < equatorial / ne);
}

TEST(world_is_a_pure_function_of_params) {
    WorldParams a;
    a.seed = 11;
    WorldParams b = a;
    b.seed = 12;
    CHECK_EQ(generate_test_world(a).hash(), generate_test_world(a).hash());
    CHECK(generate_test_world(a).hash() != generate_test_world(b).hash());
}
