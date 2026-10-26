#include "histsim/systems/people.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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

double crowding_of(const SimState& s, CellId c) {
    const double k = s.env.capacity[c];
    if (k <= 0.0) return s.cell_population[c] > 0 ? 50.0 : 0.0;
    return static_cast<double>(s.cell_population[c]) / k;
}

}  // namespace

double forager_death_probability(int b) { return vital::kForagerDeathProb[b]; }
double forager_fertility(int b) { return vital::kForagerFertility[b]; }
double stable_age_share(int b) { return vital::kStableAgeShare[b]; }
double female_share_of_births() { return vital::kFemaleShareOfBirths; }
double replacement_fertility_multiplier() { return vital::kReplacementFertilityMultiplier; }

Cohort make_stable_cohort(CellId cell, std::uint32_t group, std::uint32_t n) {
    Cohort c;
    c.cell = cell;
    c.culture = group;
    c.language = group;
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
    std::int64_t births_total = 0, deaths_total = 0;

    for (std::size_t i = 0; i < s.cohorts.size(); ++i) {
        Cohort& co = s.cohorts[i];
        const double crowding = crowding_of(s, co.cell);
        const double fert_mult = std::max(
            kMinFertilityMultiplier, 1.0 - (1.0 - vital::kReplacementFertilityMultiplier) * crowding);
        const double mort_mult =
            crowding > 1.0 ? std::min(kMaxMortalityMultiplier, 1.0 + kFamineMortalityPerCrowding * (crowding - 1.0))
                           : 1.0;
        RngStream rng = ctx.rng(Stream::Demography, static_cast<std::uint32_t>(i));

        // Deaths.
        for (int b = 0; b < kAgeBins; ++b) {
            const double q = std::min(1.0, vital::kForagerDeathProb[b] * mort_mult);
            const auto df = std::min<std::int64_t>(co.female[b], stochastic_round(q * co.female[b], rng));
            const auto dm = std::min<std::int64_t>(co.male[b], stochastic_round(q * co.male[b], rng));
            co.female[b] -= static_cast<std::uint32_t>(df);
            co.male[b] -= static_cast<std::uint32_t>(dm);
            deaths_total += df + dm;
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
    std::int64_t moved = 0;
    std::array<double, 8> weight{};
    std::array<CellId, 8> dest{};

    for (CellId c = 0; c < n; ++c) {
        const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
        if (first == last) continue;

        const double crowding = crowding_of(s, c);
        const bool starving = s.env.capacity[c] <= 0.0;
        const double rate =
            starving ? 1.0 : std::min(kMaxRate, kExploratoryRate + kRateAtCapacity * crowding * crowding);

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

        for (auto i = first; i < last; ++i) {
            Cohort& co = s.cohorts[i];
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
            std::int64_t band_size = 0;
            for (int b = 0; b < kAgeBins; ++b) {
                const double share = std::min(1.0, rate * kMigrationByAge[b]);
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
            }
        }
    }

    s.cohorts.insert(s.cohorts.end(), arrivals.begin(), arrivals.end());
    s.normalize_cohorts(n);
    s.ledger.migrants += moved;

    // Small groups join the largest group in their cell (a stand-in for
    // assimilation until M2 models culture and language change).
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
    r.global_anomaly_c = s.climate.global_anomaly_c;
    for (double k : s.env.capacity) r.capacity += k;
    s.history.push_back(r);
}

}  // namespace histsim
