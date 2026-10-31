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
// weakly held neighbours, take land from weaker polities, lose provinces they
// barely control, and now and then split in civil war.
#pragma once

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
    // Expansion: chance a year of taking the best stateless neighbour, and of
    // conquering a neighbour's cell per unit of power ratio above 1.5.
    static constexpr double kAnnex = 0.03;
    static constexpr double kConquest = 0.02;
    // Civil war: chance a year per unit of (1 - mean control), for polities
    // of at least kCivilWarCells cells.
    static constexpr double kCivilWar = 0.004;
    static constexpr std::size_t kCivilWarCells = 6;
    // Collapse: chiefdoms rise and fall within a century or two ("chiefly
    // cycling", Anderson 1994); states last longer. Chance a year, raised by
    // famine at the capital (x (1 + 3 x (crowding - 1)) above capacity).
    static constexpr double kChiefdomCollapse = 0.006;
    static constexpr double kStateCollapse = 0.0015;
};

// Towns raise mortality (x (1 + kUrbanMortality x urban share)) and disease
// contact (density x (1 + kUrbanCrowding x urban share)).
inline constexpr double kUrbanMortality = 0.3;
inline constexpr double kUrbanCrowding = 20.0;

// How far a capital's rule reaches, in km, from the techniques known there.
double polity_reach_km(const World& world, const SimState& state, CellId capital);

// Control a polity has over a cell at a distance from its capital (0-1).
[[nodiscard]] inline double polity_control(double distance_km, double reach_km) {
    const double r = distance_km / reach_km;
    return 1.0 / (1.0 + r * r);
}

// "chiefdom", "state" or "empire".
const char* polity_level(const World& world, const SimState& state, std::uint32_t polity);

// A polity's cells, people and area now.
struct PolitySize {
    std::size_t cells = 0;
    std::int64_t people = 0;
    double area_km2 = 0.0;
};
PolitySize polity_size(const World& world, const SimState& state, std::uint32_t polity);

// Whether a polity's capital knows writing (what makes it a state).
bool polity_has_writing(const World& world, const SimState& state, std::uint32_t polity);

}  // namespace histsim
