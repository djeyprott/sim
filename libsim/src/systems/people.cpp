#include "histsim/systems/people.hpp"

#include "histsim/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <mutex>

#include "histsim/systems/bondage.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/systems/farming.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/systems/religion.hpp"
#include "histsim/systems/technology.hpp"
#include "histsim/systems/war.hpp"
#include "vital_rates.inc"


namespace histsim {

namespace {

// Relative tendency to migrate by age bin: young adults most, the old least.
constexpr std::array<double, kAgeBins> kMigrationByAge = {
    0.8, 0.7, 0.7, 1.4, 1.5, 1.3, 1.1, 1.0, 0.8, 0.6, 0.4,
    0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3};

std::int64_t power_of_ten_floor(std::int64_t n) {
    if (n < 1) return 0;
    std::int64_t p = 1;
    while (p <= n / 10) p *= 10;
    return p;
}

constexpr std::uint32_t kColonizeEntity = 0x40000000u;
constexpr std::uint32_t kExodusEntity = 0x50000000u;
constexpr std::uint32_t kWanderEntity = 0x60000000u;
constexpr std::uint32_t kTrekEntity = 0x70000000u;
constexpr std::uint32_t kSettleEntity = 0x80000000u;

double crowding_of(const SimState& s, CellId c) {
    const double k = s.env.capacity[c];
    if (k <= 0.0) return s.cell_population[c] > 0 ? 50.0 : 0.0;
    return static_cast<double>(s.cell_population[c]) / k;
}

}  // namespace

// Life expectancy and net reproduction for yearly death chances by bin,
// counting person-years by single years of age (at mid-year).
LifeTable life_table(const std::array<double, kAgeBins>& q) {
    LifeTable t;
    double alive = 1.0;
    for (int age = 0; age < kAgeBins * kYearsPerAgeBin + 10; ++age) {
        const int b = std::min(kAgeBins - 1, age / kYearsPerAgeBin);
        const double mid = alive * (1.0 - 0.5 * q[b]);
        t.life_expectancy += mid;
        t.net_reproduction += mid * vital::kForagerFertility[b] * vital::kFemaleShareOfBirths;
        alive *= 1.0 - q[b];
    }
    return t;
}

const LifeTable& forager_life_table() {
    static const LifeTable t = [] {
        std::array<double, kAgeBins> q{};
        for (int b = 0; b < kAgeBins; ++b) q[b] = vital::kForagerDeathProb[b];
        return life_table(q);
    }();
    return t;
}

LifeTable cell_life_table(const SimState& s, CellId c) {
    std::array<double, kAgeBins> q{};
    const double urban = s.urban.size() > c ? s.urban[c] : 0.0;
    const bool sick = s.env.disease_mortality.size() >= (static_cast<std::size_t>(c) + 1) * kDiseaseAgeGroups;
    for (int b = 0; b < kAgeBins; ++b) {
        const double extra = sick ? s.env.disease_mortality[c * kDiseaseAgeGroups + disease_age_group(b)] : 0.0;
        q[b] = std::min(1.0, vital::kForagerDeathProb[b] * (1.0 + kUrbanMortality * urban) + extra);
    }
    return life_table(q);
}

double cell_replacement_fertility(const SimState& s, CellId c) {
    const double nrr = cell_life_table(s, c).net_reproduction;
    if (nrr <= 0.0) return 1.0 / vital::kReplacementFertilityMultiplier;
    return vital::kReplacementFertilityMultiplier * forager_life_table().net_reproduction / nrr;
}

double forager_death_probability(int b) { return vital::kForagerDeathProb[b]; }
double forager_fertility(int b) { return vital::kForagerFertility[b]; }
double stable_age_share(int b) { return vital::kStableAgeShare[b]; }
double female_share_of_births() { return vital::kFemaleShareOfBirths; }
double replacement_fertility_multiplier() { return vital::kReplacementFertilityMultiplier; }

Cohort make_stable_cohort(CellId cell, std::uint32_t culture, std::uint32_t language, std::uint32_t n) {
    Cohort c;
    c.cell = cell;
    c.culture = culture;
    c.language = language;
    std::uint32_t assigned = 0;
    for (int b = 0; b < kAgeBins; ++b) {
        const auto in_bin = static_cast<std::uint32_t>(static_cast<double>(n) * vital::kStableAgeShare[b]);
        const auto f = static_cast<std::uint32_t>(static_cast<double>(in_bin) * vital::kFemaleShareOfBirths);
        c.female[b] = f;
        c.male[b] = in_bin - f;
        assigned += in_bin;
    }
    // Rounding leftovers go to the youngest bin, alternating sexes.
    for (std::uint32_t i = 0; assigned < n; ++i, ++assigned) {
        (i % 2 == 0 ? c.female[0] : c.male[0]) += 1;
    }
    return c;
}

// ------------------------------------------------------------------ demography

void DemographySystem::step(StepContext& ctx) {
    SimState& s = ctx.state;
    const std::size_t n = ctx.world.cell_count();
    const std::int64_t before = s.total_population();
    s.disease_deaths.assign(n, 0);
    std::int64_t all_births = 0, all_deaths = 0;

    // Births replace deaths at capacity under each cell's own death rates.
    // Cells are independent (each cohort has its own random stream), so they
    // run in parallel; per-cell tallies are summed afterwards.
    std::vector<std::int64_t> cell_births(n, 0), cell_deaths(n, 0), cell_disease(n, 0);
    parallel_for(n, [&](std::size_t c_begin, std::size_t c_end) {
    for (std::size_t c = c_begin; c < c_end; ++c) {
    if (s.cohort_begin[c] == s.cohort_begin[c + 1]) continue;
    const double replacement = cell_replacement_fertility(s, static_cast<CellId>(c));
    std::int64_t& births_total = cell_births[c];
    std::int64_t& deaths_total = cell_deaths[c];
    for (std::size_t i = s.cohort_begin[c]; i < s.cohort_begin[c + 1]; ++i) {
        Cohort& co = s.cohorts[i];
        const double crowding = crowding_of(s, co.cell);
        // Farmers' extra fertility is room to grow, not an excess at capacity.
        const double headroom = (1.0 + kFarmerExtraFertility * co.farming) / vital::kReplacementFertilityMultiplier - 1.0;
        const double fert_mult = std::min(
            kMaxFertilityMultiplier,
            std::max(kMinFertilityMultiplier, std::min(kMaxReplacement, replacement) * (1.0 + headroom * (1.0 - crowding))));
        // Famine past capacity; stores (pottery, granaries) soften it.
        const double famine = crowding > 1.0
                                  ? kFamineMortalityPerCrowding * (crowding - 1.0) *
                                        (1.0 + technique_effects(ctx.world, co.cell, techniques_in(s, co.cell)).famine)
                                  : 0.0;
        // Towns are deadlier (crowding, dirt, water): the urban graveyard.
        const double urban = s.urban.empty() ? 0.0 : s.urban[co.cell];
        // The enslaved die younger and have fewer children.
        const double mort_mult = std::min(kMaxMortalityMultiplier, 1.0 + famine) * (1.0 + kUrbanMortality * urban) *
                                 (1.0 + kBondMortality * co.bonded);
        RngStream rng = ctx.rng(Stream::Demography, static_cast<std::uint32_t>(i));

        // Deaths: the life table (raised by famine) plus disease. Deaths are
        // drawn once; the disease share of them is counted in proportion.
        const double* disease =
            s.env.disease_mortality.empty() ? nullptr : &s.env.disease_mortality[co.cell * kDiseaseAgeGroups];
        double disease_deaths = 0.0;
        for (int b = 0; b < kAgeBins; ++b) {
            const double base = vital::kForagerDeathProb[b] * mort_mult;
            const double extra = disease ? disease[disease_age_group(b)] : 0.0;
            const double q = std::min(1.0, base + extra);
            const auto df = std::min<std::int64_t>(co.female[b], stochastic_round(q * co.female[b], rng));
            const auto dm = std::min<std::int64_t>(co.male[b], stochastic_round(q * co.male[b], rng));
            co.female[b] -= static_cast<std::uint32_t>(df);
            co.male[b] -= static_cast<std::uint32_t>(dm);
            deaths_total += df + dm;
            if (extra > 0.0) disease_deaths += static_cast<double>(df + dm) * extra / (base + extra);
        }
        if (disease_deaths > 0.0) {
            const auto d = static_cast<std::uint32_t>(disease_deaths + 0.5);
            s.disease_deaths[co.cell] += d;
            cell_disease[c] += d;
        }

        // Births, to the women who survived the year.
        double expected = 0.0;
        for (int b = 0; b < kAgeBins; ++b) expected += co.female[b] * vital::kForagerFertility[b];
        const std::int64_t births = stochastic_round(expected * fert_mult * (1.0 - kBondFertility * co.bonded), rng);
        const std::int64_t girls = std::min(births, stochastic_round(static_cast<double>(births) * vital::kFemaleShareOfBirths, rng));

        // Aging: a fifth of each 5-year bin moves up a bin each year. Work from
        // the top down so nobody moves twice.
        for (int b = kAgeBins - 2; b >= 0; --b) {
            const auto mf = std::min<std::int64_t>(co.female[b], stochastic_round(co.female[b] / 5.0, rng));
            const auto mm = std::min<std::int64_t>(co.male[b], stochastic_round(co.male[b] / 5.0, rng));
            co.female[b] -= static_cast<std::uint32_t>(mf);
            co.female[b + 1] += static_cast<std::uint32_t>(mf);
            co.male[b] -= static_cast<std::uint32_t>(mm);
            co.male[b + 1] += static_cast<std::uint32_t>(mm);
        }
        co.female[0] += static_cast<std::uint32_t>(girls);
        co.male[0] += static_cast<std::uint32_t>(births - girls);
        births_total += births;
    }
    }
    }, 16);
    for (CellId c = 0; c < n; ++c) {
        all_births += cell_births[c];
        all_deaths += cell_deaths[c];
        s.disease_deaths_total += cell_disease[c];
    }

    s.ledger.births += all_births;
    s.ledger.deaths += all_deaths;
    s.normalize_cohorts(n);

    const std::int64_t after = before + all_births - all_deaths;
    if (power_of_ten_floor(after) != power_of_ten_floor(before)) {
        s.events.push_back({ctx.tick, EventType::WorldPopulation, kInvalidCell, after});
    }
}

// ------------------------------------------------------------------ migration

void MigrationSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();

