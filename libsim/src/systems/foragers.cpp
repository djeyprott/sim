#include "histsim/systems/foragers.hpp"

#include <algorithm>
#include <array>

namespace histsim {

namespace {

// Largest power of ten <= n (0 for n < 1). Integer-only.
std::int64_t power_of_ten_floor(std::int64_t n) {
    if (n < 1) {
        return 0;
    }
    std::int64_t p = 1;
    while (p <= n / 10) {
        p *= 10;
    }
    return p;
}

}  // namespace

void ForagerDemography::step(StepContext& ctx) {
    const World& w = ctx.world;
    SimState& s = ctx.state;
    const auto before = s.population.read();
    auto after = s.population.write();

    const std::int64_t total_before = s.total_population();
    std::int64_t births_total = 0, deaths_total = 0;

    for (CellId c = 0; c < w.cell_count(); ++c) {
        const std::int64_t n = before[c];
        if (n == 0) {
            after[c] = 0;
            continue;
        }
        RngStream rng = ctx.rng(Stream::Demography, c);
        const double k = w.forager_capacity[c];
        const double crowding = k > 0.0 ? static_cast<double>(n) / k : 1.0e6;
        const double death_rate = kBaseDeathRate + (kBirthRate - kBaseDeathRate) * crowding;

        const std::int64_t births = stochastic_round(kBirthRate * static_cast<double>(n), rng);
        const std::int64_t deaths =
            std::min(n + births, stochastic_round(death_rate * static_cast<double>(n), rng));
        after[c] = static_cast<std::uint32_t>(n + births - deaths);
        births_total += births;
        deaths_total += deaths;
    }
    s.population.commit();
    s.ledger.births += births_total;
    s.ledger.deaths += deaths_total;

    const std::int64_t total_after = total_before + births_total - deaths_total;
    if (power_of_ten_floor(total_after) != power_of_ten_floor(total_before)) {
        s.events.push_back({ctx.tick, EventType::WorldPopulation, kInvalidCell, total_after});
    }
}

void ForagerMigration::step(StepContext& ctx) {
    const World& w = ctx.world;
    SimState& s = ctx.state;
    const auto before = s.population.read();
    s.population.begin_from_current();
    auto after = s.population.write();

    std::int64_t moved = 0;
    std::array<double, 8> weight{};
    std::array<CellId, 8> dest{};

    for (CellId c = 0; c < w.cell_count(); ++c) {
        const std::int64_t n = before[c];
        if (n == 0) {
            continue;
        }
        const double k = w.forager_capacity[c];
        const double pressure = k > 0.0 ? std::min(1.0, static_cast<double>(n) / k) : 1.0;

        // Destinations: land neighbors, weighted by spare capacity plus a small
        // share of total capacity so even full neighbors take some people.
        std::size_t count = 0;
        double weight_sum = 0.0;
        for (CellId nb : w.grid.neighbors(c)) {
            const double knb = w.forager_capacity[nb];
            if (knb <= 0.0) {
                continue;
            }
            const double spare = std::max(0.0, knb - static_cast<double>(before[nb]));
            weight[count] = spare + 0.05 * knb;
            dest[count] = nb;
            weight_sum += weight[count];
            ++count;
        }
        if (count == 0) {
            continue;
        }

        RngStream rng = ctx.rng(Stream::Migration, c);
        std::int64_t leaving = std::min(n, stochastic_round(kMaxEmigrationRate * pressure * static_cast<double>(n), rng));
        if (k <= 0.0) {
            leaving = n;  // nothing to live on here
        }
        if (leaving == 0) {
            continue;
        }

        // Split `leaving` across destinations so the parts sum exactly.
        std::int64_t remaining = leaving;
        double remaining_weight = weight_sum;
        for (std::size_t i = 0; i < count; ++i) {
            std::int64_t part;
            if (i + 1 == count) {
                part = remaining;
            } else {
                part = std::min(remaining, stochastic_round(static_cast<double>(remaining) * weight[i] / remaining_weight, rng));
            }
            after[dest[i]] += static_cast<std::uint32_t>(part);
            remaining -= part;
            remaining_weight -= weight[i];
        }
        after[c] -= static_cast<std::uint32_t>(leaving);
        moved += leaving;
    }
    s.population.commit();
    s.ledger.migrants += moved;

    const auto now = s.population.read();
    for (CellId c = 0; c < w.cell_count(); ++c) {
        if (now[c] > 0 && !s.ever_settled[c]) {
            s.ever_settled[c] = 1;
            s.events.push_back({ctx.tick, EventType::CellSettled, c, now[c]});
        }
    }
}

}  // namespace histsim
