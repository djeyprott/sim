// Colonists, folk migrations and chain reactions, and prestige in language
// shift.
#include <algorithm>
#include <vector>

#include "histsim/simulation.hpp"
#include "histsim/systems/conditions.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/systems/farming.hpp"
#include "histsim/systems/people.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

struct Start {
    Simulation sim;
    SimState s;
    CellId home = kInvalidCell;
};

// A world at its start and a well-peopled cell whose neighbours are all land.
Start start() {
    WorldParams p;
    p.grid_level = 4;
    Start x{Simulation::create(p), {}, kInvalidCell};
    x.s = x.sim.state();
    const World& w = x.sim.world();
    for (CellId c = 0; c < w.cell_count() && x.home == kInvalidCell; ++c) {
        if (x.s.cell_population[c] < 8000) continue;
        bool land = true;
        for (CellId nb : w.grid.neighbors(c)) land = land && w.is_land[nb] && x.s.env.capacity[nb] > 0.0;
        if (land) x.home = c;
    }
    CHECK(x.home != kInvalidCell);
    return x;
}

void migrate(const World& w, SimState& s, int years = 1) {
    MigrationSystem migration;
    for (int y = 0; y < years; ++y) {
        StepContext ctx{w, s, 5, s.tick};
        migration.step(ctx);
        s.tick += kTicksPerYear;
    }
}

}  // namespace

TEST(migration_crowded_farmers_send_colonists_who_keep_their_language) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    // Farmers with every domesticate, at their land's capacity.
    std::uint32_t all = 0;
    for (std::size_t sp = 0; sp < w.content->species.size(); ++sp) all |= 1u << sp;
    s.domesticates[x.home] = all;
    s.env.farming[x.home] = 1.0;
    s.env.capacity[x.home] = static_cast<double>(s.cell_population[x.home]);
    for (auto i = s.cohort_begin[x.home]; i < s.cohort_begin[x.home + 1]; ++i) s.cohorts[i].farming = 1.0;
    const std::uint32_t language = s.cohorts_in(x.home).front().language;
    const std::int64_t people = s.total_population();
    std::int64_t colonists = 0;
    // Each cell looks in its turn (less often on coarse grids).
    const auto period = MigrationSystem::kColonizeYears * static_cast<std::int64_t>(std::max(1.0, w.cell_scale()));
    for (std::int64_t y = 0; y < period; ++y) {
        s.env.capacity[x.home] = static_cast<double>(s.cell_population[x.home]);
        migrate(w, s);
        colonists += s.colonists;
    }
    CHECK(colonists > 0);
    CHECK_EQ(s.total_population(), people);  // moving, not dying
    // Somewhere else a farming group speaks the farmers' language and has
    // their crops.
    bool settled = false;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (c == x.home) continue;
        for (const Cohort& co : s.cohorts_in(c)) {
            if (co.language == language && co.farming >= 0.5 && s.domesticates[c] != 0) settled = true;
        }
    }
    CHECK(settled);
}

TEST(migration_a_famine_sets_a_people_moving) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const std::int64_t people = s.total_population();
    // The land now feeds under half its people: most would leave.
    s.env.capacity[x.home] = 0.4 * static_cast<double>(s.cell_population[x.home]);
    migrate(w, s);
    CHECK(!s.migrations.empty());
    const FolkMovement& m = s.migrations.front();
    CHECK_EQ(m.from, x.home);
    CHECK(m.cause == MigrationCause::Famine);
    CHECK(m.people >= MigrationSystem::kFolkRecordPeople);
    CHECK(w.grid.distance_km(m.from, m.to) <= MigrationSystem::kExodusKm);
    CHECK(m.to != m.from);
    CHECK_EQ(s.total_population(), people);
    bool logged = false;
    for (const Event& e : s.events) logged |= e.type == EventType::FolkMigration && e.cell == x.home;
    CHECK(logged);
    // A lean year is weathered: with food at 0.85 of need, fewer than half
    // would go.
    Start y = start();
    y.s.env.capacity[y.home] = 0.85 * static_cast<double>(y.s.cell_population[y.home]);
    migrate(y.sim.world(), y.s);
    CHECK(y.s.migrations.empty());
}

TEST(migration_war_sends_refugees_out_of_the_conquerors_land) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    ConditionsSystem conditions;
    StepContext ctx{w, s, 5, s.tick};
    conditions.step(ctx);
    s.conditions.danger[x.home] = 0.05f;  // a sack: one in twenty killed
    migrate(w, s);
    CHECK(!s.migrations.empty());
    CHECK(s.migrations.front().cause == MigrationCause::War);
    // A raid's toll of one in a hundred moves fewer than half to go.
    Start y = start();
    StepContext ctx2{y.sim.world(), y.s, 5, y.s.tick};
    conditions.step(ctx2);
    y.s.conditions.danger[y.home] = 0.01f;
    migrate(y.sim.world(), y.s);
    CHECK(y.s.migrations.empty());
}

