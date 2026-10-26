#include <algorithm>
#include <cmath>

#include "histsim/grid.hpp"
#include "testing.hpp"

using namespace histsim;

TEST(grid_cell_counts_follow_formula) {
    for (int level = 0; level <= 6; ++level) {
        const Grid g = make_icosahedral_grid(level);
        const std::size_t expected = 10 * (std::size_t{1} << (2 * level)) + 2;
        CHECK_EQ(g.cell_count(), expected);
    }
}

TEST(grid_has_twelve_pentagons_rest_hexagons) {
    const Grid g = make_icosahedral_grid(4);
    std::size_t pentagons = 0;
    for (CellId c = 0; c < g.cell_count(); ++c) {
        const std::size_t k = g.neighbors(c).size();
        CHECK(k == 5 || k == 6);
        pentagons += k == 5 ? 1 : 0;
    }
    CHECK_EQ(pentagons, std::size_t{12});
}

TEST(grid_neighbors_are_symmetric_and_distinct) {
    const Grid g = make_icosahedral_grid(4);
    for (CellId c = 0; c < g.cell_count(); ++c) {
        const auto nb = g.neighbors(c);
        for (CellId a : nb) {
            CHECK(a != c);
            const auto back = g.neighbors(a);
            CHECK(std::find(back.begin(), back.end(), c) != back.end());
        }
        std::vector<CellId> sorted(nb.begin(), nb.end());
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    }
}

TEST(grid_neighbors_wind_counter_clockwise) {
    const Grid g = make_icosahedral_grid(3);
    for (CellId c = 0; c < g.cell_count(); ++c) {
        const Vec3& o = g.center(c);
        const auto nb = g.neighbors(c);
        for (std::size_t i = 0; i < nb.size(); ++i) {
            const Vec3& a = g.center(nb[i]);
            const Vec3& b = g.center(nb[(i + 1) % nb.size()]);
            const Vec3 da{a.x - o.x, a.y - o.y, a.z - o.z};
            const Vec3 db{b.x - o.x, b.y - o.y, b.z - o.z};
            const Vec3 n{da.y * db.z - da.z * db.y, da.z * db.x - da.x * db.z, da.x * db.y - da.y * db.x};
            CHECK(n.x * o.x + n.y * o.y + n.z * o.z > 0.0);  // turns toward the outward normal
        }
    }
}

TEST(grid_area_sums_to_planet_and_is_near_uniform) {
    const Grid g = make_icosahedral_grid(5);
    double total = 0.0, lo = 1e300, hi = 0.0;
    for (CellId c = 0; c < g.cell_count(); ++c) {
        total += g.area_km2(c);
        lo = std::min(lo, g.area_km2(c));
        hi = std::max(hi, g.area_km2(c));
    }
    CHECK_NEAR(total / kPlanetAreaKm2, 1.0, 1e-9);
    // Measured spread is ~1.78 at level 5 and tends to ~1.79 at finer levels
    // (comparable to H3). Fail if a change makes it noticeably worse.
    CHECK(hi / lo < 1.85);
}

TEST(grid_centers_are_unit_vectors) {
    const Grid g = make_icosahedral_grid(4);
    for (CellId c = 0; c < g.cell_count(); ++c) {
        const Vec3& v = g.center(c);
        CHECK_NEAR(v.x * v.x + v.y * v.y + v.z * v.z, 1.0, 1e-12);
    }
}

TEST(grid_distance_matches_great_circle) {
    const Grid g = make_icosahedral_grid(5);
    for (CellId c = 0; c < g.cell_count(); c += 37) {
        for (CellId other : {g.neighbors(c)[0], static_cast<CellId>((c * 7919u) % g.cell_count())}) {
            const Vec3& a = g.center(c);
            const Vec3& b = g.center(other);
            // Reference via atan2(|a x b|, a . b), well conditioned at any angle.
            const Vec3 x{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
            const double exact = std::atan2(std::sqrt(x.x * x.x + x.y * x.y + x.z * x.z),
                                            a.x * b.x + a.y * b.y + a.z * b.z) * kPlanetRadiusKm;
            CHECK(std::fabs(g.distance_km(c, other) - exact) <= 1e-6 * exact + 1e-6);
        }
    }
}

TEST(grid_distance_extremes) {
    const Grid g = make_icosahedral_grid(2);
    CHECK_NEAR(g.distance_km(0, 0), 0.0, 1e-9);
    // Find the cell most nearly opposite cell 0; it should be ~half the circumference.
    const Vec3& a = g.center(0);
    CellId far = 0;
    double best = 2.0;
    for (CellId c = 0; c < g.cell_count(); ++c) {
        const Vec3& b = g.center(c);
        const double d = a.x * b.x + a.y * b.y + a.z * b.z;
        if (d < best) {
            best = d;
            far = c;
        }
    }
    CHECK_NEAR(g.distance_km(0, far), kPi * kPlanetRadiusKm, 1e-3);
}

TEST(grid_is_reproducible) {
    CHECK_EQ(make_icosahedral_grid(5).geometry_hash(), make_icosahedral_grid(5).geometry_hash());
    CHECK(make_icosahedral_grid(4).geometry_hash() != make_icosahedral_grid(5).geometry_hash());
}
