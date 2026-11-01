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
    double farming = 0.0;  // share of its food from crops and herds (0 = foragers)

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

// A Bond event: one of the Holocene's abrupt cold spells (Bond et al. 1997,
// 2001), such as the 8.2 and 4.2 ka events, recurring about every 1,500
// years. `years` == 0 means none is under way.
struct BondEvent {
    std::int64_t start_year = 0;
    std::int64_t years = 0;
    double strength_c = 0.0;  // peak cooling at high northern latitudes
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
    BondEvent bond;                               // under way, if years > 0
    std::vector<BondEvent> bond_history;          // every Bond event so far, in order
    std::int64_t next_bond_year = 0;              // when the natural cycle brings the next one
    double bond_anomaly_c = 0.0;                  // its effect on this year's global mean temperature
};

// This year's conditions on every cell, written by the climate and environment
// systems and read by the people systems.
struct Environment {
    std::vector<double> temperature_c;
    std::vector<double> precipitation_mm;
    std::vector<double> game;      // wild food abundance, 0..1 of the undisturbed level
    std::vector<double> capacity;  // people the cell can feed this year, foraging and farming
    std::vector<double> farming;   // share of the cell's food from farming (population-weighted)
    std::vector<double> farm_capacity;  // people the cell could feed if everyone farmed, this year
    std::vector<double> pastoral;       // share of that farm food from herds (1 = pastoralists)
    // Extra chance of dying from disease this year, per cell and age group
    // (cell * kDiseaseAgeGroups + group), set by the disease system and
    // applied by demography.
    std::vector<double> disease_mortality;
};

// A city: the towns of a cell, once they hold enough people. Its size is the
// cell's urban share times its population.
struct City {
    std::uint32_t id = 0;
    CellId cell = kInvalidCell;
    std::string name;
    Tick founded = 0;
    Tick abandoned = kNever;
};

// How a polity is bound to an overlord.
enum class Bond : std::uint8_t {
    None = 0,
    Tributary = 1,  // sends the overlord part of its goods each year
    Vassal = 2,     // also fights in the overlord's wars and may be absorbed
};

// What weighs on a polity's stability (PolitySystem): each 0-1.
enum class Pressure : std::uint8_t {
    Famine = 0,     // share of its people beyond what the land and imports feed
    War = 1,        // recent defeats, land lost, war dead and raids
    Overreach = 2,  // provinces held loosely, far from the capital
    Poverty = 3,    // little trade passing through the realm
    Epidemic = 4,   // disease deaths beyond the usual
    Division = 5,   // people of other cultures than the rulers'
    Count
};
inline constexpr std::size_t kPressureCount = static_cast<std::size_t>(Pressure::Count);

// How a polity ended. Append; never renumber.
enum class PolityFate : std::uint8_t {
    Living = 0,
    Collapsed = 1,  // broke up; `fate_cause` says what drove it
    Conquered = 2,  // annexed or overrun in war
    Absorbed = 3,   // a vassal folded into its overlord
    Dwindled = 4,   // lost its last land to secession or emptiness
};
// fate_cause for a collapse: a Pressure, or kSuccessionCrisis.
inline constexpr std::uint8_t kSuccessionCrisis = 255;

// A polity: a chiefdom, state or empire ruling a set of cells from a capital.
struct Polity {
    std::uint32_t id = 0;
    std::string name;
    CellId capital = kInvalidCell;
    Tick founded = 0;
    Tick ended = kNever;
    std::uint32_t culture = 0;   // of the founders
    std::uint32_t language = 0;
    std::uint32_t parent = 0;    // the polity it broke away from, if any
    bool nomadic = false;        // a confederation of mounted herders
    std::uint32_t overlord = 0;  // the polity it is bound to (0 = none)
    Bond bond = Bond::None;
    Tick bond_since = 0;
    // Stability (0-1) moves each year toward a level set by its resilience
    // (hierarchy, ritual, writing, bureaucracy) less the pressures on it; a
    // fragile polity breaks up. `shock` is the recent toll of war (decays).
    double stability = 0.6;
    double shock = 0.0;
    std::array<double, kPressureCount> pressure{};  // this year's pressures
    PolityFate fate = PolityFate::Living;
    std::uint8_t fate_cause = 0;
    // Nomads who conquered a settled state rule from its capital and settle
    // down over a few generations (kNever = not settling).
    Tick settling_since = kNever;
};

// A defensive alliance: each joins a war against the other.
struct Alliance {
    std::uint32_t a = 0, b = 0;  // polity ids, a < b
    Tick formed = 0;
    Tick ended = kNever;
};

// After a war its leaders keep the peace until `until`.
struct Truce {
    std::uint32_t a = 0, b = 0;  // a < b
    Tick until = 0;
};

enum class WarGoal : std::uint8_t {
    Conquest = 0,      // take land; the loser may become a vassal
    Tribute = 1,       // make the loser a tributary (nomads against farmers)
    Independence = 2,  // a subject or rebel province fights its overlord
};
enum class WarOutcome : std::uint8_t {
    Ongoing = 0,
    AttackersWon = 1,
    DefendersWon = 2,
    Stalemate = 3,
};

// A war between two sides; the first polity on each side leads it.
struct War {
    std::uint32_t id = 0;
    std::string name;
    std::vector<std::uint32_t> attackers;
    std::vector<std::uint32_t> defenders;
    WarGoal goal = WarGoal::Conquest;
    Tick started = 0;
    Tick ended = kNever;
    WarOutcome outcome = WarOutcome::Ongoing;
    std::int64_t deaths = 0;
    std::uint32_t cells_taken = 0;  // by the attackers
    std::uint32_t cells_lost = 0;   // retaken by the defenders
};

