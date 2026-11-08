// Bond events, domestication and farming (M2 slice 3), and the M2 gate.
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/climate.hpp"
#include "histsim/systems/farming.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

std::size_t species_index(const Content& c, const std::string& id) {
    for (std::size_t i = 0; i < c.species.size(); ++i) {
        if (c.species[i].id == id) return i;
    }
    throw std::runtime_error("no species " + id);
}

Command bond_command(double cooling, std::int64_t years) {
    Command c;
    c.type = CommandType::BondEvent;
    c.amount = cooling;
    c.years = years;
    return c;
}

}  // namespace

// ---- Bond events

TEST(bond_intensity_rises_holds_and_fades) {
    const BondEvent b{1000, 200, 2.0};
    CHECK_EQ(bond_intensity(b, 999), 0.0);
    CHECK_EQ(bond_intensity(b, 1000), 0.0);
    CHECK_NEAR(bond_intensity(b, 1025), 0.5, 1e-12);
    CHECK_EQ(bond_intensity(b, 1050), 1.0);
    CHECK_EQ(bond_intensity(b, 1119), 1.0);
    CHECK(bond_intensity(b, 1160) < 0.6);
    CHECK_EQ(bond_intensity(b, 1200), 0.0);
    CHECK_EQ(bond_intensity(BondEvent{}, 5), 0.0);
}

TEST(bond_effect_cools_the_north_and_dries_monsoons) {
    // A world with land near both poles.
    WorldParams p;
    p.grid_level = 4;
    World w = generate_test_world(p);
    auto polar_land = [&](const World& x, double sign) {
        for (CellId c = 0; c < x.cell_count(); ++c) {
            if (x.is_land[c] && sign * x.grid.center(c).z > 0.8) return true;
        }
        return false;
    };
    while (!polar_land(w, 1.0) || !polar_land(w, -1.0)) {
        ++p.seed;
        w = generate_test_world(p);
    }
    const BondEvent b{0, 200, 2.0};
    double north = 0.0, south = 0.0, monsoon_rain = 1.0, other_rain = 1.0;
    int nn = 0, ns = 0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (!w.is_land[c]) continue;
        const BondEffect e = bond_effect(w, b, 100, c);
        CHECK(e.temp_c <= 0.0);
        CHECK(e.rain_factor >= 0.5 && e.rain_factor <= 1.0);
        const double z = w.grid.center(c).z;
        if (z > 0.8) north += e.temp_c, ++nn;
        if (z < -0.8) south += e.temp_c, ++ns;
        const bool monsoon = w.rain_season[c] > 0.5 && std::fabs(z) > 0.1 && std::fabs(z) < 0.5;
        if (monsoon) monsoon_rain = std::min(monsoon_rain, e.rain_factor);
        if (std::fabs(z) < 0.25 && w.rain_season[c] < 0.0) other_rain = std::min(other_rain, e.rain_factor);
    }
    CHECK(nn > 0 && ns > 0);
    CHECK(north / nn < -1.5);
    CHECK(south / ns > -0.5);
    CHECK(monsoon_rain < 0.85);
    CHECK_EQ(other_rain, 1.0);
}

TEST(bond_events_recur_about_every_1500_years) {
    WorldParams p;
    p.grid_level = 3;
    Simulation sim = Simulation::create(p);
    sim.step_years(4000);
    const auto& h = sim.state().climate.bond_history;
    CHECK(h.size() >= 2);
    CHECK(h.front().start_year <= 100 + static_cast<std::int64_t>(ClimateSystem::kBondFirstWithinYears));
    for (std::size_t i = 1; i < h.size(); ++i) {
        const std::int64_t gap = h[i].start_year - h[i - 1].start_year;
        CHECK(gap >= 960 && gap <= 1980);
    }
    for (const BondEvent& b : h) {
        CHECK(b.years >= 150 && b.years <= 400);
        CHECK(b.strength_c >= 0.8 && b.strength_c <= 2.0);
    }
    int events = 0;
    for (const Event& e : sim.state().events) events += e.type == EventType::BondEvent;
    CHECK_EQ(static_cast<std::size_t>(events), h.size());
}

TEST(bond_command_cools_the_north) {
    WorldParams p;
    p.grid_level = 4;
    Simulation control = Simulation::create(p);
    Simulation cold = Simulation::create(p);
    control.step_years(10);
    cold.step_years(10);
    CHECK(cold.apply(bond_command(3.0, 200)).ok);
    CHECK_EQ(cold.state().climate.bond.years, std::int64_t{200});
    control.step_years(80);
    cold.step_years(80);
    double diff = 0.0;
    int count = 0;
    for (CellId c = 0; c < cold.world().cell_count(); ++c) {
        if (cold.world().grid.center(c).z < 0.8) continue;
        diff += cold.state().env.temperature_c[c] - control.state().env.temperature_c[c];
        ++count;
    }
    CHECK(count > 0);
    CHECK(diff / count < -2.0);
}

// ---- Farming