    std::vector<Cohort> arrivals;
    std::vector<std::pair<CellId, std::uint32_t>> carried;  // (destination, domesticates)
    const std::size_t i_mobility = [&] {
        const auto& traits = w.content->traits;
        for (std::size_t t = 0; t < traits.size(); ++t) {
            if (traits[t].id == "mobility") return t;
        }
        return traits.size();
    }();
    // How readily a group moves, by its mobility (1 for typical foragers).
    auto restless = [&](const Cohort& co, double strength) {
        return i_mobility < kMaxTraits ? mobility_factor(co.traits[i_mobility], strength) : 1.0;
    };
    std::int64_t moved = 0;

    // ---- Everyday movement, per cell in parallel: each cell's bands are
    // gathered per chunk and added in cell order afterwards.
    struct Outbox {
        std::size_t begin = 0;
        std::vector<Cohort> arrivals;
        std::vector<std::pair<CellId, std::uint32_t>> carried;
        std::int64_t moved = 0;
    };
    std::vector<Outbox> outboxes;
    std::mutex outbox_mutex;
    parallel_for(n, [&](std::size_t c_begin, std::size_t c_end) {
    Outbox out;
    out.begin = c_begin;
    std::array<double, 8> weight{};
    std::array<CellId, 8> dest{};
    for (CellId c = static_cast<CellId>(c_begin); c < c_end; ++c) {
        const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
        if (first == last) continue;

        const double crowding = crowding_of(s, c);
        const bool starving = s.env.capacity[c] <= 0.0;
        // Rates are for level-5 cells; smaller cells trade people faster, for
        // the same pace in km a year. (Not for the same diffusion: that
        // churns a fine grid's people so much that cells settle well below
        // their capacity and dialects blur into each other.)
        const double rate =
            starving ? 1.0
                     : std::min(kMaxGridRate,
                                w.per_step(std::min(kMaxRate, kExploratoryRate + kRateAtCapacity * crowding * crowding)));

        // Destinations: land neighbors that can feed someone, by spare
        // capacity (plus a little for any capacity) over the cost of climbing.
        std::size_t count = 0;
        double weight_sum = 0.0;
        for (CellId nb : w.grid.neighbors(c)) {
            const double k = s.env.capacity[nb];
            if (k <= 0.0) continue;
            const double spare = std::max(0.0, k - static_cast<double>(s.cell_population[nb]));
            const double climb_km = std::fabs(w.elevation_m[nb] - w.elevation_m[c]) / 1000.0;
            weight[count] = (spare + 0.05 * k) / (1.0 + kClimbCostPerKm * climb_km);
            dest[count] = nb;
            weight_sum += weight[count];
            ++count;
        }
        if (count == 0 || weight_sum <= 0.0) continue;
        // Herders move with their animals: only crop farmers are tied down.
        const double pastoral = s.env.pastoral.empty() ? 0.0 : s.env.pastoral[c];

        for (auto i = first; i < last; ++i) {
            Cohort& co = s.cohorts[i];
            const double settled = co.farming * (1.0 - pastoral);
            // The most mobile wander more; settled farmers already stay put
            // (kFarmerStay).
            const double moves = std::max(1.0, restless(co, kMobileWander));
            RngStream rng = ctx.rng(Stream::Migration, i);
            // Pick this band's destination.
            double pick = rng.uniform() * weight_sum;
            std::size_t d = 0;
            while (d + 1 < count && pick >= weight[d]) {
                pick -= weight[d];
                ++d;
            }
            Cohort band;
            band.cell = dest[d];
            band.culture = co.culture;
            band.language = co.language;
            band.traits = co.traits;
            band.dialect = co.dialect;
            band.farming = co.farming;
            band.bonded = co.bonded;
            std::int64_t band_size = 0;
            for (int b = 0; b < kAgeBins; ++b) {
                const double share = std::min(1.0, rate * kMigrationByAge[b] * (1.0 - kFarmerStay * settled) * moves);
                const auto mf = std::min<std::int64_t>(co.female[b], stochastic_round(share * co.female[b], rng));
                const auto mm = std::min<std::int64_t>(co.male[b], stochastic_round(share * co.male[b], rng));
                co.female[b] -= static_cast<std::uint32_t>(mf);
                co.male[b] -= static_cast<std::uint32_t>(mm);
                band.female[b] = static_cast<std::uint32_t>(mf);
                band.male[b] = static_cast<std::uint32_t>(mm);
                band_size += mf + mm;
            }
            if (band_size > 0) {
                out.arrivals.push_back(band);
                out.moved += band_size;
                // Farming migrants bring their crops and herds, if they come
                // in numbers enough to farm rather than to be absorbed (a
                // few newcomers marry in and take up the local ways).
                const double there = static_cast<double>(s.cell_population[band.cell]);
                const bool farm_on = static_cast<double>(band_size) >= kMinGroupShare * there && band_size >= kMinGroupSize;
                if (farm_on && co.farming > 0.1 && !s.domesticates.empty() && s.domesticates[c] != 0) {
                    out.carried.emplace_back(band.cell, s.domesticates[c]);
                }
            }
        }
    }
    if (!out.arrivals.empty()) {
        std::lock_guard<std::mutex> lock(outbox_mutex);
        outboxes.push_back(std::move(out));
    }
    }, 32);
    std::sort(outboxes.begin(), outboxes.end(), [](const Outbox& a, const Outbox& b) { return a.begin < b.begin; });
    for (Outbox& out : outboxes) {
        arrivals.insert(arrivals.end(), out.arrivals.begin(), out.arrivals.end());
        carried.insert(carried.end(), out.carried.begin(), out.carried.end());
        moved += out.moved;
    }


