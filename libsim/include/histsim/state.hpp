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
    // Share of its people held in bondage (enslaved, 0-1). Groups split off
    // carry the same share (the enslaved go with their households); merged
    // groups average it by headcount.
    double bonded = 0.0;

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

// A religion: a people's folk beliefs, a state's temple cult, or a
// universal faith; sects split from their parents.
struct Religion {
    std::uint32_t id = 0;
    std::uint32_t parent = 0;   // the religion it grew from or split from (0 = none)
    std::uint8_t kind = 0;      // index into Content::religion_kinds
    std::uint32_t culture_root = 0;  // for an ethnic (folk) religion: its people
    std::string name;
    Tick founded = 0;
    Tick extinct = kNever;
    CellId origin = kInvalidCell;
    std::uint32_t polity = 0;   // the polity it arose in, if any
    std::uint32_t founder = 0;  // the figure who taught it (missionary faiths), 0 if none
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
    // Food stored in the cell, in person-years: granaries, pits, jars and
    // herds kept for the lean year (EconomySystem: food stores).
    std::vector<double> food_store;
    // The climate people are used to (ClimateSystem): each cell's running
    // mean over about 30 years of its warmth (C above the world's own) and
    // rainfall (x the world's own).
    std::vector<double> normal_shift_c;
    std::vector<double> normal_rain;
    // What crops and herds the land suits under that climate (FarmingSystem),
    // recomputed when it moves a step (kCropShiftStepC, kCropRainStep): the
    // climate they were computed for; per cell x species (0-255) rain-fed and
    // on the floodplain; and the months that grow crops and that are warm.
    std::vector<double> crop_shift_c;
    std::vector<double> crop_rain;
    std::vector<std::uint8_t> crop_suit;
    std::vector<std::uint8_t> crop_suit_watered;
    std::vector<std::uint8_t> growing_months;
    std::vector<std::uint8_t> warm_months;
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
    std::uint32_t pretender_to = 0;  // the realm whose throne it claims (a claimant's party in a succession war)
    bool nomadic = false;        // a confederation of mounted herders
    std::uint32_t overlord = 0;  // the polity it is bound to (0 = none)
    Bond bond = Bond::None;
    Tick bond_since = 0;
    // Stability (0-1) moves each year toward a level set by its resilience
    // (hierarchy, ritual, writing, bureaucracy) less the pressures on it; a
    // fragile polity breaks up. `shock` is the recent toll of war (decays).
    double stability = 0.6;
    double shock = 0.0;
    double infamy = 0.0;  // alarm its recent conquests cause its neighbours (diplomacy.hpp)
    std::array<double, kPressureCount> pressure{};  // this year's pressures
    PolityFate fate = PolityFate::Living;
    std::uint8_t fate_cause = 0;
    // Nomads who conquered a settled state rule from its capital and settle
    // down over a few generations (kNever = not settling).
    Tick settling_since = kNever;
    // Value (at base prices) of the loot and tribute it has taken in recent
    // years, fading by WarSystem::kPlunderFade a year: what a steppe ruler
    // hands out to keep his followers.
    double plunder = 0.0;
    std::uint32_t religion = 0;  // the rulers' faith (that of the capital)
    Tick wandered = kNever;      // when it last set out for new land as a whole people
    Tick arrived = kNever;       // when its last trek ended (it settled)
    std::uint32_t ruler = 0;        // current ruler (0 = none yet)
    std::uint8_t institutions = 0;  // bits of Institution
    bool contested = false;         // a disputed succession this year threatens to break it up
    // Its laws (systems/polity.hpp Laws): for each of tenure, taxes, army and
    // succession, 0 (most central) to 2. New realms start at {1, 1, 1, 0}.
    std::array<std::uint8_t, 4> laws{1, 1, 1, 0};
    bool divide = false;            // a partible inheritance: the realm is shared out next year
    CellId trek_to = kInvalidCell;  // where it is going, while on the move
    std::uint32_t trek_record = 0;  // its movement in SimState::migrations (index + 1)
    std::uint32_t founder = 0;      // a figure who founds its first house (a governor who broke away), 0 if none
    double unrest = 0.0;            // 0-1: of the people of the crown's own land (states)
    std::uint32_t rebel = 0;        // a figure who takes the throne next: a rebel leader who overthrew the dynasty, or a general's coup
    std::uint32_t commander = 0;    // its general (index into SimState::commanders), 0 if none
    std::uint32_t priest = 0;       // its high priest (a figure), 0 if none
};

