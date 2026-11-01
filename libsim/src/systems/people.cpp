#include "histsim/systems/people.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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
    std::int64_t births_total = 0, deaths_total = 0;

    // Births replace deaths at capacity under each cell's own death rates.
    CellId cached = kInvalidCell;
    double replacement = vital::kReplacementFertilityMultiplier;
    for (std::size_t i = 0; i < s.cohorts.size(); ++i) {
        Cohort& co = s.cohorts[i];
        const double crowding = crowding_of(s, co.cell);
        if (co.cell != cached) {
            cached = co.cell;
            replacement = cell_replacement_fertility(s, co.cell);
        }
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
        const double mort_mult = std::min(kMaxMortalityMultiplier, 1.0 + famine) * (1.0 + kUrbanMortality * urban);
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
            s.disease_deaths_total += d;
        }

        // Births, to the women who survived the year.
        double expected = 0.0;
        for (int b = 0; b < kAgeBins; ++b) expected += co.female[b] * vital::kForagerFertility[b];
        const std::int64_t births = stochastic_round(expected * fert_mult, rng);
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

    s.ledger.births += births_total;
    s.ledger.deaths += deaths_total;
    s.normalize_cohorts(n);

    const std::int64_t after = before + births_total - deaths_total;
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
    std::int64_t moved = 0;
    std::array<double, 8> weight{};
    std::array<CellId, 8> dest{};

    for (CellId c = 0; c < n; ++c) {
        const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
        if (first == last) continue;

        const double crowding = crowding_of(s, c);
        const bool starving = s.env.capacity[c] <= 0.0;
        // Rates are for level-5 cells; smaller cells trade people faster, for
        // the same spread in km.
        const double rate =
            starving ? 1.0
                     : std::min(kMaxGridRate,
                                w.per_step_diffusion(std::min(kMaxRate, kExploratoryRate + kRateAtCapacity * crowding * crowding)));

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
            std::int64_t band_size = 0;
            for (int b = 0; b < kAgeBins; ++b) {
                const double share = std::min(1.0, rate * kMigrationByAge[b] * (1.0 - kFarmerStay * settled));
                const auto mf = std::min<std::int64_t>(co.female[b], stochastic_round(share * co.female[b], rng));
                const auto mm = std::min<std::int64_t>(co.male[b], stochastic_round(share * co.male[b], rng));
                co.female[b] -= static_cast<std::uint32_t>(mf);
                co.male[b] -= static_cast<std::uint32_t>(mm);
                band.female[b] = static_cast<std::uint32_t>(mf);
                band.male[b] = static_cast<std::uint32_t>(mm);
                band_size += mf + mm;
            }
            if (band_size > 0) {
                arrivals.push_back(band);
                moved += band_size;
                // Farming migrants bring their crops and herds, if they come
                // in numbers enough to farm rather than to be absorbed (a
                // few newcomers marry in and take up the local ways).
                const double there = static_cast<double>(s.cell_population[band.cell]);
                const bool farm_on = static_cast<double>(band_size) >= kMinGroupShare * there && band_size >= kMinGroupSize;
                if (farm_on && co.farming > 0.1 && !s.domesticates.empty() && s.domesticates[c] != 0) {
                    carried.emplace_back(band.cell, s.domesticates[c]);
                }
            }
        }
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
    // On grids finer than level 5, colonists look as far as a level-5 cell
    // spacing (nearer land preferred); on coarser grids, less often.
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
            const double could = farm_density(w, nb, known, techs) * w.grid.area_km2(nb);
            const double room = could - static_cast<double>(s.cell_population[nb]);
            if (could <= 0.0 || room < kColonizeRoom * could) return;
            const double climb_km = std::fabs(w.elevation_m[nb] - w.elevation_m[c]) / 1000.0;
            const double appeal = factor * room / (1.0 + kClimbCostPerKm * climb_km);
            if (appeal > best_appeal || (appeal == best_appeal && nb < best)) best_appeal = appeal, best = nb, best_room = room;
        };
        if (reach == 1) {
            for (CellId nb : w.grid.neighbors(c)) consider(nb, 1.0);
        } else {
            w.grid.cells_within(c, reach, around, mark);
            for (std::size_t i = 1; i < around.size(); ++i) {
                consider(around[i].first, 1.0 / (1.0 + static_cast<double>(around[i].second - 1) / reach));
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
        if (strife == 2 && kExodusWar > chance) chance = kExodusWar, cause = MigrationCause::War;
        if (strife == 3 && kExodusRaid > chance) chance = kExodusRaid, cause = MigrationCause::Raid;
        if (chance <= 0.0) continue;
        RngStream rng = ctx.rng(Stream::Migration, kExodusEntity | c);
        if (rng.uniform() >= chance) continue;
        Cohort* folk = largest_cohort(c, false);
        if (folk == nullptr) continue;
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
        Cohort band = split_off(*folk, to, kExodusShare, rng);
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
            Cohort pushed = split_off(*locals, onward, kDisplace, rng);
            if (pushed.total() <= 0) break;
            arrivals.push_back(pushed);
            moved += pushed.total();
            id = record(at, pushed, MigrationCause::Displaced, id);
            band = pushed;
            from = at;
            at = onward;
        }
    }

    for (CellId c = 0; c < n; ++c) s.usual_capacity[c] += kUsualRate * (s.env.capacity[c] - s.usual_capacity[c]);

    s.cohorts.insert(s.cohorts.end(), arrivals.begin(), arrivals.end());
    s.normalize_cohorts(n);
    s.ledger.migrants += moved;
    for (const auto& [to, known] : carried) {
        std::uint32_t usable = 0;
        for (std::size_t sp = 0; sp < w.content->species.size(); ++sp) {
            if (((known >> sp) & 1u) && w.cultivated(to, sp) >= FarmingSystem::kMinSuitability) {
                usable |= 1u << sp;
            }
        }
        s.domesticates[to] |= usable;
    }

    // Small groups join the largest group in their cell: a band of a few
    // dozen arriving among many takes up their language and customs at once.
    // (Larger minorities shift gradually; see CultureSystem.)
    bool merged_any = false;
    for (CellId c = 0; c < n; ++c) {
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
            }
            for (int b = 0; b < kAgeBins; ++b) {
                s.cohorts[largest].female[b] += s.cohorts[i].female[b];
                s.cohorts[largest].male[b] += s.cohorts[i].male[b];
                s.cohorts[i].female[b] = 0;
                s.cohorts[i].male[b] = 0;
            }
            merged_any = true;
        }
    }
    if (merged_any) s.normalize_cohorts(n);

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
    s.history.push_back(r);
}

}  // namespace histsim