    // A group of `share` of a cohort's people, in its age structure, bound
    // for `to`. The cohort keeps the rest.
    auto split_off = [&](Cohort& co, CellId to, double share, RngStream& rng) {
        Cohort band;
        band.cell = to;
        band.culture = co.culture;
        band.language = co.language;
        band.traits = co.traits;
        band.dialect = co.dialect;
        band.farming = co.farming;
        band.bonded = co.bonded;
        share = std::clamp(share, 0.0, 1.0);
        for (int b = 0; b < kAgeBins; ++b) {
            const auto f = std::min<std::int64_t>(co.female[b], stochastic_round(share * co.female[b], rng));
            const auto m = std::min<std::int64_t>(co.male[b], stochastic_round(share * co.male[b], rng));
            co.female[b] -= static_cast<std::uint32_t>(f);
            co.male[b] -= static_cast<std::uint32_t>(m);
            band.female[b] = static_cast<std::uint32_t>(f);
            band.male[b] = static_cast<std::uint32_t>(m);
        }
        return band;
    };
    auto largest_cohort = [&](CellId c, bool farmers_only) -> Cohort* {
        Cohort* best = nullptr;
        for (auto i = s.cohort_begin[c]; i < s.cohort_begin[c + 1]; ++i) {
            Cohort& co = s.cohorts[i];
            if (farmers_only && co.farming < kColonizeFarming) continue;
            if (best == nullptr || co.total() > best->total()) best = &co;
        }
        return best;
    };

