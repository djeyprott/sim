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

class PolitySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "polities"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Polities; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Reach of a capital's rule without administrative techniques.
    static constexpr double kBaseReachKm = 100.0;
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
    //                writing + kBureaucracyWeight with bureaucracy
    // Pressures (0-1): famine = kFamineScale x share of people beyond
    // capacity; war = shock; overreach = 1 - people-weighted mean control;
    // poverty = 1 - (trade value per person / kTradePerPerson); epidemic =
    // kEpidemicScale x (disease deaths per person - kUsualDiseaseDeaths);
    // division = share of people whose culture is of another lineage.
    static constexpr double kStabilityBase = 0.45;
    static constexpr double kStabilityRise = 0.1;
    static constexpr double kStabilityFall = 0.3;
    static constexpr double kHierarchyWeight = 0.25;
    static constexpr double kRitualWeight = 0.15;
    static constexpr double kWritingWeight = 0.1;
    static constexpr double kBureaucracyWeight = 0.1;
    static constexpr std::array<double, kPressureCount> kPressureWeight = {1.0, 0.8, 0.5, 0.15, 0.6, 0.3};
    static constexpr double kFamineScale = 3.0;
    static constexpr double kTradePerPerson = 0.05;
    static constexpr double kEpidemicScale = 40.0;
    static constexpr double kUsualDiseaseDeaths = 0.005;
    static constexpr double kShockDecay = 0.2;  // share of the war shock that fades a year
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
    // stays together under the old capital; elsewhere, towns of enough
    // farming, density and hierarchy become seats of successor polities that
    // take the cells within kSuccessorKm; the rest is left without a ruler.
    // A chiefdom (no writing) simply dissolves.
    static constexpr double kCoreControl = 0.5;
    static constexpr double kSuccessorKm = 250.0;
    static constexpr double kSuccessorStability = 0.5;
    // Nomad confederations form on open pasture (pasture quality at least
    // kNomadPasture: grassland, shrubland, savanna) where herds give at least
    // kNomadPastoral of the farm food, people ride horses and warfare is at
    // least kNomadWarfare: chance kNomadFormation x warfare x (1 + wealth of
    // the farmers within raiding range / kShadowWealth, at most kShadowMax)
    // per check. Their rule reaches kNomadReach times as far. Conquest
    // dynasties stop being nomads kSettleYears after taking a state.
    static constexpr double kNomadPasture = 1.0;
    static constexpr double kNomadPastoral = 0.5;
    static constexpr double kNomadWarfare = 0.4;
    static constexpr double kNomadFormation = 0.01;
    static constexpr double kShadowWealth = 20000.0;
    static constexpr double kShadowMax = 5.0;
    static constexpr double kNomadReach = 3.0;
    static constexpr std::int64_t kSettleYears = 60;
};

// Towns raise mortality (x (1 + kUrbanMortality x urban share)) and disease
// contact (density x (1 + kUrbanCrowding x urban share)).
inline constexpr double kUrbanMortality = 0.3;
inline constexpr double kUrbanCrowding = 20.0;

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