// A general's command of a polity's armies (PolitySystem appoints, WarSystem
// counts the victories and defeats). Fame rises with victories and falls
// with defeats; a famous, ambitious general may seize the throne.
struct Commander {
    std::uint32_t figure = 0;
    std::uint32_t polity = 0;
    Tick from = 0;
    Tick to = kNever;
    std::uint32_t victories = 0, defeats = 0;
    double fame = 0.0;  // 0 and up
    bool seized = false;  // ended by seizing the throne
};

// A province of a large polity: the land nearest one of its towns, held for
// the ruler by a governor whose loyalty rises and falls with the realm's
// fortunes (PolitySystem). The land nearest the capital is the crown's own
// (province 0). A disloyal province breaks away with its governor; when a
// realm collapses, its disloyal provinces go their own ways.
struct Province {
    std::uint32_t id = 0;
    std::uint32_t polity = 0;
    CellId seat = kInvalidCell;
    std::string name;
    std::uint32_t governor = 0;  // a figure (SimState::figures)
    Tick governor_since = 0;     // when the governor took office
    double loyalty = 0.7;        // 0-1
    double held_back = 0.0;      // 0-1: share of its grain tax and soldiers the governor keeps for himself
    double unrest = 0.0;         // 0-1: of its people
    Tick founded = 0;
    Tick ended = kNever;
    std::uint32_t became = 0;    // the polity it became, if it broke away
};

// A rising of the people of a province (or of the crown's land) against
// their rulers (PolitySystem).
enum class UprisingOutcome : std::uint8_t {
    Crushed = 0,
    BrokeAway = 1,   // the province became a polity of its own under the leader
    Overthrew = 2,   // the dynasty fell; the leader took the throne
};
struct Uprising {
    Tick tick = 0;
    std::uint32_t polity = 0;
    std::uint32_t province = 0;  // 0: the crown's own land
    CellId cell = kInvalidCell;  // where it began (the seat)
    std::uint32_t leader = 0;    // a figure
    UprisingOutcome outcome = UprisingOutcome::Crushed;
    std::int64_t rebels = 0;
    std::int64_t dead = 0;
    std::uint32_t became = 0;    // the polity it founded, if it broke away
};

// Institutions a polity can build (bits of Polity::institutions). Append;
// never renumber.
enum class Institution : std::uint8_t {
    Temple = 0,   // a temple priesthood: legitimacy, steadier rule
    Council = 1,  // a council of notables: orderly successions
    LawCode = 2,  // written law: steadier rule, peoples less divided
    Army = 3,     // a standing army: stronger in war, a firmer hold on provinces
    Count
};
inline constexpr std::size_t kInstitutionCount = static_cast<std::size_t>(Institution::Count);
[[nodiscard]] inline bool has_institution(std::uint8_t set, Institution i) noexcept {
    return (set >> static_cast<int>(i)) & 1u;
}

// A ruler of a polity, and the house he or she belongs to.
// ---------------------------------------------------------------- figures

// Historical figures: the few people followed by name (rulers and their
// families, rival claimants, usurpers, inventors, prophets), beside the
// many counted in cohorts. A figure's life is drawn when it is recorded
// (birth and death; a ruler's death comes from RulerSystem instead), so
// nothing needs updating year by year. Figures draw from their own random
// stream: recording them changes nothing else in the run.
enum FigureRole : std::uint16_t {
    kRoleRuler = 1,
    kRoleConsort = 2,     // married to a ruler
    kRoleRoyalKin = 4,    // child of a ruler
    kRoleClaimant = 8,    // claimed a throne in a disputed succession
    kRoleUsurper = 16,    // seized a throne from another house
    kRoleInventor = 32,
    kRoleProphet = 64,    // taught a missionary faith
    kRoleFounder = 128,   // founded a realm and a house
    kRoleGovernor = 256,  // governed a province
    kRoleRebel = 512,     // led a peasant uprising
    kRoleGeneral = 1024,  // commanded a polity's armies
    kRolePriest = 2048,   // high priest of a realm's faith
};