    // ---- Colonists: crowded farmers settle land their crops could fill.
    s.colonists = 0;
    const std::int64_t year = ctx.tick / kTicksPerYear;
    // On grids finer than level 5, colonists go as far as a level-5 cell
    // spacing; on coarser grids, less often.
    const int reach = w.reach_steps();
    const std::int64_t period = kColonizeYears * static_cast<std::int64_t>(std::max(1.0, w.cell_scale()));
    std::vector<std::uint8_t> mark(reach > 1 ? n : 0, 0);
    std::vector<std::pair<CellId, int>> around;
    for (CellId c = 0; c < n; ++c) {
        // Each cell looks for new land every kColonizeYears (staggered).
        if ((year + static_cast<std::int64_t>(c)) % period != 0) continue;
        if (s.cell_population[c] == 0 || s.env.farming.empty() || s.env.farming[c] < kColonizeFarming) continue;
        const double crowding = crowding_of(s, c);
        if (crowding < kColonizeCrowding) continue;
        const std::uint32_t known = s.domesticates.empty() ? 0 : s.domesticates[c];
        if (known == 0) continue;
        const std::uint64_t techs = techniques_in(s, c);
        CellId best = kInvalidCell;
        double best_appeal = 0.0, best_room = 0.0;
        auto consider = [&](CellId nb, double factor) {
            if (!w.is_land[nb]) return;
            const double could = farm_density(w, nb, known, techs, &s) * w.grid.area_km2(nb);
            const double room = could - static_cast<double>(s.cell_population[nb]);
            if (could <= 0.0 || room < kColonizeRoom * could) return;
            const double climb_km = std::fabs(w.elevation_m[nb] - w.elevation_m[c]) / 1000.0;
            const double appeal = factor * room / (1.0 + kClimbCostPerKm * climb_km);
            if (appeal > best_appeal || (appeal == best_appeal && nb < best)) best_appeal = appeal, best = nb, best_room = room;
        };
        if (reach == 1) {
            for (CellId nb : w.grid.neighbors(c)) consider(nb, 1.0);
        } else {
            // Leapfrogging, as at level 5: land a level-5 spacing away first,
            // nearer land only if none there has room.
            w.grid.cells_within(c, reach, around, mark);
            for (int ring = reach; ring >= 1 && best == kInvalidCell; --ring) {
                for (std::size_t i = 1; i < around.size(); ++i) {
                    if (around[i].second == ring) consider(around[i].first, 1.0);
                }
            }
        }
        if (technique_effects(w, c, techs).water_transport > 0.0 && !w.trade_edge_begin.empty()) {
            for (std::uint32_t e : w.trade_links(c)) {
                if (w.trade_edges[e].modes & kTradeSea) consider(w.trade_other(e, c), kSeaColonyAppeal);
            }
        }
        if (best == kInvalidCell) continue;
        Cohort* settlers = largest_cohort(c, true);
        if (settlers == nullptr) continue;
        const double wanted = kColonizeRate * static_cast<double>(period) * static_cast<double>(s.cell_population[c]) *
                              std::min(1.0, (crowding - kColonizeCrowding) / kColonizeRamp + 0.1);
        const double here = static_cast<double>(s.cell_population[best]);
        const double group = std::min({std::max(wanted, kColonyMinShare * here), 0.5 * best_room,
                                       kColonizeMaxShare * static_cast<double>(s.cell_population[c])});
        RngStream rng = ctx.rng(Stream::Migration, kColonizeEntity | c);
        Cohort band = split_off(*settlers, best, group / static_cast<double>(std::max<std::int64_t>(1, settlers->total())), rng);
        const std::int64_t size = band.total();
        if (size <= 0) continue;
        arrivals.push_back(band);
        moved += size;
        s.colonists += size;
        carried.emplace_back(best, known);
    }

