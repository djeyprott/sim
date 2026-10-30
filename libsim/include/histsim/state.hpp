// Dynamic simulation state: everything that changes during a run and goes
// into snapshots and the state hash.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "histsim/content.hpp"
#include "histsim/types.hpp"

namespace histsim {

// ---------------------------------------------------------------- people

inline constexpr int kAgeBins = 21;  // 0-4, 5-9, ..., 95-99, 100+
inline constexpr int kYearsPerAgeBin = 5;

// Dimensions of a cohort's dialect: a point that drifts at random and is
// pulled toward the speakers it talks to. How far apart two groups' points are
// stands for how hard they find each other to understand.
inline constexpr int kDialectDims = 8;

// A group of people in one cell who share a culture and a language, with an
// age-sex pyramid, its local version of the culture's traits and its dialect.
struct Cohort {
    CellId cell = kInvalidCell;
    std::uint32_t culture = 0;   // id into SimState::cultures
    std::uint32_t language = 0;  // id into SimState::languages
    std::array<std::uint32_t, kAgeBins> female{};
    std::array<std::uint32_t, kAgeBins> male{};
    std::array<double, kMaxTraits> traits{};       // 0-1 each, as in Content::traits
    std::array<double, kDialectDims> dialect{};

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

// ---------------------------------------------------------------- languages

inline constexpr Tick kNever = -1;

// One word of a language's core vocabulary. Words with the same cognate id
// descend from the same ancestral word, so the share of cognates between
// two languages measures how closely related they are.
struct Word {
    std::vector<PhonemeId> sounds;
    std::uint64_t cognate = 0;
};

struct Language {
    std::uint32_t id = 0;
    std::uint32_t parent = 0;  // 0 = founder of a family
    std::uint32_t family = 0;  // id of the family's founding language
    Tick founded = 0;
    Tick extinct = kNever;
    std::string name;
    std::vector<Word> words;        // one per Content::concepts
    std::uint32_t syllables = 1;    // bit s set if syllable shape s is allowed
    std::uint8_t word_order = 0;    // index into Content::word_order
    std::uint8_t morphology = 0;
    std::uint8_t tone = 0;
    std::vector<std::uint16_t> sound_changes;  // rules applied since founding, in order
};

struct Culture {
    std::uint32_t id = 0;
    std::uint32_t parent = 0;  // 0 = one of the founding cultures
    std::uint32_t root = 0;    // founding culture of this lineage
    std::uint32_t language = 0;  // main language when it was founded
    Tick founded = 0;
    Tick extinct = kNever;
    std::string name;
};

// ---------------------------------------------------------------- climate

// A regional climate event imposed by an intervention: a temperature and
// rainfall shift over a disc, fading out over its outer 30%, for a number of
// years.
struct ClimateModifier {
    CellId center = kInvalidCell;
    double radius_km = 0.0;
    double temp_c = 0.0;         // added to this year's temperature
    double precip_frac = 0.0;    // rainfall multiplied by (1 + precip_frac)
    std::int64_t years_left = 0;
};

// Global and regional climate state carried from year to year.
struct ClimateState {
    double global_anomaly_c = 0.0;   // global mean temperature minus baseline
    double volcanic_forcing = 0.0;   // W/m^2, negative after eruptions, decays
    double solar_forcing = 0.0;      // W/m^2, slow random wander
    double imposed_forcing = 0.0;    // W/m^2, set by interventions, persistent
    std::vector<double> regional_temp_anomaly;    // per coarse climate cell, degrees C
    std::vector<double> regional_precip_anomaly;  // per coarse climate cell, fraction
    std::vector<ClimateModifier> modifiers;       // active regional events
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
    Intervention = 4,     // a command applied by the user; value = CommandType
    LanguageSplit = 5,    // a dialect became a language; value = new language id
    CultureSplit = 6,     // a regional culture became distinct; value = new culture id
    LanguageExtinct = 7,  // last speakers gone; value = language id
    CultureExtinct = 8,   // value = culture id
};

struct Event {
    Tick tick = 0;
    EventType type = EventType::CellSettled;
    CellId cell = kInvalidCell;
    std::int64_t value = 0;
};

// Running totals that make conservation checkable: at any tick,
// population == initial_population + births - deaths + added.
struct Ledger {
    std::int64_t initial_population = 0;
    std::int64_t births = 0;
    std::int64_t deaths = 0;    // includes deaths caused by interventions
    std::int64_t migrants = 0;  // moves between cells; does not change the total
    std::int64_t added = 0;     // people placed by interventions
};

// ---------------------------------------------------------------- commands

// Interventions ("god mode"). Every command applied to a run is kept in the
// state with the tick it was applied at, so a run with interventions still
// saves, hashes and replays exactly. Append new types; never renumber.
enum class CommandType : std::uint32_t {
    Eruption = 1,         // amount = forcing in W/m^2 (negative cools)
    ClimateForcing = 2,   // amount = persistent forcing in W/m^2 (replaces the previous value)
    RegionalClimate = 3,  // cell, radius; amount = degrees C; amount2 = rainfall change (fraction); years
    Plague = 4,           // cell, radius; amount = share of people who die (0-1)
    Settlers = 5,         // cell; amount = number of people (a new group)
    WipeOut = 6,          // cell, radius; everyone there dies
    SetGame = 7,          // cell, radius; amount = game abundance (0-1)
};

struct Command {
    Tick tick = 0;  // set when applied
    CommandType type = CommandType::Eruption;
    CellId cell = kInvalidCell;
    double radius_km = 0.0;
    double amount = 0.0;
    double amount2 = 0.0;
    std::int64_t years = 0;
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
    std::uint32_t languages = 0;          // living languages
    std::uint32_t language_families = 0;  // families with a living language
    std::uint32_t cultures = 0;           // living cultures
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
    std::vector<Command> commands;     // interventions, in the order applied

    // Every language and culture that ever existed; entry i has id i, and
    // entry 0 is an unused placeholder so that 0 can mean "none".
    std::vector<Language> languages;
    std::vector<Culture> cultures;
    std::uint64_t next_cognate = 1;

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
