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
    // Direct rule only where it pays and the realm can manage it.
    // Worth: a cell's people (farmers in full, foragers at kForagerWorth)
    // x (1 + its trade per person / kTradePerPerson, at most 3), per km^2,
    // against the same over the realm's own land. Land as rich as its own is
    // taken at kMinControl; richer land further out (control kMinControl /
    // sqrt(ratio), at least kMinWorthControl), poorer only nearer (at most
    // kMaxWorthControl).
    static constexpr double kForagerWorth = 0.2;
    static constexpr double kMinWorthControl = 0.15;
    static constexpr double kMaxWorthControl = 0.6;
    // Capacity: ruling a cell directly takes 1 / control of the realm's
    // administration (a level-5 cell's worth of land); a realm runs at most
    // kAdminLoad x (1 + administration) x (1 + 0.3 contact) (x kNomadReach
    // for nomads). At capacity it takes no more land, and it makes war on a
    // neighbour for tribute rather than for its land.
    static constexpr double kAdminLoad = 40.0;
    // A settled realm at kTributeLoad of its capacity makes war for tribute:
    // its victories in the field win it a tributary, not land.
    static constexpr double kTributeLoad = 0.8;
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
    // Provinces. A state (a polity with writing) of at least
    // kProvinceMinCells (level-5) cells is divided into provinces seated at its largest towns at least
    // kProvinceSpacingKm from the capital and from each other (ranked by
    // townspeople + kSeatRural x people); each cell belongs to the nearest
    // seat, the capital's being the crown's own. Each province has a governor
    // (a figure); its loyalty moves kLoyaltyRate of the way a year toward
    //   kLoyaltyBase + kLoyaltyStability x stability + kLoyaltyControl x
    //   control at its seat + kLoyaltyPeople (its people are the rulers')
    //   + kLoyaltyFaith (and their faith) + kLoyaltyAmbition x (0.5 -
    //   governor's ambition).
    // Below kRevoltBelow, it breaks away with chance kRevolt x (kRevoltBelow -
    // loyalty) / kRevoltBelow x (0.5 + ambition) a year, the governor
    // founding a house, and fights for independence; the realm loses
    // kRevoltShock stability and every other province kRevoltContagion
    // loyalty. When a provincial realm breaks up, provinces below
    // kStayLoyal go their own ways with their governors; the rest stay with
    // the rump. Provincial realms lose land this way instead of by cell
    // secession and civil war.
    static constexpr std::size_t kProvinceMinCells = 4;
    static constexpr double kProvinceSpacingKm = 500.0;
    static constexpr double kLoyaltyRate = 0.1;
    static constexpr double kLoyaltyBase = 0.0;
    static constexpr double kLoyaltyStability = 0.5;
    static constexpr double kLoyaltyControl = 0.4;
    static constexpr double kLoyaltyPeople = 0.1;
    static constexpr double kLoyaltyFaith = 0.05;
    static constexpr double kLoyaltyAmbition = 0.3;
    static constexpr double kRevoltBelow = 0.4;
    static constexpr double kRevolt = 0.05;
    static constexpr double kRevoltShock = 0.05;
    static constexpr double kRevoltContagion = 0.05;
    static constexpr double kStayLoyal = 0.6;
    // Peasant uprisings, in states. Each province, and the crown's own land,
    // has an unrest that moves kUnrestRate of the way a year toward
    //   kUnrestHunger x (share of its people beyond what its land feeds) +
    //   kUnrestCrowding x (people / what the land feeds - kCrowdedFrom) /
    //   kCrowdedSpan (0-1: land hunger, as the land fills up) +
    //   kUnrestAtWar (at war) + kUnrestWar x the recent toll of war (shock) +
    //   kUnrestPlague x epidemic pressure +
    //   kUnrestTribute (tribute paid to an overlord) + kUnrestForeign
    //   (rulers of another people) + kUnrestFaith (and faith) +
    //   kUnrestInept x (0.5 - its governor's, or on the crown's land the
    //   ruler's, ability).
    // Above kRiseAbove, the people rise with chance kRise x (unrest -
    // kRiseAbove) / (1 - kRiseAbove) a year, kRebelShare x unrest of them
    // taking up arms against a force of kGarrison x the realm's strength x
    // the control at the seat x (0.5 + stability) (x kArmyGarrison with a
    // standing army). Crushed (chance force / (force + rebels)), kCrushDeaths
    // of the region's people die, unrest falls by kCrushCalm and the realm
    // loses kUprisingShock stability. Victorious, kRebelDeaths die; a
    // province breaks away under the rebel leader, and on the crown's land,
    // or with kOverthrowRatio times the force, the dynasty falls and the
    // leader takes the throne; the realm loses kUprisingWinShock stability.
    static constexpr double kUnrestRate = 0.2;
    static constexpr double kUnrestHunger = 1.0;
    static constexpr double kUnrestWar = 0.3;
    static constexpr double kUnrestCrowding = 0.3;
    static constexpr double kCrowdedFrom = 0.85;
    static constexpr double kCrowdedSpan = 0.3;
    static constexpr double kUnrestAtWar = 0.1;
    static constexpr double kUnrestPlague = 0.3;
    static constexpr double kUnrestTribute = 0.15;
    static constexpr double kUnrestForeign = 0.15;
    static constexpr double kUnrestFaith = 0.1;
    static constexpr double kUnrestInept = 0.2;
    static constexpr double kRiseAbove = 0.3;
    static constexpr double kRise = 0.08;
    static constexpr double kRebelShare = 0.15;
    static constexpr double kGarrison = 0.1;
    static constexpr double kArmyGarrison = 1.5;
    static constexpr double kCrushDeaths = 0.02;
    static constexpr double kCrushCalm = 0.5;
    static constexpr double kUprisingShock = 0.03;
    static constexpr double kRebelDeaths = 0.01;
    static constexpr double kOverthrowRatio = 2.0;
    static constexpr double kUprisingWinShock = 0.1;
    // Generals. A state at war or with a standing army has a general (a
    // figure); his side fights at (1 - kGeneralSkill / 2 + kGeneralSkill x
    // martial) of its strength. A victory brings kFameVictory fame
    // (kFameCapital more if the enemy capital fell), a defeat costs
    // kFameDefeat and with chance kDismiss his command; fame fades kFameFade
    // a year. Each year he may seize the throne with chance kCoup x fame x
    // (0.5 + ambition) x (1.5 - stability) x (1.5 - the ruler's ability), x 2
    // under a regency. A general of at least kUsurperFame is the usurper when
    // a disputed succession goes to another house.
    static constexpr double kGeneralSkill = 0.5;
    static constexpr double kFameVictory = 0.3;
    static constexpr double kFameCapital = 0.3;
    static constexpr double kFameDefeat = 0.3;
    static constexpr double kDismiss = 0.5;
    static constexpr double kFameFade = 0.03;
    static constexpr double kCoup = 0.01;
    static constexpr double kUsurperFame = 0.3;
    // High priests. A state with a temple priesthood or an organized faith
    // has a high priest (a figure): he lends a pious ruler legitimacy and
    // turns on an impious one, the stability target moving by
    // kPriestLegitimacy x 2 x (the ruler's piety - 0.5) x (0.5 + his piety);
    // a zealous one (piety above 0.5) stirs the people of other faiths,
    // unrest + kPriestZeal x 2 x (piety - 0.5).
    static constexpr double kPriestLegitimacy = 0.1;
    static constexpr double kPriestZeal = 0.1;
    // A general at the head of a field army that has won battles is that
    // much more dangerous: coup chance x (1 + kArmyCoup x min(2, its victories)).
    static constexpr double kArmyCoup = 1.0;
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

