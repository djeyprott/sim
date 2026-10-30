// World geography (M2 slice 1): content packs, biomes, seasons, rivers,
// soils and wild species ranges.
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

#include "histsim/content.hpp"
#include "histsim/json.hpp"
#include "histsim/world.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

template <typename F>
bool throws(F&& f) {
    try {
        f();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

// The default packs with a different species file.
Content with_species(const std::string& species) {
    ContentSources src = default_sources();
    src.species = species;
    return load_content(src);
}

const World& level5() {
    static const World w = [] {
        WorldParams p;
        p.seed = 1;
        p.grid_level = 5;
        return generate_test_world(p);
    }();
    return w;
}

}  // namespace

// ---- JSON

TEST(json_parses_values_and_paths) {
    const json::Value v = json::parse(R"({ // comment
        "a": [1, -2.5, 1e3, true, null, "x\"yé"],
        "b": {"c": 0.1}
    })");
    const auto& a = v.at("a").as_array();
    CHECK_EQ(a.size(), std::size_t{6});
    CHECK_EQ(a[0].as_int(), std::int64_t{1});
    CHECK(a[1].as_number() == -2.5);
    CHECK(a[2].as_number() == 1000.0);
    CHECK(a[3].as_bool());
    CHECK(a[4].is_null());
    CHECK(a[5].as_string() == "x\"y\xc3\xa9");
    CHECK(v.at("b").at("c").as_number() == 0.1);  // exactly the nearest double
    CHECK(v.number_or("missing", 7.0) == 7.0);
    CHECK(v.at("b").at("c").path().find("b") != std::string::npos);
}

TEST(json_rejects_bad_input) {
    CHECK(throws([] { (void)json::parse("{\"a\": 1,}"); }));
    CHECK(throws([] { (void)json::parse("{\"a\": 1, \"a\": 2}"); }));  // duplicate key
    CHECK(throws([] { (void)json::parse("[1 2]"); }));
    CHECK(throws([] { (void)json::parse("\"unterminated"); }));
    CHECK(throws([] { (void)json::parse("1.2345678901234567890"); }));  // too many digits to read exactly
    CHECK(throws([] { (void)json::parse("{} extra"); }));
    CHECK(throws([] { (void)json::parse("{\"a\": 1}").at("b"); }));
    CHECK(throws([] { (void)json::parse("1.5").as_int(); }));
}

// ---- Content

TEST(content_default_pack_loads) {
    const Content& c = default_content();
    CHECK(c.species.size() >= 10);
    CHECK(c.species.size() <= kMaxSpecies);
    CHECK(c.hash != 0);
    std::set<std::string> ids;
    for (const SpeciesDef& s : c.species) ids.insert(s.id);
    CHECK_EQ(ids.size(), c.species.size());
    CHECK(ids.count("wild_wheat_barley") == 1);
}

TEST(content_validation_errors_are_reported) {
    const char* good = R"({"version": 1, "species": [{
        "id": "x", "name": "X", "kind": "plant",
        "coldest_month_c": [-5, 0, 10, 20], "warmest_month_c": [10, 20, 30, 40],
        "annual_precip_mm": [100, 300, 800, 1500], "rain_season": [-1, -1, 1, 1],
        "elevation_m": [-100, 0, 1000, 3000], "native_regions": 1, "range_km": 1500}]})";
    const Content c = with_species(good);
    CHECK_EQ(c.species.size(), std::size_t{1});
    CHECK(c.species[0].coldest_month_c(5.0) == 1.0);
    CHECK_NEAR(c.species[0].coldest_month_c(-2.5), 0.5, 1e-12);
    CHECK(c.species[0].coldest_month_c(25.0) == 0.0);

    auto with = [&](const std::string& from, const std::string& to) {
        std::string s = good;
        s.replace(s.find(from), from.size(), to);
        return s;
    };
    CHECK(throws([&] { (void)with_species(with("\"plant\"", "\"fungus\"")); }));
    CHECK(throws([&] { (void)with_species(with("[-5, 0, 10, 20]", "[0, -5, 10, 20]")); }));
    CHECK(throws([&] { (void)with_species(with("\"native_regions\": 1", "\"native_regions\": 0")); }));
    CHECK(throws([&] { (void)with_species(with("\"version\": 1", "\"version\": 2")); }));
    CHECK(throws([&] { (void)with_species(with("\"range_km\": 1500", "\"range_km\": -1")); }));
    // A different pack is a different world.
    CHECK(with_species(with("\"X\"", "\"Y\"")).hash != c.hash);
}

// ---- Climate and biomes

TEST(geo_biomes_cover_land_and_include_the_major_types) {
    const World& w = level5();
    std::set<int> seen;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        const auto b = static_cast<Biome>(w.biome[c]);
        CHECK_EQ(w.is_land[c] != 0, b != Biome::Ocean);
        CHECK(w.biome[c] < static_cast<std::uint8_t>(Biome::Count));
        seen.insert(w.biome[c]);
    }
    for (Biome b : {Biome::Tundra, Biome::TemperateForest, Biome::Desert, Biome::TropicalRainforest,
                    Biome::TropicalSavanna, Biome::TemperateGrassland}) {
        CHECK(seen.count(static_cast<int>(b)) == 1);
    }
}