TEST(migration_people_driven_out_after_newcomers_came_are_pushed_on) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    // Newcomers arrived ten years ago; now the land fails.
    s.migrations.push_back({s.tick, x.home, x.home, 5000, 1, 1, MigrationCause::War, 0});
    s.last_arrival.assign(w.cell_count(), 0);
    s.last_arrival_tick.assign(w.cell_count(), 0);
    s.last_arrival[x.home] = 1;
    s.last_arrival_tick[x.home] = s.tick;
    s.tick += 10 * kTicksPerYear;
    s.env.capacity[x.home] = 0.4 * static_cast<double>(s.cell_population[x.home]);
    migrate(w, s);
    CHECK(s.migrations.size() > 1);
    CHECK(s.migrations[1].cause == MigrationCause::Displaced);
    CHECK_EQ(s.migrations[1].pushed_by, 1u);
}

TEST(migration_people_move_toward_land_that_would_feed_them_better) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    // One neighbour's land feeds twice as many as live there; the others
    // are as full as home.
    const auto nbs = w.grid.neighbors(x.home);
    const CellId rich = nbs.front();
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (s.env.capacity[c] > 0.0) s.env.capacity[c] = static_cast<double>(std::max<std::uint32_t>(1, s.cell_population[c]));
    }
    s.env.capacity[rich] = 2.0 * static_cast<double>(std::max<std::uint32_t>(1000, s.cell_population[rich]));
    std::vector<std::uint32_t> before = s.cell_population;
    migrate(w, s);
    std::int64_t to_rich = static_cast<std::int64_t>(s.cell_population[rich]) - before[rich];
    for (CellId nb : nbs) {
        if (nb == rich) continue;
        CHECK(to_rich > static_cast<std::int64_t>(s.cell_population[nb]) - before[nb]);
    }
}

TEST(culture_farmers_outweigh_more_numerous_foragers_in_language_shift) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    // Split the cell's people: 40% farmers speaking another language.
    Cohort& locals = s.cohorts[s.cohort_begin[x.home]];
    std::uint32_t other = 0;
    for (const Cohort& co : s.cohorts) {
        if (co.language != locals.language) {
            other = co.language;
            break;
        }
    }
    CHECK(other != 0);
    Cohort farmers = locals;
    farmers.language = other;
    farmers.farming = 1.0;
    for (int b = 0; b < kAgeBins; ++b) {
        farmers.female[b] = locals.female[b] * 2 / 5;
        farmers.male[b] = locals.male[b] * 2 / 5;
        locals.female[b] -= farmers.female[b];
        locals.male[b] -= farmers.male[b];
    }
    locals.farming = 0.0;
    const std::int64_t farmers_before = farmers.total();
    s.cohorts.push_back(farmers);
    s.normalize_cohorts(w.cell_count());
    CultureSystem culture;
    StepContext ctx{w, s, 9, s.tick};
    culture.step(ctx);
    // Foragers take up the farmers' language, not the other way round.
    std::int64_t speakers = 0;
    for (const Cohort& co : s.cohorts_in(x.home)) speakers += co.language == other ? co.total() : 0;
    CHECK(speakers > farmers_before);
}

TEST(migration_a_hard_pressed_tribe_moves_as_a_whole_and_the_locals_mostly_stay) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    // A herding tribe, starving, rules the home cell.
    s.polity_of.assign(w.cell_count(), 0);
    s.polities.assign(1, Polity{});
    Polity tribe;
    tribe.id = 1;
    tribe.name = "Testfolk";
    tribe.capital = x.home;
    tribe.culture = s.cohorts_in(x.home).front().culture;
    tribe.language = s.cohorts_in(x.home).front().language;
    tribe.pressure[static_cast<std::size_t>(Pressure::Famine)] = 1.0;
    s.polities.push_back(tribe);
    s.polity_of[x.home] = 1;
    std::size_t i_mobility = 0;
    while (w.content->traits[i_mobility].id != "mobility") ++i_mobility;
    for (auto i = s.cohort_begin[x.home]; i < s.cohort_begin[x.home + 1]; ++i) s.cohorts[i].traits[i_mobility] = 0.9f;
    const std::int64_t people = s.total_population();
    const std::uint32_t root = s.cultures[tribe.culture].root;
    auto others = [&](CellId c) {
        std::int64_t n = 0;
        for (const Cohort& co : s.cohorts_in(c)) n += s.cultures[co.culture].root != root ? co.total() : 0;
        return n;
    };
    std::vector<std::int64_t> before(w.cell_count(), 0);
    for (int year = 0; year < 300 && s.polities[1].wandered == kNever; ++year) {
        for (CellId c = 0; c < w.cell_count(); ++c) before[c] = others(c);
        s.polities[1].pressure[static_cast<std::size_t>(Pressure::Famine)] = 1.0;
        migrate(w, s);
    }
    const Polity& moved = s.polities[1];
    CHECK(moved.wandered != kNever);
    CHECK(moved.capital != x.home);
    CHECK(s.polity_of[x.home] != 1);  // the land left behind is free
    CHECK_EQ(s.polity_of[moved.capital], 1u);
    CHECK(s.march_deaths > 0);  // a few die on the march
    CHECK_EQ(s.total_population(), people - s.march_deaths);  // and nobody else
    bool logged = false;
    for (const FolkMovement& m : s.migrations) {
        logged |= m.cause == MigrationCause::Wandering && m.polity == 1 && m.to == moved.capital;
    }
    CHECK(logged);
    // Its people now live in the new land, and most of the locals there
    // stay to live under them (at most kMaxDisplace are pushed on).
    std::int64_t newcomers = 0;
    for (const Cohort& co : s.cohorts_in(moved.capital)) newcomers += s.cultures[co.culture].root == root ? co.total() : 0;
    CHECK(newcomers > 0);
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (s.polity_of[c] != 1 || before[c] == 0) continue;
        CHECK(static_cast<double>(others(c)) >= 0.5 * static_cast<double>(before[c]));
    }
}