    // ---- Folk migrations, driven by war, raids and famine; arrivals may
    // push others on.
    s.folk_movers = 0;
    const std::size_t i_warfare = [&] {
        const auto& traits = w.content->traits;
        for (std::size_t t = 0; t < traits.size(); ++t) {
            if (traits[t].id == "warfare") return t;
        }
        return traits.size();
    }();
    std::vector<CellId> in_reach;
    // The most open land within reach of `from`, other than `avoid`.
    auto refuge = [&](CellId from, CellId avoid, std::uint32_t not_ruled_by) {
        CellId best = kInvalidCell;
        double best_appeal = 0.0;
        cells_within(w, from, kExodusKm, in_reach);
        for (CellId d : in_reach) {
            if (d == from || d == avoid || !w.is_land[d]) continue;
            if (not_ruled_by != 0 && !s.polity_of.empty() && s.polity_of[d] == not_ruled_by) continue;
            const double k = s.env.capacity[d];
            if (k <= 0.0) continue;
            const double open = k - static_cast<double>(s.cell_population[d]) + 0.05 * k;
            const double appeal = open / (1.0 + w.grid.distance_km(from, d) / kExodusDistanceKm);
            if (appeal > best_appeal || (appeal == best_appeal && d < best)) best_appeal = appeal, best = d;
        }
        return best;
    };
    if (s.last_arrival.size() != n) {
        s.last_arrival.assign(n, 0);
        s.last_arrival_tick.assign(n, 0);
    }
    if (s.usual_capacity.size() != n) s.usual_capacity = s.env.capacity;
    if (s.recent_capacity.size() != n) s.recent_capacity = s.env.capacity;
    for (CellId c = 0; c < n; ++c) s.recent_capacity[c] += kRecentRate * (s.env.capacity[c] - s.recent_capacity[c]);
    auto record = [&](CellId from, const Cohort& band, MigrationCause cause, std::uint32_t pushed_by) {
        const std::int64_t people = band.total();
        s.folk_movers += people;
        if (people < kFolkRecordPeople) return std::uint32_t{0};
        s.migrations.push_back({ctx.tick, from, band.cell, people, band.language, band.culture, cause, pushed_by});
        s.events.push_back({ctx.tick, EventType::FolkMigration, from, static_cast<std::int64_t>(s.migrations.size() - 1)});
        const auto id = static_cast<std::uint32_t>(s.migrations.size());  // index + 1
        s.last_arrival[band.cell] = id;
        s.last_arrival_tick[band.cell] = ctx.tick;
        return id;
    };
    // ---- Herders follow their horde into the land it took in war.
    for (const SimState::Settling& st : s.settling) {
        if (st.polity == 0 || st.polity >= s.polities.size() || st.from >= n || st.to >= n) continue;
        const Polity& horde = s.polities[st.polity];
        if (horde.ended != kNever || s.polity_of.size() != n || s.polity_of[st.to] != st.polity) continue;
        const std::uint32_t root = s.cultures[horde.culture].root;
        Cohort* herd = nullptr;
        for (auto i = s.cohort_begin[st.from]; i < s.cohort_begin[st.from + 1]; ++i) {
            Cohort& co = s.cohorts[i];
            if (s.cultures[co.culture].root == root && co.total() > 0 && (herd == nullptr || co.total() > herd->total())) herd = &co;
        }
        if (herd == nullptr) continue;
        const double mob = i_mobility < kMaxTraits ? herd->traits[i_mobility] : 0.5;
        const double room = (kTrekRoom + kTrekRoomMobile * mob) * s.env.capacity[st.to] - static_cast<double>(s.cell_population[st.to]);
        if (room < 1.0) continue;
        RngStream rng = ctx.rng(Stream::Migration, kSettleEntity | st.to);
        Cohort band = split_off(*herd, st.to, std::min(kSettleShare * mob, room / static_cast<double>(herd->total())), rng);
        if (band.total() <= 0) continue;
        arrivals.push_back(band);
        moved += band.total();
        record(st.from, band, MigrationCause::Conquest, 0);
    }
    s.settling.clear();
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] < kFolkMinPeople) continue;
        const std::uint8_t strife = s.strife.size() == n ? s.strife[c] : 0;
        double chance = 0.0;
        MigrationCause cause = MigrationCause::Famine;
        const double usual = s.usual_capacity[c];
        if (usual > 0.0 && crowding_of(s, c) > 1.0) {
            const double dearth = s.recent_capacity[c] / usual;
            if (dearth < kDearth) chance = kExodusFamine * std::min(1.0, (kDearth - dearth) / kDearthSpan);
        }
        if (strife == 1 && kExodusFront > chance) chance = kExodusFront, cause = MigrationCause::War;
        if (strife == 2 && kExodusWar > chance) chance = kExodusWar, cause = MigrationCause::War;
        if (strife == 3 && kExodusRaid > chance) chance = kExodusRaid, cause = MigrationCause::Raid;
        if (chance <= 0.0) continue;
        Cohort* folk = largest_cohort(c, false);
        if (folk == nullptr) continue;
        // Mobile peoples take to the road more readily, and more of them go;
        // only they leave ahead of the fighting.
        const double mobile = i_mobility < kMaxTraits ? folk->traits[i_mobility] : 0.5;
        if (strife == 1 && cause == MigrationCause::War) chance *= mobile * mobile;
        chance = std::min(1.0, chance * restless(*folk, kMobileExodus));
        RngStream rng = ctx.rng(Stream::Migration, kExodusEntity | c);
        if (rng.uniform() >= chance) continue;
        // Driven out by hunger soon after newcomers came: pushed out by them.
        std::uint32_t pushed_by = 0;
        if (cause == MigrationCause::Famine && s.last_arrival[c] != 0 &&
            ctx.tick - s.last_arrival_tick[c] <= kChainYears * kTicksPerYear) {
            cause = MigrationCause::Displaced;
            pushed_by = s.last_arrival[c];
        }
        // War refugees flee the conqueror's land.
        const std::uint32_t enemy = cause == MigrationCause::War && !s.polity_of.empty() ? s.polity_of[c] : 0;
        const CellId to = refuge(c, kInvalidCell, enemy);
        if (to == kInvalidCell) continue;
        Cohort band = split_off(*folk, to, std::min(kMaxExodusShare, kExodusShare * (1.0 + kMobileExodusShare * mobile)), rng);
        if (band.total() <= 0) continue;
        arrivals.push_back(band);
        moved += band.total();
        std::uint32_t id = record(c, band, cause, pushed_by);
        if (folk->farming > 0.1 && !s.domesticates.empty()) carried.emplace_back(to, s.domesticates[c]);
        // A strong people arriving pushes the largest people there on, and
        // they may push on others in turn.
        CellId from = c, at = to;
        for (int link = 0; link < kMaxChainLinks; ++link) {
            Cohort* locals = largest_cohort(at, false);
            if (locals == nullptr || static_cast<double>(band.total()) < kDisplaceRatio * static_cast<double>(locals->total())) {
                break;
            }
            if (i_warfare < kMaxTraits && band.traits[i_warfare] < locals->traits[i_warfare]) break;
            const CellId onward = refuge(at, from, 0);
            if (onward == kInvalidCell) break;
            const double outnumbered = static_cast<double>(band.total()) / static_cast<double>(std::max<std::int64_t>(1, locals->total()));
            Cohort pushed = split_off(*locals, onward, std::min(kMaxDisplace, kDisplace * outnumbered), rng);
            if (pushed.total() <= 0) break;
            arrivals.push_back(pushed);
            moved += pushed.total();
            id = record(at, pushed, MigrationCause::Displaced, id);
            band = pushed;
            from = at;
            at = onward;
        }
    }

    // ---- Wandering peoples: tribes and confederations on the move, step by
    // step.
    if (s.polity_of.size() == n && s.polities.size() > 1) {
        const std::size_t np = s.polities.size();
        std::vector<std::vector<CellId>> held(np);
        std::vector<double> people(np, 0.0);
        for (CellId c = 0; c < n; ++c) {
            const std::uint32_t p = s.polity_of[c];
            if (p == 0) continue;
            held[p].push_back(c);
            people[p] += static_cast<double>(s.cell_population[c]);
        }
        std::vector<CellId> lands;
        std::vector<std::uint8_t> claimed(n, 0), keep(n, 0);
        auto weak_enough = [&](std::uint32_t p, CellId c) {
            const std::uint32_t q = s.polity_of[c];
            return !claimed[c] && (q == 0 || q == p || people[q] < kWanderStronger * people[p]);
        };
        auto mobility_of = [&](std::uint32_t p) {
            double m = 0.0, q = 0.0;
            for (CellId c : held[p]) {
                for (const Cohort& co : s.cohorts_in(c)) {
                    if (i_mobility < kMaxTraits) m += static_cast<double>(co.total()) * co.traits[i_mobility];
                    q += static_cast<double>(co.total());
                }
            }
            return q > 0.0 ? m / q : 0.5;
        };
        // The next step toward `goal`: the neighbour of its lead cell nearest
        // the goal (and nearer than where it stands) that it can take.
        auto next_step = [&](std::uint32_t p, CellId goal) {
            const CellId at = s.polities[p].capital;
            CellId best = kInvalidCell;
            double best_km = w.grid.distance_km(at, goal);
            for (CellId nb : w.grid.neighbors(at)) {
                if (!w.is_land[nb] || s.env.capacity[nb] <= 0.0 || !weak_enough(p, nb)) continue;
                const double km = w.grid.distance_km(nb, goal);
                if (km < best_km || (km == best_km && best != kInvalidCell && nb < best)) best_km = km, best = nb;
            }
            return best;
        };
        // Move `share` of the rulers' people from the polity's lands to as
        // much land around `step` as it held; the lands left behind are
        // free, and those taken change hands, their people pushed on as far
        // as they are outnumbered. Returns how many moved (0: nothing done).
        auto relocate = [&](std::uint32_t p, CellId step, double share, std::uint32_t record_id, RngStream& rng) {
            Polity& pol = s.polities[p];
            lands.assign(1, step);
            claimed[step] = 1;
            for (std::size_t i = 0; i < lands.size() && lands.size() < held[p].size(); ++i) {
                for (CellId nb : w.grid.neighbors(lands[i])) {
                    if (lands.size() >= held[p].size()) break;
                    if (!w.is_land[nb] || s.env.capacity[nb] <= 0.0 || !weak_enough(p, nb)) continue;
                    claimed[nb] = 1;
                    lands.push_back(nb);
                }
            }
            const std::uint32_t root = s.cultures[pol.culture].root;
            std::int64_t gone = 0;
            std::size_t next = 0;
            std::vector<std::int64_t> come(lands.size(), 0);
            // Room in each new land: up to (kTrekRoom + kTrekRoomMobile x
            // mobility) x what it feeds: a horde crowds in on the locals and
            // drives them off, where farmers on the move must find space.
            std::vector<double> room(lands.size(), 0.0);
            const double crowd = kTrekRoom + kTrekRoomMobile * mobility_of(p);
            for (std::size_t li = 0; li < lands.size(); ++li) {
                room[li] = std::max(0.0, crowd * s.env.capacity[lands[li]] -
                                             static_cast<double>(s.cell_population[lands[li]]));
            }
            for (CellId c : held[p]) {
                const std::uint32_t known = s.domesticates.empty() ? 0 : s.domesticates[c];
                for (auto i = s.cohort_begin[c]; i < s.cohort_begin[c + 1]; ++i) {
                    Cohort& co = s.cohorts[i];
                    if (co.total() <= 0) continue;
                    // The rulers' people go, the more of them the more mobile
                    // they are; other peoples of the realm who live on the
                    // move (herders under the horde) follow as mobile as they are.
                    const double mob = i_mobility < kMaxTraits ? co.traits[i_mobility] : 0.5;
                    const bool kin = s.cultures[co.culture].root == root;
                    if (!kin && mob < kFollowMobility) continue;
                    const double goes = kin ? share + (1.0 - share) * kMobileTrek * mob : share * mob;
                    // The next land with room (round robin), not where it is.
                    std::size_t slot = lands.size();
                    for (std::size_t tries = 0; tries < lands.size(); ++tries) {
                        const std::size_t k = next++ % lands.size();
                        if (lands[k] != c && room[k] - static_cast<double>(come[k]) >= 1.0) {
                            slot = k;
                            break;
                        }
                    }
                    if (slot == lands.size()) continue;  // no room anywhere: they stay
                    const double fits = (room[slot] - static_cast<double>(come[slot])) / static_cast<double>(co.total());
                    Cohort band = split_off(co, lands[slot], std::min(goes, fits), rng);
                    if (band.total() <= 0) continue;
                    gone += band.total();
                    // Deaths on the way: hunger, cold, thirst and fighting.
                    {
                        const auto biome = static_cast<Biome>(w.biome[lands[slot]]);
                        double die = kMarchDeaths;
                        if (biome == Biome::Desert) {
                            die *= kMarchHarsh;
                        } else if (biome == Biome::DryShrubland || biome == Biome::Tundra) {
                            die *= 0.5 * (1.0 + kMarchHarsh);
                        }
                        const std::uint32_t holder = s.polity_of[lands[slot]];
                        if (holder != 0 && holder != p) die *= kMarchHostile;
                        std::int64_t dead = 0;
                        for (int b = 0; b < kAgeBins; ++b) {
                            const auto df = std::min<std::int64_t>(band.female[b], stochastic_round(die * band.female[b], rng));
                            const auto dm = std::min<std::int64_t>(band.male[b], stochastic_round(die * band.male[b], rng));
                            band.female[b] -= static_cast<std::uint32_t>(df);
                            band.male[b] -= static_cast<std::uint32_t>(dm);
                            dead += df + dm;
                        }
                        s.ledger.deaths += dead;
                        s.march_deaths += dead;
                    }
                    if (band.total() <= 0) continue;
                    come[slot] += band.total();
                    arrivals.push_back(band);
                    if (known != 0) carried.emplace_back(lands[slot], known);
                }
            }
            if (gone == 0) {
                for (CellId c : lands) claimed[c] = 0;
                return std::int64_t{0};
            }
            const CellId from = pol.capital;
            for (CellId c : lands) keep[c] = 1;
            for (CellId c : held[p]) {
                if (!keep[c]) s.polity_of[c] = 0;  // the land left behind is free
            }
            for (std::size_t li = 0; li < lands.size(); ++li) {
                const CellId c = lands[li];
                keep[c] = 0;
                const std::uint32_t q = s.polity_of[c];
                if (q == p) continue;  // its own land still
                if (q != 0) s.polities[q].shock += kWanderShock;
                s.polity_of[c] = p;
                s.last_arrival[c] = record_id;
                s.last_arrival_tick[c] = ctx.tick;
                Cohort* locals = largest_cohort(c, false);
                if (locals == nullptr || s.cultures[locals->culture].root == root) continue;
                const CellId onward = refuge(c, from, p);
                if (onward == kInvalidCell) continue;
                // Overwhelmed locals flee; where the newcomers are few,
                // nearly all stay and live under them.
                const double outnumbered =
                    static_cast<double>(come[li]) / static_cast<double>(std::max<std::int64_t>(1, locals->total()));
                Cohort pushed = split_off(*locals, onward, std::min(kMaxDisplace, kDisplace * outnumbered), rng);
                if (pushed.total() <= 0) continue;
                arrivals.push_back(pushed);
                moved += pushed.total();
                record(c, pushed, MigrationCause::Displaced, record_id);
            }
            held[p] = lands;
            pol.capital = step;
            moved += gone;
            s.folk_movers += gone;
            return gone;
        };
        auto settle = [&](Polity& pol) {
            pol.trek_to = kInvalidCell;
            if (pol.trek_record != 0 && pol.trek_record <= s.migrations.size()) s.migrations[pol.trek_record - 1].to = pol.capital;
            pol.trek_record = 0;
            pol.arrived = ctx.tick;
            s.events.push_back({ctx.tick, EventType::PolityWandered, pol.capital, static_cast<std::int64_t>(pol.id)});
        };
        // Living, peopled polities, most people first.
        std::vector<std::uint32_t> by_people;
        for (std::uint32_t q = 1; q < np; ++q) {
            if (s.polities[q].ended == kNever && people[q] > 0.0) by_people.push_back(q);
        }
        std::stable_sort(by_people.begin(), by_people.end(),
                         [&](std::uint32_t a, std::uint32_t b) { return people[a] > people[b]; });
        for (std::uint32_t p = 1; p < np; ++p) {
            Polity& pol = s.polities[p];
            if (pol.ended != kNever || held[p].empty()) continue;

            // On the road: a step toward the goal, at the pace of its people.
            if (pol.trek_to != kInvalidCell) {
                const double pace_km = kTrekKm + kTrekKmMobile * mobility_of(p);
                RngStream rng = ctx.rng(Stream::Migration, kTrekEntity | p);
                if (rng.uniform() >= w.per_step(pace_km / kLevel5SpacingKm)) continue;
                const CellId step = next_step(p, pol.trek_to);
                if (step == kInvalidCell || relocate(p, step, kTrekShare, pol.trek_record, rng) == 0) {
                    settle(pol);  // blocked: they settle where they stand
                    continue;
                }
                if (pol.trek_record != 0 && pol.trek_record <= s.migrations.size()) s.migrations[pol.trek_record - 1].to = step;
                if (step == pol.trek_to) settle(pol);
                continue;
            }

            if (pol.bond == Bond::Vassal) continue;
            if (people[p] < static_cast<double>(kWanderMinPeople) || polity_has_writing(w, s, p)) continue;
            if (pol.wandered != kNever && ctx.tick - pol.wandered < kWanderYears * kTicksPerYear) continue;
            // Its people: how mobile, how fed, how crowded.
            const double mobility = mobility_of(p);
            double recent = 0.0, usual = 0.0, capacity = 0.0;
            for (CellId c : held[p]) {
                recent += s.recent_capacity[c];
                usual += s.usual_capacity[c];
                capacity += s.env.capacity[c];
            }
            double pressure = std::min(1.0, pol.shock);
            pressure = std::max(pressure, pol.pressure[static_cast<std::size_t>(Pressure::Famine)]);
            if (usual > 0.0) pressure = std::max(pressure, std::clamp((kDearth - recent / usual) / kDearthSpan, 0.0, 1.0));
            if (capacity > 0.0) pressure = std::max(pressure, std::clamp((people[p] / capacity - 1.05) / 0.3, 0.0, 1.0));
            // A stronger neighbour: the largest realm seated within reach.
            double threat = 0.0;
            for (std::uint32_t q : by_people) {  // largest first: the first in reach is it
                if (q == p || s.polities[q].ended != kNever) continue;
                if (w.grid.distance_km(pol.capital, s.polities[q].capital) <= kWanderThreatKm) {
                    threat = people[q];
                    break;
                }
            }
            pressure = std::max(pressure, std::clamp((threat / people[p] - kWanderThreat) / kWanderThreatSpan, 0.0, 1.0));
            // Pressed hard, any tribe may go; the most mobile also roam
            // unpressed, to pastures new.
            double chance = pressure >= kWanderPressure ? kWander * pressure * mobility_factor(mobility, kWanderMobility) : 0.0;
            chance += kWanderRoam * std::max(0.0, (mobility - kWanderRoamMobility) / (1.0 - kWanderRoamMobility));
            if (chance <= 0.0) continue;
            RngStream rng = ctx.rng(Stream::Migration, kWanderEntity | p);
            if (rng.uniform() >= chance) continue;
            // Where to: the most open land within reach, not held by the strong.
            const bool herders = pol.nomadic || (!s.env.pastoral.empty() && s.env.pastoral[pol.capital] >= 0.5);
            const double range_km = kWanderKm + kWanderKmMobile * mobility;
            CellId target = kInvalidCell;
            double best = 0.0;
            cells_within(w, pol.capital, range_km, in_reach);
            for (CellId d : in_reach) {
                if (!w.is_land[d] || s.polity_of[d] == p || s.env.capacity[d] <= 0.0 || !weak_enough(p, d)) continue;
                const double k = s.env.capacity[d];
                double appeal = (k - static_cast<double>(s.cell_population[d]) + 0.05 * k) /
                                (1.0 + w.grid.distance_km(pol.capital, d) / kWanderDistanceKm);
                if (herders) appeal *= pasture_quality(static_cast<Biome>(w.biome[d]));
                if (s.polity_of[d] != 0) appeal *= 0.7;  // someone to fight
                if (appeal > best || (appeal == best && d < target)) best = appeal, target = d;
            }
            if (target == kInvalidCell) continue;
            const CellId step = next_step(p, target);
            if (step == kInvalidCell) continue;
            // They set out: most of the rulers' people take the first step;
            // the rest stay in the old land, which is left free.
            const CellId from = pol.capital;
            s.migrations.push_back({ctx.tick, from, step, 0, pol.language, pol.culture, MigrationCause::Wandering, 0, p});
            const auto id = static_cast<std::uint32_t>(s.migrations.size());
            const std::int64_t gone = relocate(p, step, kWanderShare, id, rng);
            if (gone == 0) {
                s.migrations.pop_back();
                continue;  // none of the rulers' people left to go
            }
            s.migrations.back().people = gone;
            s.events.push_back({ctx.tick, EventType::FolkMigration, from, static_cast<std::int64_t>(id - 1)});
            pol.wandered = ctx.tick;
            pol.trek_to = target;
            pol.trek_record = id;
            if (step == target) settle(pol);
            // Its wars end in its flight: lost where it led a side; as an
            // ally, it leaves the war.
            for (War& war : s.wars) {
                if (war.ended != kNever) continue;
                const bool leads_a = !war.attackers.empty() && war.attackers.front() == p;
                const bool leads_d = !war.defenders.empty() && war.defenders.front() == p;
                if (leads_a || leads_d) {
                    war.ended = ctx.tick;
                    war.outcome = leads_a ? WarOutcome::DefendersWon : WarOutcome::AttackersWon;
                    s.events.push_back({ctx.tick, EventType::WarEnded, from, static_cast<std::int64_t>(war.id)});
                    continue;
                }
                std::erase(war.attackers, p);
                std::erase(war.defenders, p);
            }
        }
    }

    for (CellId c = 0; c < n; ++c) s.usual_capacity[c] += kUsualRate * (s.env.capacity[c] - s.usual_capacity[c]);

    s.normalize_cohorts(n, arrivals);
    s.ledger.migrants += moved;

    for (const auto& [to, known] : carried) {
        std::uint32_t usable = 0;
        for (std::size_t sp = 0; sp < w.content->species.size(); ++sp) {
            if (((known >> sp) & 1u) && raisable(w, s, to, sp) >= FarmingSystem::kMinSuitability) {
                usable |= 1u << sp;
            }
        }
        s.domesticates[to] |= usable;
    }

    // Small groups join the largest group in their cell: a band of a few
    // dozen arriving among many takes up their language and customs at once.
    // (Larger minorities shift gradually; see CultureSystem.)
    // Each cell on its own, in parallel.
    std::atomic<bool> merged_any{false};
    parallel_for(n, [&](std::size_t c_begin, std::size_t c_end) {
    for (std::size_t c = c_begin; c < c_end; ++c) {
        const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
        if (last - first < 2) continue;
        auto largest = first;
        for (auto i = first + 1; i < last; ++i) {
            if (s.cohorts[i].total() > s.cohorts[largest].total()) largest = i;  // ties: lowest key wins
        }
        const double threshold =
            std::max(static_cast<double>(kMinGroupSize), kMinGroupShare * static_cast<double>(s.cell_population[c]));
        for (auto i = first; i < last; ++i) {
            if (i == largest || static_cast<double>(s.cohorts[i].total()) >= threshold) continue;
            // Farmers settling among foragers keep to themselves.
            if (s.cohorts[i].farming - s.cohorts[largest].farming > kKeepApartFarming) continue;
            Cohort& big = s.cohorts[largest];
            const double wb = static_cast<double>(big.total()), ws = static_cast<double>(s.cohorts[i].total());
            if (wb + ws > 0.0) {
                const double f = ws / (wb + ws);
                for (std::size_t t = 0; t < kMaxTraits; ++t) big.traits[t] += f * (s.cohorts[i].traits[t] - big.traits[t]);
                big.farming += f * (s.cohorts[i].farming - big.farming);
                if (s.cohorts[i].bonded != 0.0 || big.bonded != 0.0) {
                    big.bonded = (wb * big.bonded + ws * s.cohorts[i].bonded) / (wb + ws);
                }
            }
            for (int b = 0; b < kAgeBins; ++b) {
                s.cohorts[largest].female[b] += s.cohorts[i].female[b];
                s.cohorts[largest].male[b] += s.cohorts[i].male[b];
                s.cohorts[i].female[b] = 0;
                s.cohorts[i].male[b] = 0;
            }
            merged_any.store(true, std::memory_order_relaxed);
        }
    }
    }, 256);
    if (merged_any.load()) s.normalize_cohorts(n);


    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] > 0 && !s.ever_settled[c]) {
            s.ever_settled[c] = 1;
            s.events.push_back({ctx.tick, EventType::CellSettled, c, s.cell_population[c]});
        }
    }
}

