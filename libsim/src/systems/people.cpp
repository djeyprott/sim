#include "histsim/systems/people.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "histsim/systems/culture.hpp"
#include "histsim/systems/farming.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/systems/technology.hpp"
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

    for (std::size_t i = 0; i < s.cohorts.size(); ++i) {
        Cohort& co = s.cohorts[i];
        const double crowding = crowding_of(s, co.cell);
        const double fert_mult = std::max(
            kMinFertilityMultiplier, 1.0 - (1.0 - vital::kReplacementFertilityMultiplier) * crowding) *
            (1.0 + kFarmerExtraFertility * co.farming);
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
            band.traits = co.traits;
            band.dialect = co.dialect;
            band.farming = co.farming;
            std::int64_t band_size = 0;
            for (int b = 0; b < kAgeBins; ++b) {
                const double share = std::min(1.0, rate * kMigrationByAge[b] * (1.0 - kFarmerStay * co.farming));
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
                // Farming migrants bring their crops and herds.
                if (co.farming > 0.1 && !s.domesticates.empty() && s.domesticates[c] != 0) {
                    carried.emplace_back(band.cell, s.domesticates[c]);
                }
            }
        }
    }

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
    s.history.push_back(r);
}

}  // namespace histsim