// What a figure most wants: set by its traits and roles.
enum class FigureGoal : std::uint8_t {
    None = 0,
    Conquest = 1,
    Prosperity = 2,  // a well-governed, rich realm
    Faith = 3,
    Power = 4,       // a throne, or more of one
    Knowledge = 5,
    Family = 6,      // the house's future
};

struct Figure {
    std::uint32_t id = 0;
    std::string name;
    bool female = false;
    Tick born = 0;
    Tick died = kNever;  // may lie in the future (the year it will die); kNever: a reigning ruler, whose death RulerSystem decides
    std::uint32_t father = 0, mother = 0, spouse = 0;  // figure ids (0: not recorded)
    std::uint32_t dynasty = 0;  // house, if royal
    std::uint32_t polity = 0;   // realm it belonged to
    std::uint32_t culture = 0, language = 0;
    CellId home = kInvalidCell;
    std::uint32_t ruler = 0;  // its reign (SimState::rulers), if it ruled
    std::uint16_t roles = 0;  // FigureRole bits
    FigureGoal goal = FigureGoal::None;
    double ability = 0.5, martial = 0.5, piety = 0.5, ambition = 0.5;  // 0-1
};

enum class ReignEnd : std::uint8_t {
    Reigning = 0,
    Died = 1,       // died on the throne
    Usurped = 2,    // the throne was seized by another house
    RealmFell = 3,  // the realm ended
};
struct Ruler {
    std::uint32_t id = 0;
    std::uint32_t polity = 0;
    std::uint32_t dynasty = 0;
    std::string name;
    std::uint32_t number = 1;  // regnal number among same-named rulers of the house (II, III...)
    Tick born = 0;
    Tick reign_start = 0;
    Tick reign_end = kNever;
    ReignEnd end = ReignEnd::Reigning;
    double ability = 0.5;  // 0-1: how well the realm is governed
    double martial = 0.5;  // 0-1: how readily it goes to war
    std::uint32_t figure = 0;  // the person (SimState::figures)
};
struct Dynasty {
    std::uint32_t id = 0;
    std::string name;
    std::uint32_t polity = 0;   // where it first ruled
    std::uint32_t founder = 0;  // ruler id
    Tick founded = 0;
    Tick ended = kNever;
};

// A defensive alliance: each joins a war against the other.
struct Alliance {
    std::uint32_t a = 0, b = 0;  // polity ids, a < b
    Tick formed = 0;
    Tick ended = kNever;
};

// How two realms regard each other (diplomacy.hpp): kept for pairs that
// border, are bound or allied, or fought within living memory.
struct Relation {
    std::uint32_t a = 0, b = 0;  // polity ids, a < b
    double opinion = 0.0;        // -1 hostile .. 1 friendly
    double trust = 0.5;          // 0..1: promises kept and broken
    std::uint32_t wars = 0;      // wars fought against each other
    Tick last_war = kNever;      // when the last of them ended
    Tick since = 0;              // first met
    std::uint32_t claims_ab = 0, claims_ba = 0;  // cells of b that a claims, of a that b claims (this year)
    Tick withheld = kNever;        // when one last withheld tribute owed to the other
    std::uint32_t withheld_by = 0;  // which one
};

// A realm's claim on land it lost in war, until `until`.
struct Claim {
    CellId cell = kInvalidCell;
    std::uint32_t by = 0;  // the claimant
    Tick until = 0;
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
    Succession = 3,    // a claimant's party fights for the throne of the realm it rose in
};
enum class WarOutcome : std::uint8_t {
    Ongoing = 0,
    AttackersWon = 1,
    DefendersWon = 2,
    Stalemate = 3,
};

// What a peace settled (diplomacy.hpp).
enum class PeaceTerms : std::uint8_t {
    None = 0,        // still at war, or ended with a side gone
    WhitePeace = 1,  // neither prevailed: land taken in the war is given back
    LandKept = 2,    // the victors keep what they took
    LandReturned = 3,  // the defenders prevailed and took back what they had lost
    Tribute = 4,     // the loser pays tribute
    Vassalage = 5,   // the loser becomes a vassal
    Annexed = 6,     // the loser is taken whole
    Independence = 7,  // a subject won its freedom
    Dynasty = 8,     // nomads took the state and rule it
    Throne = 9,      // a war of succession won: the victor rules the whole realm
    Divided = 10,    // a war of succession ended with the realm divided between the claimants
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
    std::uint32_t army_a = 0, army_d = 0;  // the leaders' field armies now (SimState::armies), 0 if none
    std::uint32_t cells_a = 0, cells_d = 0;  // the leaders' cells when it began
    std::uint32_t wins_a = 0, wins_d = 0;    // battles won by each side's field army
    double score = 0.0;                      // -1 (defenders prevail) .. 1 (attackers prevail)
    PeaceTerms terms = PeaceTerms::None;
};

