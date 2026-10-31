#include <algorithm>
#include <cmath>
#include <set>

#include "histsim/view/color.hpp"
#include "histsim/view/map_modes.hpp"
#include "histsim/view/png.hpp"
#include "histsim/view/raster.hpp"
#include "testing.hpp"

using namespace histsim;
using namespace histsim::view;

TEST(view_color_scale_interpolates_and_clamps) {
    const ColorScale s({{0.0, Rgba{0, 0, 0, 255}}, {10.0, Rgba{200, 100, 50, 255}}});
    CHECK(s(-5.0) == (Rgba{0, 0, 0, 255}));
    CHECK(s(50.0) == (Rgba{200, 100, 50, 255}));
    CHECK(s(5.0) == (Rgba{100, 50, 25, 255}));
    CHECK(s(std::nan("")) == (Rgba{0, 0, 0, 255}));

    const ColorScale log({{1.0, Rgba{0, 0, 0, 255}}, {100.0, Rgba{200, 200, 200, 255}}}, true);
    CHECK(log(10.0) == (Rgba{100, 100, 100, 255}));  // halfway in log space
}

TEST(view_hex_parses) {
    CHECK(hex("#1a2B3c") == (Rgba{0x1a, 0x2b, 0x3c, 255}));
}

TEST(view_raster_matches_brute_force_nearest_cell) {
    const Grid g = make_icosahedral_grid(4);
    const MapRaster r = MapRaster::build(g, 360, 180);
    for (int y = 0; y < r.height(); y += 13) {
        for (int x = 0; x < r.width(); x += 17) {
            const double lat = (90.0 - (y + 0.5)) * kPi / 180.0;
            const double lon = (-180.0 + (x + 0.5)) * kPi / 180.0;
            const Vec3 p{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
            double best_dot = -2.0;
            for (CellId c = 0; c < g.cell_count(); ++c) {
                const Vec3& v = g.center(c);
                best_dot = std::max(best_dot, v.x * p.x + v.y * p.y + v.z * p.z);
            }
            const Vec3& got = g.center(r.cell_at(x, y));
            const double got_dot = got.x * p.x + got.y * p.y + got.z * p.z;
            CHECK(got_dot >= best_dot - 1e-12);  // nearest, or tied with it
        }
    }
    CHECK_EQ(r.cell_at(-1, 0), kInvalidCell);
    CHECK_EQ(r.cell_at(0, 180), kInvalidCell);
}

TEST(view_raster_covers_every_cell) {
    const Grid g = make_icosahedral_grid(3);
    const MapRaster r = MapRaster::build(g, 720, 360);
    const std::set<CellId> seen(r.cells().begin(), r.cells().end());
    CHECK_EQ(seen.size(), g.cell_count());
}

TEST(view_every_mode_colors_every_cell_and_has_a_legend) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    sim.step_years(20);
    std::vector<Rgba> colors;
    for (const auto& mode : map_modes()) {
        color_cells(sim, mode.id, colors);
        CHECK_EQ(colors.size(), sim.world().cell_count());
        const std::set<std::uint32_t> distinct = [&] {
            std::set<std::uint32_t> s;
            for (const Rgba& c : colors) s.insert((c.r << 16) | (c.g << 8) | c.b);
            return s;
        }();
        // A mode that paints one flat color is broken. Farming has barely
        // begun after 20 years, so those modes show only a few classes yet.
        const bool early = mode.id == "farming" || mode.id == "domesticates" || mode.id == "technology" ||
                           mode.id == "disease" || mode.id == "polity" || mode.id == "towns";
        CHECK(distinct.size() > (early ? 2u : 5u));
        const Legend l = legend(sim, mode.id);
        CHECK(!l.title.empty());
        CHECK(l.ramp.size() >= 2 || l.swatches.size() >= 3);  // continuous or categorical
    }
    bool threw = false;
    try {
        color_cells(sim, "no-such-mode", colors);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(view_describe_cell_lists_land_details) {
    Simulation sim = Simulation::create({});
    CellId land = kInvalidCell;
    for (CellId c = 0; c < sim.world().cell_count(); ++c) {
        if (sim.world().is_land[c] && sim.state().cell_population[c] > 0) {
            land = c;
            break;
        }
    }
    CHECK(land != kInvalidCell);
    const auto fields = describe_cell(sim, land);
    const bool has_population = std::any_of(fields.begin(), fields.end(), [](const CellField& f) {
        return f.label == "Population";
    });
    CHECK(has_population);
    CHECK(describe_cell(sim, kInvalidCell).empty());
}

TEST(view_png_has_valid_structure) {
    std::vector<std::uint8_t> rgba(8 * 4 * 4, 0);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = static_cast<std::uint8_t>(i);
        rgba[i + 3] = 255;
    }
    const auto png = encode_png(8, 4, rgba);
    const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    CHECK(std::equal(sig, sig + 8, png.begin()));
    // The file ends with the IEND chunk, whose CRC is a fixed constant.
    const std::uint8_t iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
    CHECK(std::equal(iend, iend + 12, png.end() - 12));
}