TEST(farm_density_needs_a_suitable_domesticate) {
    WorldParams p;
    p.grid_level = 4;
    const World w = generate_test_world(p);
    const Content& c = *w.content;
    const std::size_t rice = species_index(c, "wild_rice");
    const std::size_t pigs = species_index(c, "wild_boar");
    CellId best = kInvalidCell;
    for (CellId cell = 0; cell < w.cell_count(); ++cell) {
        if (w.is_land[cell] && w.cultivated(cell, rice) > 0.6 && w.cultivated(cell, pigs) > 0.3) best = cell;
    }
    CHECK(best != kInvalidCell);
    CHECK_EQ(farm_density(w, best, 0), 0.0);
    const double crops = farm_density(w, best, 1u << rice);
    const double herds = farm_density(w, best, 1u << pigs);
    const double both = farm_density(w, best, (1u << rice) | (1u << pigs));
    CHECK(crops > 0.1);
    CHECK(herds > 0.0 && herds < crops);
    CHECK(both > crops);
    // A crop that cannot grow there adds nothing.
    for (CellId cell = 0; cell < w.cell_count(); ++cell) {
        if (w.is_land[cell] && w.cultivated(cell, rice) == 0.0) {
            CHECK_EQ(farm_density(w, cell, 1u << rice), 0.0);
            break;
        }
    }
}

TEST(farming_starts_with_dogs_only) {
    WorldParams p;
    p.grid_level = 4;
    const Simulation sim = Simulation::create(p);
    const Content& c = *sim.world().content;
    const std::size_t dogs = species_index(c, "wolves");
    bool any_dogs = false;
    for (CellId cell = 0; cell < sim.world().cell_count(); ++cell) {
        CHECK_EQ(sim.state().domesticates[cell] & ~(1u << dogs), 0u);
        any_dogs |= (sim.state().domesticates[cell] >> dogs) & 1u;
        CHECK_EQ(sim.state().env.farming[cell], 0.0);
    }
    CHECK(any_dogs);
    for (const World::WildStand& ws : sim.world().wild_stands) CHECK(ws.species != dogs);
}

// The M2 gate: farming arises in several plausible regions. On several
// worlds, within 3,000 years, crops are domesticated independently in at
// least three places, some of them far apart, always where their wild
// ancestors grow; farming spreads, and the population grows.
namespace {

// Each cell's main language family (0 where nobody lives).
std::vector<std::uint32_t> main_families(const Simulation& sim) {
    const SimState& s = sim.state();
    std::vector<std::uint32_t> out(sim.world().cell_count(), 0);
    for (CellId c = 0; c < out.size(); ++c) {
        const Cohort* lead = nullptr;
        for (const Cohort& co : s.cohorts_in(c)) {
            if (lead == nullptr || co.total() > lead->total()) lead = &co;
        }
        if (lead != nullptr) out[c] = s.languages[lead->language].family;
    }
    return out;
}

}  // namespace

TEST(m2_gate_farming_arises_in_several_plausible_regions) {
    std::size_t same_all = 0, both_all = 0;
    for (std::uint64_t seed : {1u, 2u, 3u}) {
        WorldParams p;
        p.seed = seed;
        p.grid_level = 4;
        Simulation sim = Simulation::create(p);
        const std::int64_t start = sim.state().total_population();
        const std::vector<std::uint32_t> families_then = main_families(sim);
        sim.step_years(3000);
        const SimState& s = sim.state();
        const World& w = sim.world();
        const Content& c = *w.content;

        std::vector<CellId> crop_origins;
        for (const Domestication& d : s.domestications) {
            // Plausible: where the species grows wild, in its niche.
            CHECK((w.species_mask[d.cell] >> d.species) & 1u);
            CHECK(w.abundance(d.cell, d.species) > 0.0);
            CHECK(!c.species[d.species].domesticated_before_start);
            if (c.species[d.species].kind == SpeciesKind::Plant) crop_origins.push_back(d.cell);
        }
        CHECK(crop_origins.size() >= 3);
        double farthest = 0.0;
        for (CellId a : crop_origins) {
            for (CellId b : crop_origins) farthest = std::max(farthest, w.grid.distance_km(a, b));
        }
        CHECK(farthest > 3000.0);

        const YearRecord& last = s.history.back();
        const double farmers = static_cast<double>(last.farmers) / static_cast<double>(last.population);
        CHECK(farmers > 0.2);
        CHECK(farmers < 0.98);  // foragers remain somewhere
        CHECK(last.population > start + start / 5);

        // Farmers spread their languages: a sizable share of the land changes
        // family, and some family gains much land.
        const std::vector<std::uint32_t> families_now = main_families(sim);
        std::map<std::uint32_t, long> gained;
        std::size_t same = 0, both = 0;
        for (CellId cell = 0; cell < w.cell_count(); ++cell) {
            if (families_then[cell] != 0) --gained[families_then[cell]];
            if (families_now[cell] != 0) ++gained[families_now[cell]];
            if (families_then[cell] != 0 && families_now[cell] != 0) {
                ++both;
                same += families_then[cell] == families_now[cell] ? 1 : 0;
            }
        }
        long most = 0;
        for (const auto& [family, cells] : gained) most = std::max(most, cells);
        // Across the three worlds at least 8% of the land changes family
        // (some foragers take up farming from neighbours rather than giving
        // way to farmers; one world alone can fall short). Checked below.
        same_all += same;
        both_all += both;
        CHECK(most >= 5);  // level-4 cells: about 1 million km^2 (less on broken coasts)

        // Farming only where people know a domesticate that can be raised:
        // learned where it could be, in the climate of the time, so nearly
        // all can be in the climate they are used to now.
        std::size_t known = 0, unraisable = 0;
        for (CellId cell = 0; cell < w.cell_count(); ++cell) {
            if (s.env.farming[cell] > 0.01) CHECK(s.domesticates[cell] != 0);
            for (std::size_t sp = 0; sp < c.species.size(); ++sp) {
                if (((s.domesticates[cell] >> sp) & 1u) && !c.species[sp].domesticated_before_start) {
                    ++known;
                    unraisable += raisable(w, s, cell, sp) < FarmingSystem::kMinSuitability &&
                                  !((w.species_mask[cell] >> sp) & 1u);
                }
            }
        }
        CHECK(unraisable * 20 <= known);
    }
    CHECK(static_cast<double>(same_all) < 0.92 * static_cast<double>(both_all));
}

