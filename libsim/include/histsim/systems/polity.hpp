// Towns, cities and polities (M3).
//
// Towns grow where farming feeds many people per km^2 (a surplus that can
// feed people who do not farm), more at the seats of rulers. Once a cell's
// towns hold 5,000 people it has a city, named in its people's language.
// Towns are deadlier (crowding and dirt: the urban graveyard effect) and
// raise disease contact, so crowd diseases can finally keep circulating.
//
// Polities form where dense farming, a taste for hierarchy and circumscription
// (people cannot easily leave: fertile land hemmed in by sea, desert or
// mountains, after Carneiro 1970) come together. A polity's control of a cell
// fades with distance from its capital: control = 1 / (1 + (d / reach)^2),
// with reach growing with administrative techniques (writing, bureaucracy,
// roads) and contact (horses, wheels, sails). So a chiefdom without writing
// holds about one cell, a state with writing a ring around its capital, and
// only states with bureaucracy and roads reach further. Polities absorb
// weakly held stateless neighbours, lose provinces they barely control, and
// now and then split in civil war; land changes hands between polities only
// in war (war.hpp).
//
// Polities do not fall by a set chance: each has a stability that tracks its
// resilience (a taste for hierarchy and ritual, writing, bureaucracy) against
// what weighs on it now: famine, the toll of war, overreach, poverty,
// epidemics and subjects of other cultures. Fragile polities lose provinces,
// fall into civil war and break up, and the cause is recorded. A chiefdom
// breaks into its villages; a state leaves a core under its capital and
// successor states around its other towns. Only succession disputes, until
// rulers are simulated, remain a small chance.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "histsim/scheduler.hpp"

namespace histsim {

class SettlementSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "settlement"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Production; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Town share target = kPerSurplus x (people fed by farmland per km^2 -
    // kThreshold), up to kMax; capitals get kCapital more.
    static constexpr double kThreshold = 0.4;
    static constexpr double kPerSurplus = 0.08;
    static constexpr double kMax = 0.35;
    static constexpr double kCapital = 0.04;
    static constexpr double kRate = 0.01;  // share of the gap closed per year
    static constexpr double kCityPeople = 5000.0;
};

// Rulers and dynasties. Each polity has a ruler, of a house (dynasty). A
// new polity's first ruler founds a house; each year a ruler may die (the
// forager life table x kRulerMortality, by age), and an heir of the house
// succeeds: a child (under kAdultAge, ruled for by a regent) if the ruler
// died young. A succession is disputed with chance kDispute x (1.5 -
// stability) x 2 for a child heir: then the realm may break up
// (kBreakupChiefdom, or kBreakupState with writing, out of the disputed),
// or another house seizes the throne (a usurpation). Rulers are more or
// less able (stability target + kAbilityWeight x (ability - 0.5) x 2;
// kRegencyPenalty under a regent) and martial (war declared x (0.5 +
// martial)).
class RulerSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "rulers"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Polities; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kRulerMortality = 0.8;
    static constexpr int kAdultAge = 16;
    static constexpr double kDispute = 0.15;
    // Tuned so that, with reigns of about 20-25 years, break-ups come about
    // as often as the old yearly succession-crisis chances (0.05% a year for
    // chiefdoms, 0.02% for states).
    static constexpr double kBreakupChiefdom = 0.065;
    static constexpr double kBreakupState = 0.026;
    static constexpr double kAbilityWeight = 0.1;
    static constexpr double kRegencyPenalty = 0.1;
};

// Institutions (Polity::institutions). Each year a polity without one may
// found it (kFound a year) when it qualifies:
//   temple priesthood: its rulers hold an organized faith, or ritual at the
//     capital is at least kTempleRitual with a town of kTempleTown;
//   council of notables: at least kCouncilCells cells and hierarchy at
//     least kCouncilHierarchy;
//   law code: writing;
//   standing army: writing, at least kArmyPeople people, and bronze or
//     riding.
// Effects: stability target + kTempleStability (temple), + kLawStability
// (law code); disputed successions x kTempleDispute (temple) x
// kCouncilDispute (council); division pressure x kLawDivision; overreach x
// kArmyOverreach and war strength x kArmyStrength (army). Below kFragile
// stability each may be lost (kDecay a year). A state's core and successors
// inherit its institutions, but not its army.
struct Institutions {
    static constexpr double kFound = 0.01;
    static constexpr double kDecay = 0.01;
    static constexpr double kTempleRitual = 0.6;
    static constexpr double kTempleTown = 5000.0;
    static constexpr std::size_t kCouncilCells = 3;
    static constexpr double kCouncilHierarchy = 0.4;
    static constexpr double kArmyPeople = 300000.0;
    static constexpr double kTempleStability = 0.05;
    static constexpr double kLawStability = 0.05;
    static constexpr double kTempleDispute = 0.8;
    static constexpr double kCouncilDispute = 0.6;
    static constexpr double kLawDivision = 0.7;
    static constexpr double kArmyOverreach = 0.7;
    static constexpr double kArmyStrength = 1.25;
};
const char* institution_name(Institution i);

