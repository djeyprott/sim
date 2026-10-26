// Tests for the M1 systems: vital rates, demography, migration, climate,
// environment, and the milestone gate.
#include <algorithm>
#include <cmath>
#include <numeric>

#include "histsim/portable_math.hpp"
#include "histsim/simulation.hpp"
#include "histsim/systems/climate.hpp"
#include "histsim/systems/people.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

// A world plus an empty state sized for it, with this year's environment at
// the baseline. Tests then place cohorts and run single systems.
struct Bench {
    World world;
    SimState state;

    explicit Bench(int level = 3, std::uint64_t seed = 2) {
        WorldParams p;
        p.seed = seed;
        p.grid_level = level;
        world = generate_test_world(p);
        const std::size_t n = world.cell_count();
        state.climate.regional_temp_anomaly.assign(world.climate_grid.cell_count(), 0.0);
        state.climate.regional_precip_anomaly.assign(world.climate_grid.cell_count(), 0.0);
        state.env.temperature_c = world.temperature_c;
        state.env.precipitation_mm = world.precipitation_mm;
        state.env.capacity = world.forager_capacity;
        state.env.game.assign(n, 1.0);
        state.ever_settled.assign(n, 0);
        state.refresh(n);
    }

    // The richest cell whose neighbors are all habitable land, so spread
    // tests are not boxed in by a coastline.
    CellId best_land_cell() const {
        CellId best = kInvalidCell;
        for (CellId c = 0; c < world.cell_count(); ++c) {
            bool interior = world.forager_capacity[c] > 0.0;
            for (CellId nb : world.grid.neighbors(c)) interior = interior && world.forager_capacity[nb] > 0.0;
            if (interior && (best == kInvalidCell || world.forager_capacity[c] > world.forager_capacity[best])) {
                best = c;
            }
        }
        return best;
    }

    void place(CellId cell, std::uint32_t people) {
        Cohort c;
        c.cell = cell;
        c.culture = c.language = 1;
        for (int b = 0; b < kAgeBins; ++b) {
            const auto in_bin = static_cast<std::uint32_t>(people * stable_age_share(b));
            c.female[b] = in_bin / 2;
            c.male[b] = in_bin - in_bin / 2;
        }
        state.cohorts.push_back(c);
        state.ever_settled[cell] = 1;
        state.normalize_cohorts(world.cell_count());
    }

    void run(System& system, int years) {
        for (int y = 0; y < years; ++y) {
            StepContext ctx{world, state, world.params.seed, state.tick};
            system.step(ctx);
            state.tick += kTicksPerYear;
        }
    }
};

}  // namespace

// ------------------------------------------------------------------ vital rates

TEST(m1_vital_rates_are_plausible) {
    double tfr = 0.0;
    for (int b = 0; b < kAgeBins; ++b) {
        tfr += kYearsPerAgeBin * forager_fertility(b);
        CHECK(forager_death_probability(b) > 0.0 && forager_death_probability(b) < 1.0);
    }
    CHECK_NEAR(tfr, 5.4, 1e-3);  // Marlowe (2005) forager mean
    CHECK(forager_death_probability(0) > 5 * forager_death_probability(2));  // infant and child deaths
    for (int b = 3; b + 1 < kAgeBins; ++b) {
        CHECK(forager_death_probability(b + 1) >= forager_death_probability(b));  // rising with age
    }
    double share = 0.0;
    for (int b = 0; b < kAgeBins; ++b) share += stable_age_share(b);
    CHECK_NEAR(share, 1.0, 1e-5);
    CHECK_NEAR(replacement_fertility_multiplier(), 0.82, 0.01);
}

TEST(m1_empty_land_growth_matches_life_table) {
    // A large band with effectively unlimited land should grow about 0.7% a
    // year, the Lotka growth rate of the life table at total fertility 5.4.
    Bench b;
    const CellId c = b.best_land_cell();
    b.place(c, 200000);
    std::fill(b.state.env.capacity.begin(), b.state.env.capacity.end(), 1e12);
    DemographySystem demo;
    b.run(demo, 100);  // let the age structure settle
    const double start = static_cast<double>(b.state.total_population());
    b.run(demo, 200);
    const double rate = std::pow(static_cast<double>(b.state.total_population()) / start, 1.0 / 200) - 1.0;
    CHECK(rate > 0.004 && rate < 0.010);
}