TEST(farming_herding_wins_on_dry_grassland) {
    CHECK(pasture_quality(Biome::TemperateGrassland) > pasture_quality(Biome::TemperateForest));
    CHECK(pasture_quality(Biome::DryShrubland) > pasture_quality(Biome::Desert));
    WorldParams p;
    p.grid_level = 5;
    const World w = generate_test_world(p);
    const auto& species = w.content->species;
    // Every domesticate known: on dry grassland away from rivers the herds
    // feed more than the crops.
    std::uint32_t all = 0;
    for (std::size_t sp = 0; sp < species.size(); ++sp) all |= 1u << sp;
    std::size_t dry = 0, pastoral = 0;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (w.biome[c] != static_cast<std::uint8_t>(Biome::TemperateGrassland) || is_river(w, c)) continue;
        if (w.precipitation_mm[c] > 350.0) continue;
        const FarmParts parts = farm_parts(w, c, all);
        if (parts.herds <= 0.0) continue;
        ++dry;
        if (pastoral_share(parts) >= 0.5) ++pastoral;
    }
    CHECK(dry > 0);
    CHECK(pastoral * 4 >= dry * 3);  // at least three in four
}

// Crops follow the climate people are used to: in the world's own climate
// they are the world's; a lasting cooling shortens the growing season and
// takes land from the crops, a lasting warming lengthens the cold lands'
// summers.
TEST(farming_crops_follow_a_lasting_change_of_climate) {
    WorldParams p;
    p.grid_level = 4;
    for (const double forcing : {-10.4, 10.4}) {
        Simulation sim = Simulation::create(p);
        sim.step_years(1);
        const World& w = sim.world();
        const std::size_t n = w.cell_count(), ns = w.content->species.size();
        {
            const auto& env = sim.state().env;
            CHECK_EQ(env.crop_suit.size(), n * ns);
            std::size_t same = 0;
            for (CellId c = 0; c < n; ++c) {
                if (!w.is_land[c] || env.crop_shift_c[c] != 0.0 || env.crop_rain[c] != 1.0) continue;
                ++same;
                CHECK_EQ(env.growing_months[c], w.growing_months[c]);
                for (std::size_t sp = 0; sp < ns; ++sp) {
                    CHECK_EQ(env.crop_suit[c * ns + sp], w.cultivated_suitability[c * ns + sp]);
                    CHECK_EQ(env.crop_suit_watered[c * ns + sp], w.watered_suitability[c * ns + sp]);
                }
            }
            CHECK(same > 0);
        }
        Command cmd;
        cmd.type = CommandType::ClimateForcing;
        cmd.amount = forcing;
        sim.apply(cmd);
        sim.step_years(60);
        const auto& env = sim.state().env;
        double shift = 0.0, land = 0.0, suit_now = 0.0, suit_was = 0.0;
        long cold_now = 0, cold_was = 0, months_now = 0, months_was = 0;
        for (CellId c = 0; c < n; ++c) {
            if (!w.is_land[c]) continue;
            shift += env.normal_shift_c[c];
            land += 1.0;
            months_now += env.growing_months[c];
            months_was += w.growing_months[c];
            if (w.warm_months[c] < 5) cold_now += env.warm_months[c], cold_was += w.warm_months[c];
            for (std::size_t sp = 0; sp < ns; ++sp) {
                suit_now += env.crop_suit[c * ns + sp];
                suit_was += w.cultivated_suitability[c * ns + sp];
            }
        }
        if (forcing < 0.0) {
            CHECK(shift / land < -4.0);  // most of the way to about -8 C
            CHECK(months_now < months_was);
            CHECK(suit_now < 0.8 * suit_was);
        } else {
            CHECK(shift / land > 4.0);
            CHECK(cold_now > cold_was);  // the cold lands' summers lengthen
        }
    }
}