// A field army (WarSystem): raised by a war's leading realm on each side,
// led by its general, it marches cell by cell toward the enemy's army or
// capital, takes land where it stands, lives off the country it passes
// through and is worn down by marching, disease and battle. It disbands
// when the war ends.
struct Army {
    std::uint32_t id = 0;
    std::uint32_t polity = 0;
    std::uint32_t war = 0;
    std::uint32_t general = 0;   // a figure (its polity's general when raised), 0 if none
    CellId cell = kInvalidCell;  // where it is
    CellId goal = kInvalidCell;  // where it is going
    double men = 0.0;
    double raised = 0.0;         // men when raised
    Tick from = 0;
    Tick to = kNever;            // disbanded
    std::uint32_t victories = 0, defeats = 0;
};

// A battle between two field armies.
struct Battle {
    Tick tick = 0;
    CellId cell = kInvalidCell;
    std::uint32_t war = 0;
    std::uint32_t winner = 0, loser = 0;  // armies
    std::int64_t dead = 0;
};

// Why a people left its land in a body.
enum class MigrationCause : std::uint8_t {
    Famine = 0,     // the land could no longer feed them (often a climate shift)
    War = 1,        // their land was taken in war
    Raid = 2,       // nomads raided them
    Displaced = 3,  // a stronger people arrived and pushed them out (a chain reaction)
    Wandering = 4,  // a tribe or confederation moved as a whole, rulers and all
    Conquest = 5,   // herders following their horde into land it took in war
};

// A folk migration: a people moving in a body to land further off.
struct FolkMovement {
    Tick tick = 0;
    CellId from = kInvalidCell, to = kInvalidCell;
    std::int64_t people = 0;
    std::uint32_t language = 0, culture = 0;
    MigrationCause cause = MigrationCause::Famine;
    std::uint32_t pushed_by = 0;  // for Displaced: the index of the movement that pushed them, + 1
    std::uint32_t polity = 0;     // for Wandering: the polity on the move
};

// Where a technique was invented independently.
struct Invention {
    Tick tick = 0;
    CellId cell = kInvalidCell;
    std::uint32_t technique = 0;
    std::uint32_t culture = 0;  // main culture of the cell then
    std::uint32_t figure = 0;   // the inventor (SimState::figures)
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
    FolkMigration = 23,   // a people left its land in a body; cell = where from; value = index into SimState::migrations
    ReligionFounded = 24, // an organized religion arose; value = religion id
    Schism = 25,          // a sect split from its parent; value = the new religion's id
    StateConversion = 26, // a polity's rulers took up another faith; cell = capital; value = polity id
    PolityWandered = 27,  // a tribe or confederation moved to new land; cell = its new capital; value = polity id
    Succession = 28,      // a ruler came to the throne by inheritance; cell = capital; value = ruler id
    Usurpation = 29,      // another house seized the throne; cell = capital; value = ruler id (the usurper)
    InstitutionFounded = 30,  // cell = capital; value = polity id x 16 + Institution
    InstitutionLost = 31,     // cell = capital; value = polity id x 16 + Institution
    SuccessionDisputed = 32,  // a rival claimed the throne; cell = capital; value = the claimant's figure id
    ProvinceRevolt = 33,      // a province broke away with its governor; cell = its seat; value = province id
    PeasantUprising = 34,     // the people rose; cell = where; value = index into SimState::uprisings
    BattleFought = 35,        // two field armies met; cell = where; value = index into SimState::battles
    TributeWithheld = 36,     // a tributary stopped paying; cell = its capital; value = subject << 32 | overlord
    Submitted = 37,           // a realm offered tribute to a stronger neighbour; cell = its capital; value = its id
    Abandoned = 38,           // an overlord at war elsewhere left an attacked subject to fend for itself;
                              // cell = the subject's capital; value = subject << 32 | overlord
    Betrayal = 39,            // an ally made a separate peace, leaving a losing side; cell = its capital;
                              // value = ally << 32 | the side's leader
    LawChanged = 40,          // a realm changed a law; cell = its capital;
                              // value = polity << 16 | law << 8 | new level (Laws::Law)
    GovernorsRise = 41,       // disloyal governors rose together as one realm; cell = its seat; value = its id
    RealmDivided = 42,        // a realm was shared out among a ruler's sons; cell = the new realm's seat; value = its id
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
    double largest_sphere_km2 = 0.0;  // a realm with its tributaries and vassals (and theirs)
    std::uint32_t domesticated = 0;       // species domesticated somewhere so far
    double trade_value = 0.0;             // value of goods carried between cells this year (food = 1)
    double food_traded = 0.0;             // food carried between cells this year, people fed
    std::uint32_t wars = 0;               // wars under way
    std::int64_t war_deaths_total = 0;    // running total of deaths in war and raids
    std::uint32_t alliances = 0;          // alliances in force
    std::uint32_t subjects = 0;           // tributaries and vassals
    std::uint32_t nomads = 0;             // nomad confederations
    std::uint32_t raids = 0;              // nomad raids this year
    std::int64_t colonists = 0;           // farmers who left to settle new land this year
    std::int64_t folk_movers = 0;         // people in folk migrations this year
    std::uint32_t religions = 0;          // religions with followers
    std::int64_t organized_faithful = 0;  // people in lands of organized (not folk) religions
    std::int64_t enslaved = 0;            // people in bondage
    std::int64_t captives = 0;            // carried off in war and raids this year
    std::int64_t sold = 0;                // sold on this year
};