TEST(m1_population_settles_at_capacity) {
    Bench b;
    const CellId c = b.best_land_cell();
    b.place(c, 20000);
    std::fill(b.state.env.capacity.begin(), b.state.env.capacity.end(), 0.0);
    b.state.env.capacity[c] = 50000.0;
    DemographySystem demo;
    b.run(demo, 600);
    const double ratio = static_cast<double>(b.state.total_population()) / 50000.0;
    CHECK(ratio > 0.9 && ratio < 1.1);
}

TEST(m1_famine_cuts_an_overcrowded_population) {
    Bench b;
    const CellId c = b.best_land_cell();
    b.place(c, 100000);
    std::fill(b.state.env.capacity.begin(), b.state.env.capacity.end(), 0.0);
    b.state.env.capacity[c] = 40000.0;
    DemographySystem demo;
    b.run(demo, 10);
    CHECK(b.state.total_population() < 80000);
}

TEST(m1_demography_keeps_cohorts_valid_and_counts_exact) {
    Bench b;
    b.place(b.best_land_cell(), 5000);
    DemographySystem demo;
    const std::int64_t start = b.state.total_population();
    b.run(demo, 50);
    CHECK_EQ(b.state.total_population(), start + b.state.ledger.births - b.state.ledger.deaths);
    for (std::size_t i = 1; i < b.state.cohorts.size(); ++i) {
        CHECK(b.state.cohorts[i - 1].key_less(b.state.cohorts[i]));
    }
}

// ------------------------------------------------------------------ migration

TEST(m1_migration_conserves_people_and_spreads_them) {
    Bench b(4);
    const CellId c = b.best_land_cell();
    b.place(c, static_cast<std::uint32_t>(3.0 * b.world.forager_capacity[c]));  // badly overcrowded
    MigrationSystem mig;
    const std::int64_t start = b.state.total_population();
    b.run(mig, 30);
    CHECK_EQ(b.state.total_population(), start);
    CHECK(b.state.ledger.migrants > 0);
    // Without births only the source is crowded, so bands reach its neighbors.
    std::size_t inhabited = 0;
    for (std::uint32_t p : b.state.cell_population) inhabited += p > 0 ? 1 : 0;
    CHECK(inhabited >= 4);
    for (const Cohort& co : b.state.cohorts) {
        CHECK(b.world.is_land[co.cell] == 1);
    }
    bool settled_event = false;
    for (const Event& e : b.state.events) settled_event |= e.type == EventType::CellSettled;
    CHECK(settled_event);
}

TEST(m1_foragers_spread_from_a_homeland) {
    // One band and an empty continent: growth fills the homeland, crowding
    // pushes bands out, and a frontier advances cell by cell.
    Bench b(4);
    const CellId c = b.best_land_cell();
    b.place(c, 500);
    DemographySystem demo;
    MigrationSystem mig;
    for (int y = 0; y < 400; ++y) {
        b.run(demo, 1);
        b.state.tick -= kTicksPerYear;  // both systems act in the same year
        b.run(mig, 1);
    }
    std::size_t inhabited = 0;
    for (std::uint32_t p : b.state.cell_population) inhabited += p > 0 ? 1 : 0;
    CHECK(inhabited > 20);
    CHECK(b.state.total_population() > 5000);
}

TEST(m1_small_groups_are_absorbed) {
    Bench b(4);
    const CellId c = b.best_land_cell();
    b.place(c, 10000);
    Cohort visitors;  // a tiny band of another group in the same cell
    visitors.cell = c;
    visitors.culture = visitors.language = 99;
    visitors.female[4] = 3;
    visitors.male[4] = 3;
    b.state.cohorts.push_back(visitors);
    b.state.normalize_cohorts(b.world.cell_count());
    CHECK_EQ(b.state.cohorts_in(c).size(), std::size_t{2});
    MigrationSystem mig;
    b.run(mig, 1);
    bool visitors_left = false;
    for (const Cohort& co : b.state.cohorts) visitors_left |= co.cell == c && co.culture == 99;
    CHECK(!visitors_left);
}