TEST(migration_a_wandering_tribe_goes_step_by_step) {
    Start x = start();
    SimState& s = x.s;
    const World& w = x.sim.world();
    s.polity_of.assign(w.cell_count(), 0);
    s.polities.assign(1, Polity{});
    Polity tribe;
    tribe.id = 1;
    tribe.name = "Testfolk";
    tribe.capital = x.home;
    tribe.culture = s.cohorts_in(x.home).front().culture;
    tribe.language = s.cohorts_in(x.home).front().language;
    s.polities.push_back(tribe);
    s.polity_of[x.home] = 1;
    // Already on the road, to land a few steps away.
    CellId goal = kInvalidCell;
    for (CellId c = 0; c < w.cell_count() && goal == kInvalidCell; ++c) {
        const double km = w.grid.distance_km(x.home, c);
        // Warm land, so the way there does not cross an ice sheet.
        if (km > 1200.0 && km < 2000.0 && w.is_land[c] && s.env.capacity[c] > 0.0 && w.temperature_c[c] > 5.0) goal = c;
    }
    CHECK(goal != kInvalidCell);
    s.polities[1].trek_to = goal;
    const std::int64_t people = s.total_population();
    const std::int64_t deaths = s.ledger.deaths;
    std::size_t steps = 0;
    double last = w.grid.distance_km(x.home, goal);
    for (int year = 0; year < 400 && s.polities[1].trek_to != kInvalidCell; ++year) {
        const CellId before = s.polities[1].capital;
        migrate(w, s);
        const CellId now = s.polities[1].capital;
        if (now == before) continue;
        ++steps;
        const auto around = w.grid.neighbors(before);
        CHECK(std::find(around.begin(), around.end(), now) != around.end());  // one step at a time
        CHECK(w.grid.distance_km(now, goal) < last);  // always nearer the goal
        last = w.grid.distance_km(now, goal);
        CHECK_EQ(s.polity_of[now], 1u);
    }
    CHECK(steps >= 3);  // the goal was several steps away
    CHECK(s.polities[1].trek_to == kInvalidCell);  // arrived, or settled where blocked
    // The march killed some, and they are in the ledger.
    CHECK(s.march_deaths > 0);
    CHECK_EQ(s.ledger.deaths - deaths, s.march_deaths);
    CHECK_EQ(s.total_population(), people - s.march_deaths);
}

TEST(normalize_with_arrivals_matches_adding_them_first) {
    // Arrivals passed alongside give exactly what adding them to the list
    // and normalizing does: same order, same merged traits, empties dropped.
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    const std::size_t n = sim.world().cell_count();
    SimState a = sim.state();
    std::vector<Cohort> arrivals;
    for (std::size_t i = 0; i < a.cohorts.size(); i += 3) {
        Cohort band = a.cohorts[i];
        band.cell = static_cast<CellId>((band.cell + 7) % n);
        if (i % 2 == 0) band.language += 1;
        for (auto& t : band.traits) t += 0.125f;
        for (int b = 0; b < kAgeBins; ++b) {
            band.female[b] /= 3;
            band.male[b] = i % 9 == 0 ? 0 : band.male[b] / 3;
            if (i % 9 == 0) band.female[b] = 0;  // some arrive empty
        }
        arrivals.push_back(band);
    }
    std::reverse(arrivals.begin(), arrivals.end());
    SimState b = a;
    a.cohorts.insert(a.cohorts.end(), arrivals.begin(), arrivals.end());
    a.normalize_cohorts(n);
    b.normalize_cohorts(n, arrivals);
    CHECK(a.cohorts.size() == b.cohorts.size());
    bool same = a.cohorts.size() == b.cohorts.size();
    for (std::size_t i = 0; same && i < a.cohorts.size(); ++i) {
        const Cohort &x = a.cohorts[i], &y = b.cohorts[i];
        same = x.cell == y.cell && x.culture == y.culture && x.language == y.language && x.traits == y.traits &&
               x.dialect == y.dialect && x.female == y.female && x.male == y.male && x.farming == y.farming;
    }
    CHECK(same);
    CHECK(a.cohort_begin == b.cohort_begin);
    CHECK(a.cell_population == b.cell_population);
}