// A slave route: captives sold from one land to another, with the total
// carried along it and when it was first and last used.
struct SlaveRoute {
    CellId from = kInvalidCell, to = kInvalidCell;
    std::int64_t people = 0;
    Tick first = 0, last = 0;
    std::uint32_t language = 0;  // of the largest lot most recently sold
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

    // Rulers and their houses (entry 0 of each a placeholder).
    std::vector<Ruler> rulers;
    std::vector<Dynasty> dynasties;
    std::vector<Figure> figures;  // index = id; figures[0] is no one
    std::vector<Province> provinces;          // index = id; provinces[0] is no one (the crown's own land)
    std::vector<std::uint32_t> province_of;   // per cell: its province (0: the crown's, or no province)
    std::vector<Uprising> uprisings;
    std::vector<Commander> commanders;  // index = id; commanders[0] is no one
    std::vector<Army> armies;           // index = id; armies[0] is none
    std::vector<Relation> relations;    // sorted by (a, b)
    std::vector<Claim> claims;
    // Land a nomad confederation took in war this year, which its herders
    // move into next year (MigrationSystem): from the cell it struck from.
    struct Settling {
        CellId from = kInvalidCell, to = kInvalidCell;
        std::uint32_t polity = 0;
    };
    std::vector<Settling> settling;
    std::vector<Battle> battles;

    // Religions (entry 0 a placeholder) and each cell's faith (0 = none yet).
    std::vector<Religion> religions;
    std::vector<std::uint32_t> faith_of;

    // Folk migrations, in the order they happened (at least
    // MigrationSystem::kFolkRecordPeople people each), and this year's totals.
    std::vector<FolkMovement> migrations;
    // Per cell: the last recorded folk migration to arrive (index + 1, 0 =
    // none) and when, so a people driven out by newcomers can be traced back
    // to them.
    std::vector<std::uint32_t> last_arrival;
    std::vector<Tick> last_arrival_tick;
    // Per cell: what the land has fed lately (capacity smoothed over a few
    // years) and usually (over about fifty), so a lasting dearth, not one bad
    // harvest, can be told apart.
    std::vector<double> recent_capacity;
    std::vector<double> usual_capacity;
    std::int64_t colonists = 0;
    std::int64_t folk_movers = 0;

    // The slave trade: routes (sorted by from, then to) and this year's
    // totals.
    std::vector<SlaveRoute> slave_routes;
    std::int64_t captives = 0;
    std::int64_t sold = 0;
    std::int64_t march_deaths = 0;  // died on wandering peoples' treks, all time

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