// A ruler's name with regnal number ("Kimo II"), and his or her age now.
std::string ruler_name(const Ruler& ruler);
int ruler_age(const Ruler& ruler, Tick now);

class PolitySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "polities"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Polities; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Each year the rulers' people (the capital's speakers of the polity's
    // language) send kOfficials x control x the people of each province held
    // at least kMinControl: officials, garrisons and merchants, who carry
    // the rulers' language (and, with its prestige, spread it).
    static constexpr double kOfficials = 0.001;
    // The capital moves to a town of the realm (for nomads, any cell) with
    // this many times as many townspeople (people) as the old seat.
    static constexpr double kCapitalMove = 2.0;
    // Reach of a capital's rule without administrative techniques.
    static constexpr double kBaseReachKm = 85.0;
    // Below this control a polity cannot take a cell; below kSecedeBelow a
    // province may break away (kSecession a year).
    static constexpr double kMinControl = 0.3;
    static constexpr double kSecedeBelow = 0.2;
    static constexpr double kSecession = 0.01;
    // Formation, checked every kFormationYears: farming share, people per
    // km^2 and hierarchy needed, and the base chance per check.
    static constexpr std::int64_t kFormationYears = 5;
    static constexpr double kMinFarming = 0.5;
    static constexpr double kMinDensity = 0.4;
    static constexpr double kMinHierarchy = 0.3;
    static constexpr double kFormation = 0.01;
    // Expansion: chance a year of taking the best stateless neighbour.
    static constexpr double kAnnex = 0.03;
    // Civil war: chance a year per unit of (1 - mean control), for polities
    // of at least kCivilWarCells cells.
    static constexpr double kCivilWar = 0.004;
    static constexpr std::size_t kCivilWarCells = 6;
    // Stability. Each year it moves toward a target (by kStabilityRise of
    // the gap when rising, kStabilityFall when falling):
    //   target = kStabilityBase + resilience - sum of weight x pressure
    //   resilience = kHierarchyWeight x hierarchy + kRitualWeight x ritual
    //                (of the people at the capital) + kWritingWeight with
    //                writing + kBureaucracyWeight with bureaucracy +
    //                kFaithWeight x share of people holding the rulers'
    //                organized faith (or a sect of it)
    // Pressures (0-1): famine = kFamineScale x share of people beyond
    // capacity; war = shock; overreach = 1 - people-weighted mean control,
    // where control counts kMobileRule x mobility less (mobile people slip
    // away from tax collectors and garrisons);
    // poverty = 1 - (trade value per person / kTradePerPerson); epidemic =
    // kEpidemicScale x (disease deaths per person - kUsualDiseaseDeaths);
    // division = share of people whose culture is of another lineage +
    // kFaithDivision x share holding an organized faith not the rulers' +
    // kBondDivision x share held in bondage.
    static constexpr double kStabilityBase = 0.45;
    static constexpr double kStabilityRise = 0.1;
    static constexpr double kStabilityFall = 0.3;
    static constexpr double kHierarchyWeight = 0.25;
    static constexpr double kRitualWeight = 0.15;
    static constexpr double kWritingWeight = 0.1;
    static constexpr double kBureaucracyWeight = 0.1;
    static constexpr double kFaithWeight = 0.1;
    static constexpr double kMobileRule = 0.5;
    static constexpr double kBondDivision = 0.5;
    static constexpr double kFaithDivision = 0.5;
    static constexpr std::array<double, kPressureCount> kPressureWeight = {1.0, 0.8, 0.5, 0.15, 0.6, 0.3};
    static constexpr double kFamineScale = 3.0;
    static constexpr double kTradePerPerson = 0.05;
    static constexpr double kEpidemicScale = 40.0;
    static constexpr double kUsualDiseaseDeaths = 0.005;
    static constexpr double kShockDecay = 0.2;  // share of the war shock that fades a year
    // A people on the march, and for kArrivalYears after it settles, holds
    // together under its war leaders (+kMarchCohesion to the target) and
    // lives off the herds and stores it brings and the harvests it takes
    // (famine pressure x kMarchFamine).
    static constexpr std::int64_t kArrivalYears = 20;
    static constexpr double kMarchCohesion = 0.15;
    static constexpr double kMarchFamine = 0.3;
    // A polity below kFragile stability breaks up with chance kCollapse x
    // ((kFragile - stability) / kFragile)^2 a year. Fragility also speeds
    // secession and civil war (x 2 x (1 - stability)).
    static constexpr double kFragile = 0.3;
    static constexpr double kCollapse = 0.3;
    // Succession disputes: until rulers are simulated (M3 part 6), a small
    // chance a year that a ruler's death splits the polity whatever its
    // state, larger for chiefdoms (rule by kin) than for states.
    static constexpr double kChiefdomSuccession = 0.0005;
    static constexpr double kStateSuccession = 0.0002;
    // When a state breaks up, its core (cells it controls at least kCoreControl)
    // stays together under the old capital. Elsewhere, places with the
    // farming, density and hierarchy to found a polity are ranked by
    // (townspeople + kSeatRural x people) x hierarchy, x kSeatWriting if they
    // know writing and x kSeatBureaucracy with scribes; strongest first, each
    // not already held (control at least kMinControl) by a stronger seat
    // becomes the seat of a successor that takes the cells it holds best, as
    // far as its own administration reaches (a province at least, in a state). Seats with writing are successor
    // states, the others chiefdoms; cells no seat holds fall back to
    // villages. A chiefdom of at least kSplitChiefdomCells (level-5) cells
    // breaks the same way into smaller chiefdoms; a smaller one dissolves.
    static constexpr double kCoreControl = 0.5;
    static constexpr double kSeatRural = 0.05;
    static constexpr double kSeatWriting = 4.0;
    static constexpr double kSeatBureaucracy = 2.0;
    static constexpr std::size_t kSplitChiefdomCells = 3;
    // A seat in a fallen state reaches at least kProvinceReachKm: it held a
    // province.
    static constexpr double kProvinceReachKm = 200.0;
    static constexpr double kSuccessorStability = 0.5;
    // Nomad confederations form on open pasture (pasture quality at least
    // kNomadPasture: grassland, shrubland, savanna) where herds give at least
    // kNomadPastoral of the farm food, people ride (horses, or camels in deserts) and warfare is at
    // least kNomadWarfare: chance kNomadFormation x warfare x (1 + wealth of
    // the farmers within raiding range / kShadowWealth, at most kShadowMax)
    // per check, but only with a state (a polity with writing) within
    // kShadowKm (otherwise kNoShadow times as readily): the shadow empire
    // (Barfield 1989), steppe confederations living off the tribute and
    // trade of agrarian neighbours. Mounted herders who do not band
    // together form tribal chiefdoms. Their rule reaches kNomadReach
    // times as far. Conquest
    // dynasties stop being nomads kSettleYears after taking a state.
    // Herders without mounts form ordinary (tribal) chiefdoms, needing only
    // kHerderDensity x kMinDensity people per km^2; those that later take
    // to riding in mounted country become confederations. Camel keepers who
    // ride count desert and dry scrub as mounted country too.
    static constexpr double kHerderDensity = 0.25;
    static constexpr double kNomadPasture = 1.0;
    static constexpr double kNomadPastoral = 0.5;
    static constexpr double kNomadWarfare = 0.4;
    static constexpr double kNomadFormation = 0.01;
    static constexpr double kShadowWealth = 20000.0;
    static constexpr double kShadowMax = 5.0;
    static constexpr double kNomadReach = 2.0;
    // A confederation needs a state within this distance (the shadow empire).
    static constexpr double kShadowKm = 1500.0;
    // Without one, confederations still form, kNoShadow times as readily:
    // the steppe's own wars could unite it too (the early Scythians).
    static constexpr double kNoShadow = 0.2;
    // A riding herder chiefdom becomes a confederation with chance
    // kRiderUnite a check beside a state (x kNoShadow without): most riding
    // tribes stay tribes, for a confederation to take in as clans.
    static constexpr double kRiderUnite = 0.05;
    static constexpr std::int64_t kSettleYears = 60;
};