// ------------------------------------------------------------------ history

void HistorySystem::step(StepContext& ctx) {
    SimState& s = ctx.state;
    YearRecord r;
    r.year = ctx.tick / kTicksPerYear + 1;  // recorded at the end of the year
    r.population = s.total_population();
    r.births_total = s.ledger.births;
    r.deaths_total = s.ledger.deaths;
    r.migrants_total = s.ledger.migrants;
    for (std::uint32_t p : s.cell_population) r.inhabited_cells += p > 0 ? 1 : 0;
    r.global_anomaly_c = s.climate.global_anomaly_c + s.climate.bond_anomaly_c;
    for (double k : s.env.capacity) r.capacity += k;
    const PeopleCounts p = count_peoples(s);
    r.languages = p.languages;
    r.language_families = p.families;
    r.cultures = p.cultures;
    double farmers = 0.0;
    for (const Cohort& co : s.cohorts) farmers += static_cast<double>(co.total()) * co.farming;
    r.farmers = static_cast<std::int64_t>(farmers + 0.5);
    r.disease_deaths_total = s.disease_deaths_total;
    std::uint64_t any = 0;
    for (std::uint64_t t : s.techniques) any |= t;
    for (; any != 0; any &= any - 1) ++r.techniques;
    for (CellId c = 0; c < s.urban.size(); ++c) {
        r.townspeople += static_cast<std::int64_t>(s.urban[c] * s.cell_population[c] + 0.5);
        r.cities += s.city_of.empty() || s.city_of[c] == 0 ? 0 : 1;
    }
    // Polity sizes in one pass over the cells.
    std::vector<double> area(s.polities.size(), 0.0);
    std::vector<std::int64_t> people(s.polities.size(), 0);
    for (CellId c = 0; c < s.polity_of.size(); ++c) {
        const std::uint32_t id = s.polity_of[c];
        if (id == 0) continue;
        area[id] += ctx.world.grid.area_km2(c);
        people[id] += s.cell_population[c];
    }
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        if (s.polities[id].ended != kNever) continue;
        ++r.polities;
        if (polity_has_writing(ctx.world, s, id)) ++r.states;
        if (area[id] > r.largest_polity_km2) {
            r.largest_polity_km2 = area[id];
            r.largest_polity_people = people[id];
        }
    }
    // Spheres: each living realm's land with that of its subjects, and theirs.
    {
        std::vector<double> sphere(s.polities.size(), 0.0);
        for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
            if (s.polities[id].ended != kNever) continue;
            std::uint32_t top = id;
            for (int depth = 0; depth < 8; ++depth) {
                const std::uint32_t up = s.polities[top].overlord;
                if (up == 0 || up >= s.polities.size() || s.polities[up].ended != kNever) break;
                top = up;
            }
            sphere[top] += area[id];
        }
        for (double a : sphere) r.largest_sphere_km2 = std::max(r.largest_sphere_km2, a);
    }
    std::uint32_t ever = 0;
    for (const Domestication& d : s.domestications) ever |= 1u << d.species;
    for (; ever != 0; ever &= ever - 1) ++r.domesticated;
    for (double v : s.trade_value) r.trade_value += 0.5 * v;  // each shipment counts at both ends
    for (double f : s.food_import) r.food_traded += std::max(0.0, f);
    for (std::size_t i = 1; i < s.wars.size(); ++i) r.wars += s.wars[i].ended == kNever ? 1 : 0;
    r.war_deaths_total = s.war_deaths_total;
    for (const Alliance& a : s.alliances) r.alliances += a.ended == kNever ? 1 : 0;
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        const Polity& pol = s.polities[id];
        if (pol.ended != kNever) continue;
        r.subjects += pol.bond != Bond::None ? 1 : 0;
        r.nomads += pol.nomadic ? 1 : 0;
    }
    r.raids = s.raids;
    r.colonists = s.colonists;
    r.folk_movers = s.folk_movers;
    if (s.faith_of.size() == s.cell_population.size()) {
        std::vector<std::uint8_t> held(s.religions.size(), 0);
        for (CellId c = 0; c < s.faith_of.size(); ++c) {
            const std::uint32_t f = s.faith_of[c];
            if (f == 0) continue;
            held[f] = 1;
            if (religion_organized(ctx.world, s, f)) r.organized_faithful += s.cell_population[c];
        }
        for (std::uint8_t h : held) r.religions += h;
    }
    double enslaved = 0.0;
    for (const Cohort& co : s.cohorts) enslaved += co.bonded * static_cast<double>(co.total());
    r.enslaved = static_cast<std::int64_t>(enslaved + 0.5);
    r.captives = s.captives;
    r.sold = s.sold;
    s.history.push_back(r);
}

}  // namespace histsim