TEST(geo_seasons_are_consistent) {
    const World& w = level5();
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (!w.is_land[c]) continue;
        CHECK(w.growing_months[c] <= 12);
        CHECK(w.temperature_range_c[c] >= 0.0);
        CHECK(w.rain_season[c] >= -1.0 && w.rain_season[c] <= 1.0);
        double t = 0.0, p = 0.0, lo = 1e9, hi = -1e9;
        for (int m = 0; m < 12; ++m) {
            const double mt = w.month_temperature_c(c, m);
            t += mt;
            p += w.month_precipitation_mm(c, m);
            lo = std::min(lo, mt);
            hi = std::max(hi, mt);
            CHECK(w.month_precipitation_mm(c, m) >= 0.0);
        }
        // Months average to the annual figures and span the seasonal range.
        CHECK_NEAR(t / 12.0, w.temperature_c[c], 0.05 + 0.01 * w.temperature_range_c[c]);
        CHECK_NEAR(p, w.precipitation_mm[c], 1e-6 * (1.0 + w.precipitation_mm[c]));
        CHECK(hi - lo <= w.temperature_range_c[c] + 1e-9);
        CHECK(hi - lo >= 0.85 * w.temperature_range_c[c]);
        if (static_cast<Biome>(w.biome[c]) == Biome::IceSheet) CHECK(w.growing_months[c] == 0);
    }
}

TEST(geo_interiors_have_wider_seasons_than_coasts) {
    const World& w = level5();
    double coast = 0.0, inland = 0.0;
    int nc = 0, ni = 0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (!w.is_land[c]) continue;
        const double lat = std::abs(w.grid.center(c).z);
        if (lat < 0.6 || lat > 0.8) continue;  // mid-latitudes
        if (w.coast_distance[c] == 1) coast += w.temperature_range_c[c], ++nc;
        if (w.coast_distance[c] >= 5) inland += w.temperature_range_c[c], ++ni;
    }
    CHECK(nc > 0 && ni > 0);
    CHECK(inland / ni > coast / nc + 3.0);
}

// ---- Rivers

TEST(geo_drainage_reaches_the_sea_without_cycles) {
    const World& w = level5();
    const std::size_t n = w.cell_count();
    for (CellId c = 0; c < n; ++c) {
        if (!w.is_land[c]) {
            CHECK(w.downstream[c] == kInvalidCell);
            continue;
        }
        // Follow the drainage: must hit the ocean in fewer than n steps.
        CellId at = c;
        std::size_t steps = 0;
        while (w.is_land[at] && steps <= n) {
            const CellId next = w.downstream[at];
            CHECK(next != kInvalidCell);
            if (next == kInvalidCell) break;
            at = next;
            ++steps;
        }
        CHECK(!w.is_land[at]);
        // Downstream carries at least everything from upstream.
        const CellId d = w.downstream[c];
        if (d != kInvalidCell && w.is_land[d]) {
            CHECK(w.discharge_m3s[d] >= w.discharge_m3s[c]);
            CHECK(w.basin_km2[d] > w.basin_km2[c]);
        }
        CHECK(w.basin_km2[c] >= w.grid.area_km2(c) * (1.0 - 1e-9));
    }
}

TEST(geo_rivers_are_a_minority_of_land_and_include_great_rivers) {
    const World& w = level5();
    std::size_t land = 0, rivers = 0;
    double biggest = 0.0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (!w.is_land[c]) continue;
        ++land;
        if (is_river(w, c)) ++rivers;
        biggest = std::max(biggest, w.discharge_m3s[c]);
    }
    CHECK(rivers > land / 50);
    CHECK(rivers < land / 4);
    CHECK(biggest > 10000.0);  // an Amazon/Congo-class river somewhere
}

// ---- Soils and foraging

TEST(geo_soils_and_aquatic_bonus_are_in_range) {
    const World& w = level5();
    double river_soil = 0.0, other_soil = 0.0;
    int nr = 0, no = 0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        CHECK(w.soil_fertility[c] >= 0.0 && w.soil_fertility[c] <= 1.0);
        CHECK(w.aquatic_bonus[c] >= 0.0 && w.aquatic_bonus[c] <= 0.8);
        if (!w.is_land[c]) {
            CHECK(w.forager_capacity[c] == 0.0);
            continue;
        }
        if (is_river(w, c)) river_soil += w.soil_fertility[c], ++nr;
        else other_soil += w.soil_fertility[c], ++no;
    }
    CHECK(river_soil / nr > other_soil / no);  // alluvium
}

// ---- Species

TEST(geo_species_live_in_their_niches_and_not_everywhere) {
    const World& w = level5();
    const Content& content = default_content();
    CHECK_EQ(w.species_count(), content.species.size());
    std::size_t land = 0;
    for (CellId c = 0; c < w.cell_count(); ++c) land += w.is_land[c];
    for (std::size_t s = 0; s < content.species.size(); ++s) {
        const SpeciesDef& d = content.species[s];
        std::size_t cells = 0;
        for (CellId c = 0; c < w.cell_count(); ++c) {
            const bool present = (w.species_mask[c] >> s) & 1u;
            CHECK_EQ(present, w.abundance(c, s) > 0.0);
            if (!present) continue;
            ++cells;
            CHECK(w.is_land[c]);
            CHECK(d.coldest_month_c(w.coldest_month_c(c)) > 0.0);
            CHECK(d.warmest_month_c(w.warmest_month_c(c)) > 0.0);
            CHECK(d.annual_precip_mm(w.precipitation_mm[c]) > 0.0);
        }
        CHECK(cells > 0);
        // Most species are regional; a few (wolves) were nearly everywhere.
        CHECK(cells < (d.range_km < 5000.0 ? land / 3 : land));
    }
}

TEST(geo_world_hash_covers_content) {
    WorldParams p;
    p.grid_level = 3;
    const World a = generate_test_world(p);
    const World b = generate_test_world(p);
    CHECK_EQ(a.hash(), b.hash());
    Content other = default_content();
    other.hash ^= 1;
    const World c = generate_test_world(p, other);
    CHECK(a.hash() != c.hash());
}
