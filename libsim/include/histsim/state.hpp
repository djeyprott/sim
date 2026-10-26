// Dynamic simulation state: everything that changes during a run and goes
// into snapshots and the state hash.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "histsim/types.hpp"

namespace histsim {

// ---------------------------------------------------------------- people

inline constexpr int kAgeBins = 21;  // 0-4, 5-9, ..., 95-99, 100+
inline constexpr int kYearsPerAgeBin = 5;

// A group of people in one cell who share a culture and a language, with an
// age-sex pyramid. In M1 culture and language are the same starting group id;
// M2 lets them drift apart.
struct Cohort {
    CellId cell = kInvalidCell;
    std::uint32_t culture = 0;
    std::uint32_t language = 0;
    std::array<std::uint32_t, kAgeBins> female{};
    std::array<std::uint32_t, kAgeBins> male{};

    [[nodiscard]] std::int64_t total() const noexcept {
        std::int64_t t = 0;
        for (int b = 0; b < kAgeBins; ++b) t += female[b] + male[b];
        return t;
    }
    [[nodiscard]] bool same_key(const Cohort& o) const noexcept {
        return cell == o.cell && culture == o.culture && language == o.language;
    }
    [[nodiscard]] bool key_less(const Cohort& o) const noexcept {
        if (cell != o.cell) return cell < o.cell;
        if (culture != o.culture) return culture < o.culture;
        return language < o.language;
    }
};

// ---------------------------------------------------------------- climate

// Global and regional climate state carried from year to year.
struct ClimateState {
    double global_anomaly_c = 0.0;   // global mean temperature minus baseline
    double volcanic_forcing = 0.0;   // W/m^2, negative after eruptions, decays
    double solar_forcing = 0.0;      // W/m^2, slow random wander
    std::vector<double> regional_temp_anomaly;    // per coarse climate cell, degrees C
    std::vector<double> regional_precip_anomaly;  // per coarse climate cell, fraction
};

// This year's conditions on every cell, written by the climate and environment
// systems and read by the people systems.
struct Environment {
    std::vector<double> temperature_c;
    std::vector<double> precipitation_mm;
    std::vector<double> game;      // wild food abundance, 0..1 of the undisturbed level
    std::vector<double> capacity;  // people the cell can feed by foraging this year
};

// ---------------------------------------------------------------- records

// Append new values; never renumber existing ones (saved logs use them).
enum class EventType : std::uint32_t {
    CellSettled = 1,      // people reach a cell that was never inhabited; value = arrivals
    WorldPopulation = 2,  // world population crossed a power of ten; value = new total
    Eruption = 3,         // large volcanic eruption; value = forcing in mW/m^2 (negative)
};

struct Event {
    Tick tick = 0;
    EventType type = EventType::CellSettled;
    CellId cell = kInvalidCell;
    std::int64_t value = 0;
};

// Running totals that make conservation checkable: at any tick,
// population == initial_population + births - deaths.
struct Ledger {
    std::int64_t initial_population = 0;
    std::int64_t births = 0;
    std::int64_t deaths = 0;
    std::int64_t migrants = 0;  // moves between cells; does not change the total
};

// One row per simulated year, for charts and analysis. Births, deaths and
// migrants are running totals since the start (difference two rows for a
// year's count).
struct YearRecord {
    std::int64_t year = 0;  // years since the run began, at the end of that year
    std::int64_t population = 0;
    std::int64_t births_total = 0;
    std::int64_t deaths_total = 0;
    std::int64_t migrants_total = 0;
    std::uint32_t inhabited_cells = 0;
    double global_anomaly_c = 0.0;
    double capacity = 0.0;  // total foraging capacity this year
};

// ---------------------------------------------------------------- state

struct SimState {
    Tick tick = 0;

    // Sorted by (cell, culture, language); no two share a key; none is empty.
    std::vector<Cohort> cohorts;
    std::vector<std::uint8_t> ever_settled;  // 1 once a cell has had people

    ClimateState climate;
    Environment env;

    Ledger ledger;
    std::vector<Event> events;         // append-only, in tick order
    std::vector<YearRecord> history;   // one per completed year

    // ---- Derived data: rebuilt by refresh(), never saved or hashed.
    std::vector<std::uint32_t> cell_population;
    std::vector<std::uint32_t> cohort_begin;  // cohorts of cell c: [cohort_begin[c], cohort_begin[c+1])

    // Sort and merge cohorts, drop empty ones, rebuild derived data.
    void normalize_cohorts(std::size_t cell_count);
    void refresh(std::size_t cell_count);

    [[nodiscard]] std::span<const Cohort> cohorts_in(CellId c) const noexcept {
        return {cohorts.data() + cohort_begin[c], cohorts.data() + cohort_begin[c + 1]};
    }
    [[nodiscard]] std::int64_t total_population() const noexcept;
    [[nodiscard]] std::uint64_t hash() const;
};

}  // namespace histsim
