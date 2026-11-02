#include "histsim/world.hpp"

#include <cmath>
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

namespace {

// The cell nearest a latitude/longitude (tests only: uses libm).
CellId cell_at(const World& w, double lat_deg, double lon_deg) {
    const double la = lat_deg * 3.14159265358979323846 / 180.0, lo = lon_deg * 3.14159265358979323846 / 180.0;
    const Vec3 p{std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)};
    CellId best = 0;
    double best_dot = -2.0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        const Vec3& q = w.grid.center(c);
        const double d = p.x * q.x + p.y * q.y + p.z * q.z;
        if (d > best_dot) best_dot = d, best = c;
    }
    return best;
}

}  // namespace

TEST(world_earth_has_earths_land_and_sea_at_every_level) {
    for (int level : {4, 5, 7, 8}) {
        WorldParams p;
        p.kind = WorldKind::Earth;
        p.grid_level = level;
        const World w = generate_test_world(p);
        std::size_t land = 0;
        for (std::uint8_t l : w.is_land) land += l;
        const double share = static_cast<double>(land) / static_cast<double>(w.cell_count());
        CHECK(share > 0.26 && share < 0.32);  // Earth: 29%
        CHECK(w.is_land[cell_at(w, 0.0, 22.0)]);      // the Congo basin
        CHECK(w.is_land[cell_at(w, 40.0, -100.0)]);   // the Great Plains
        CHECK(w.is_land[cell_at(w, -25.0, 134.0)]);   // central Australia
        CHECK(!w.is_land[cell_at(w, 0.0, -30.0)]);    // the Atlantic
        CHECK(!w.is_land[cell_at(w, -10.0, -140.0)]); // the Pacific
        CHECK(w.elevation_m[cell_at(w, 32.0, 88.0)] > 3500.0);  // Tibet
        CHECK(w.elevation_m[cell_at(w, -80.0, 60.0)] > 1500.0); // the Antarctic ice sheet
    }
}

TEST(world_earth_terrain_is_the_same_for_every_seed) {
    WorldParams a;
    a.kind = WorldKind::Earth;
    a.seed = 3;
    WorldParams b = a;
    b.seed = 4;
    const World wa = generate_test_world(a), wb = generate_test_world(b);
    CHECK(wa.elevation_m == wb.elevation_m);
    CHECK(wa.hash() != wb.hash());  // species, deposits and peoples differ
    WorldParams g = a;
    g.kind = WorldKind::Generated;
    CHECK(generate_test_world(g).hash() != wa.hash());
}