// ------------------------------------------------------------------ climate

TEST(m1_portable_sin_cos_match_libm) {
    for (double x = -500.0; x < 500.0; x += 0.731) {
        CHECK_NEAR(pmath::sin(x), std::sin(x), 1e-12);
        CHECK_NEAR(pmath::cos(x), std::cos(x), 1e-12);
    }
}

TEST(m1_climate_is_bounded_and_variable) {
    Bench b(3);
    ClimateSystem climate;
    std::vector<double> anomalies;
    for (int y = 0; y < 3000; ++y) {
        b.run(climate, 1);
        anomalies.push_back(b.state.climate.global_anomaly_c);
    }
    const double mean = std::accumulate(anomalies.begin(), anomalies.end(), 0.0) / static_cast<double>(anomalies.size());
    double var = 0.0;
    for (double a : anomalies) var += (a - mean) * (a - mean);
    const double sd = std::sqrt(var / static_cast<double>(anomalies.size()));
    const auto [lo, hi] = std::minmax_element(anomalies.begin(), anomalies.end());
    CHECK(std::fabs(mean) < 0.5);
    CHECK(sd > 0.1 && sd < 0.8);
    CHECK(*lo > -3.0 && *hi < 3.0);

    std::size_t eruptions = 0;
    for (const Event& e : b.state.events) eruptions += e.type == EventType::Eruption ? 1 : 0;
    CHECK(eruptions > 25 && eruptions < 80);  // ~1 in 60 years over 3,000 years
}

TEST(m1_eruptions_cool_the_planet) {
    Bench b(3);
    ClimateSystem climate;
    double cooling = 0.0;
    int count = 0;
    double before = 0.0;
    for (int y = 0; y < 6000; ++y) {
        const std::size_t events = b.state.events.size();
        before = b.state.climate.global_anomaly_c;
        b.run(climate, 1);
        if (b.state.events.size() > events) {
            b.run(climate, 1);  // the year after, aerosols still in the air
            cooling += b.state.climate.global_anomaly_c - before;
            ++count;
        }
    }
    CHECK(count > 20);
    CHECK(cooling / count < -0.1);
}

TEST(m1_environment_tracks_climate_and_hunting) {
    Bench b(3);
    const CellId c = b.best_land_cell();
    EnvironmentSystem env;
    // A colder year lowers capacity.
    b.run(env, 1);
    const double normal = b.state.env.capacity[c];
    b.state.env.temperature_c[c] -= 15.0;
    b.run(env, 1);
    CHECK(b.state.env.capacity[c] < normal);
    // Heavy hunting depletes game.
    b.state.env.temperature_c[c] += 15.0;
    b.place(c, static_cast<std::uint32_t>(3.0 * b.world.forager_capacity[c]));
    b.run(env, 30);
    CHECK(b.state.env.game[c] < 0.6);
}

// ------------------------------------------------------------------ gate

// M1 gate: forager populations settle near carrying capacity. Across several
// worlds, the population over the last 200 of 700 years averages 80-105% of
// that year's capacity and holds steady (no boom-bust collapse).
TEST(m1_gate_populations_settle_near_capacity) {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        WorldParams p;
        p.seed = seed;
        p.grid_level = 4;
        Simulation sim = Simulation::create(p);
        sim.step_years(700);
        const auto& h = sim.state().history;
        CHECK_EQ(h.size(), std::size_t{700});
        double sum = 0.0, lo = 1e9, hi = 0.0;
        for (std::size_t i = h.size() - 200; i < h.size(); ++i) {
            const double ratio = static_cast<double>(h[i].population) / h[i].capacity;
            sum += ratio;
            lo = std::min(lo, ratio);
            hi = std::max(hi, ratio);
        }
        const double mean = sum / 200.0;
        CHECK(mean > 0.80 && mean < 1.05);
        CHECK(hi - lo < 0.2);
    }
}