// Where a technique was invented independently.
struct Invention {
    Tick tick = 0;
    CellId cell = kInvalidCell;
    std::uint32_t technique = 0;
    std::uint32_t culture = 0;  // main culture of the cell then
};

// Where a disease emerged (far from anywhere it already was).
struct DiseaseOrigin {
    Tick tick = 0;
    CellId cell = kInvalidCell;
    std::uint32_t disease = 0;
};

// The first time a cell domesticated a species.
struct Domestication {
    Tick tick = 0;
    CellId cell = kInvalidCell;
    std::uint32_t species = 0;
    std::uint32_t culture = 0;   // main culture of the cell then
    std::uint32_t language = 0;  // and its main language
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
    Domestication = 9,    // a species became domesticated in a cell; value = species index
    BondEvent = 10,       // a Bond cold event began; value = peak cooling in milli-degrees C
    DiseaseEmerged = 11,  // a disease appeared far from anywhere it already was; value = disease index
    Invention = 12,       // a technique was invented independently; value = technique index
    CityFounded = 13,     // a town grew into a city; value = city id
    PolityFounded = 14,   // value = polity id
    PolityEnded = 15,     // value = polity id
    WarBegan = 16,        // cell = attacker's capital; value = war id
    WarEnded = 17,        // cell = attacker's capital; value = war id
    AllianceFormed = 18,  // cell = one ally's capital; value = alliance index
    Subjugated = 19,      // a polity became a tributary or vassal; value = its id
    Absorbed = 20,        // a vassal was absorbed by its overlord; value = the vassal's id
    Raid = 21,            // nomads raided a cell; value = the raiders' polity id
    ConquestDynasty = 22, // nomads took a settled state and rule it; cell = its capital; value = the nomads' id
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
    BondEvent = 8,        // amount = peak cooling in degrees C; years = duration
    Outbreak = 9,         // cell, radius; amount = disease index
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
    std::int64_t farmers = 0;             // people, weighted by their share of food from farming
    std::int64_t disease_deaths_total = 0;  // running total of deaths from diseases
    std::uint32_t techniques = 0;           // techniques known somewhere
    std::int64_t townspeople = 0;           // people living in towns and cities
    std::uint32_t cities = 0;               // cities now
    std::uint32_t polities = 0;             // living polities
    std::uint32_t states = 0;               // of which states (with writing)
    double largest_polity_km2 = 0.0;
    std::int64_t largest_polity_people = 0;
    std::uint32_t domesticated = 0;       // species domesticated somewhere so far
    double trade_value = 0.0;             // value of goods carried between cells this year (food = 1)
    double food_traded = 0.0;             // food carried between cells this year, people fed
    std::uint32_t wars = 0;               // wars under way
    std::int64_t war_deaths_total = 0;    // running total of deaths in war and raids
    std::uint32_t alliances = 0;          // alliances in force
    std::uint32_t subjects = 0;           // tributaries and vassals
    std::uint32_t nomads = 0;             // nomad confederations
    std::uint32_t raids = 0;              // nomad raids this year
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

    // Farming. Per cell: the domesticated species its people know (bit s =
    // species s). Per wild stand (World::wild_stands): how far people there
    // have come toward domesticating it (1 = domesticated).
    std::vector<std::uint32_t> domesticates;
    std::vector<double> domestication;
    std::vector<Domestication> domestications;  // in the order they happened

    // Disease, per cell x disease (cell * disease count + d): whether it is
    // present, and for epidemics the share of people who can still catch it.
    std::vector<std::uint8_t> disease_present;
    std::vector<double> susceptible;
    std::vector<std::uint32_t> disease_deaths;  // per cell, this year
    std::int64_t disease_deaths_total = 0;
    std::vector<DiseaseOrigin> disease_origins;

    // Technology: per cell, the techniques its people know (bit t =
    // Content::techniques[t]); and where each was invented independently.
    std::vector<std::uint64_t> techniques;
    std::vector<Invention> inventions;

    // Towns and polities. Per cell: the share of people living in towns, the
    // city there (0 = none) and the polity ruling it (0 = none). Entry 0 of
    // `cities` and `polities` is a placeholder so 0 can mean "none".
    std::vector<double> urban;
    std::vector<std::uint32_t> city_of;
    std::vector<City> cities;
    std::vector<std::uint32_t> polity_of;
    std::vector<Polity> polities;

    // Goods and trade. Per cell x good (cell * goods + g): the stock held and
    // this year's market price. Per cell: the goods on its market (bit g),
    // food brought in this year (people fed; negative = sent out) and the
    // value of the goods it sent and received. Per trade link
    // (World::trade_edges): the value carried a year, smoothed over about
    // ten years.
    std::vector<double> stocks;
    std::vector<double> prices;
    std::vector<std::uint32_t> goods_available;
    std::vector<double> food_import;
    std::vector<double> trade_value;
    std::vector<double> route_value;

    // War and diplomacy. Entry 0 of `wars` is a placeholder (0 = none).
    // Per cell this year: deaths in war and raids, and strife (0 none,
    // 1 fighting, 2 taken in war, 3 raided).
    std::vector<War> wars;
    std::vector<Alliance> alliances;
    std::vector<Truce> truces;
    std::vector<std::uint32_t> war_deaths;
    std::vector<std::uint8_t> strife;
    std::int64_t war_deaths_total = 0;
    std::uint32_t raids = 0;  // this year
    std::vector<Tick> last_raid;  // per cell: when nomads last raided it (kNever = never)

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