// Towns raise mortality (x (1 + kUrbanMortality x urban share)) and disease
// contact (density x (1 + kUrbanCrowding x urban share)).
inline constexpr double kUrbanMortality = 0.3;
inline constexpr double kUrbanCrowding = 20.0;

// People and townspeople of each cell's hinterland (World::for_hinterland),
// so thresholds like "a town of 20,000" mean the same at every grid level. At
// grid level 5 and coarser, a cell's own.
void hinterland_people(const World& world, const SimState& state, std::vector<double>& people,
                       std::vector<double>& town);

// How far a capital's rule reaches, in km, from the techniques known there.
double polity_reach_km(const World& world, const SimState& state, CellId capital);
// The same for a polity: nomads reach kNomadReach times as far.
double polity_reach(const World& world, const SimState& state, std::uint32_t polity);

// Control a polity has over a cell at a distance from its capital (0-1).
[[nodiscard]] inline double polity_control(double distance_km, double reach_km) {
    const double r = distance_km / reach_km;
    return 1.0 / (1.0 + r * r);
}

// "chiefdom", "state", "empire" or (nomads without writing) "confederation".
const char* polity_level(const World& world, const SimState& state, std::uint32_t polity);

// A polity's cells, people and area now.
struct PolitySize {
    std::size_t cells = 0;
    std::int64_t people = 0;
    double area_km2 = 0.0;
};
PolitySize polity_size(const World& world, const SimState& state, std::uint32_t polity);

// "famine", "war", ... for a pressure; "a succession crisis" for that cause.
const char* pressure_name(Pressure pressure);
// How a polity ended, in words: "collapsed in famine", "conquered", ...
std::string polity_fate_text(const Polity& polity);

// Whether a polity's capital knows writing (what makes it a state).
bool polity_has_writing(const World& world, const SimState& state, std::uint32_t polity);

}  // namespace histsim