// How far a realm's rule must travel: km over open level ground; dearer
// through forest, desert, tundra, rainforest and high ground (rule_terrain)
// and up steep slopes, cheaper along rivers and, for peoples with boats,
// across the sea; eased by carts, roads and ships (at half the effect they
// have on reach). Control falls with this
// distance rather than a straight line, so borders follow mountains,
// deserts, rivers and coasts.
struct RuleTravel {
    static constexpr double kRiver = 0.8;      // x km along a navigable river (/ (1 + water transport))
    static constexpr double kSea = 1.0;        // x km by sea (with boats; / (1 + water transport))
    static constexpr double kClimb = 0.6;      // overland x (1 + kClimb x height difference in km)
    static constexpr double kHighM = 1000.0;    // ground above this is rough:
    static constexpr double kHighScale = 1500.0;  // x (1 + metres above kHighM / kHighScale)
    static constexpr double kSearch = 6.0;     // each realm's distances are found out to kSearch x its reach;
    static constexpr double kFar = 1.0;        // beyond, kFar x the straight-line distance (at least the limit)
};
// How hard a cell is to cross: 1 for temperate lowland, 0.9 open
// grassland, 1.4 boreal forest, 2 desert, tundra and rainforest, 4 ice;
// more high up.
[[nodiscard]] double rule_terrain(const World& world, CellId cell);

// Each living realm's travel distance (as above) from its capital to the
// cells within its search radius, found once per system step.
class RuleField {
public:
    void build(const World& world, const SimState& state, const std::vector<double>& reach);
    [[nodiscard]] double km(const World& world, const SimState& state, std::uint32_t polity, CellId cell) const;

private:
    std::vector<std::vector<std::pair<CellId, double>>> near_;  // per polity, sorted by cell
    std::vector<double> limit_;
};
// One realm's travel distance to one cell (a search of its own: for the
// viewer and rare uses).
[[nodiscard]] double rule_km(const World& world, const SimState& state, std::uint32_t polity, CellId cell);
// The same between any two cells, under the techniques known at `from`.
[[nodiscard]] double rule_km_between(const World& world, const SimState& state, CellId from, CellId to, double limit_km);

// How much administration a realm can run directly (see kAdminLoad), and
// each living realm's load: the sum over its cells of 1 / control (in
// level-5 cells' worth of land).
[[nodiscard]] double admin_capacity(const World& world, const SimState& state, std::uint32_t polity);
[[nodiscard]] std::vector<double> admin_loads(const World& world, const SimState& state, const RuleField& rule,
                                              const std::vector<double>& reach);

// Control a polity has over a cell at a distance from its capital (0-1).
[[nodiscard]] inline double polity_control(double distance_km, double reach_km) {
    const double r = distance_km / reach_km;
    return 1.0 / (1.0 + r * r);
}

// How a polity's general scales its strength in war (1 without one).
[[nodiscard]] inline double general_factor(const SimState& s, std::uint32_t polity) {
    if (polity == 0 || polity >= s.polities.size()) return 1.0;
    const std::uint32_t c = s.polities[polity].commander;
    if (c == 0 || c >= s.commanders.size() || s.commanders[c].figure >= s.figures.size()) return 1.0;
    return 1.0 - PolitySystem::kGeneralSkill / 2.0 + PolitySystem::kGeneralSkill * s.figures[s.commanders[c].figure].martial;
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
