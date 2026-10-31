// War, diplomacy and nomads (M3).
//
// Armies. A polity raises a levy from the people it controls: kLevy x (0.5 +
// warfare) of them where its control is full, less farther out, four times
// as many among mounted nomads. Military techniques (bows, horses, chariots,
// copper and bronze weapons) multiply each soldier's strength. Vassals add
// half their strength to their overlord's.
//
// Wars. Each year a polity that is not anyone's subject may attack a
// neighbour it much outmatches, more readily if its people are warlike. The
// victim's allies and overlord join its side; the attacker's vassals join
// its. While a war lasts, the stronger side takes enemy cells along the front
// (the ones it controls best), their people suffer, and some flee to their
// own side; both sides lose soldiers in proportion to the forces engaged,
// the weaker side more. Wars end when a leader's capital falls, when a leader
// is gone, or by exhaustion (more likely the longer they last). The winner
// keeps what it took; a beaten defender whose capital fell becomes a vassal
// (or is annexed if little is left), and one beaten in a war for tribute
// becomes a tributary. A truce follows.
//
// Diplomacy. A polity facing a much stronger neighbour seeks a defensive
// ally among its other neighbours, more readily one of related culture.
// Tributaries send kTribute of their goods to their overlord each year;
// vassals also fight for it. A subject that grows strong against its
// overlord rebels (a war for independence); a vassal held firmly is absorbed.
// Civil wars (polity.hpp) are wars for independence too.
//
// Nomads. Where herds feed most people, horses are ridden and the people are
// warlike, confederations of mounted herders form (polity.hpp). Their rule
// reaches far, they raise large levies, and each year they may raid a settled
// neighbour: carrying off part of its goods and killing some of its people,
// until it agrees to pay tribute.
#pragma once

#include <vector>

#include "histsim/scheduler.hpp"

namespace histsim {

class WarSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "war"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Polities; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Armies.
    static constexpr double kLevy = 0.04;         // share of people under arms at full control, warfare 0.5
    static constexpr double kNomadLevy = 4.0;     // nomads: every herder rides
    static constexpr double kVassalShare = 0.5;   // of a vassal's strength added to its overlord's
    static constexpr double kAllyShare = 0.5;     // of an ally's strength sent to a war
    // Declaring war: chance a year kDeclare x (0.5 + warfare) x min(2, ratio
    // - 1) when the attacker is at least kDeclareRatio times as strong as the
    // defender's side.
    static constexpr double kDeclareRatio = 1.2;
    static constexpr double kDeclare = 0.08;
    // Fighting: share of the smaller army engaged a year, share of those
    // engaged who die (the weaker side loses more), chance of taking a front
    // cell per unit of strength ratio above 1 (up to kMaxPushes tries a year
    // for a side at least that many times stronger), and what happens there.
    static constexpr double kEngaged = 0.3;
    static constexpr double kBattleDeaths = 0.1;
    static constexpr double kSeize = 0.3;
    static constexpr int kMaxPushes = 3;
    static constexpr double kSackDeaths = 0.03;
    static constexpr double kRefugees = 0.1;
    // Peace: chance a year kPeace x (1 + years / 10).
    static constexpr double kPeace = 0.05;
    static constexpr std::int64_t kTruceYears = 20;
    // A beaten polity left with this many cells or fewer is annexed outright.
    static constexpr std::size_t kAnnexBelowCells = 1;
    // Diplomacy.
    static constexpr double kThreat = 1.5;         // a neighbour this many times stronger is a threat
    static constexpr double kAlliance = 0.05;      // chance a year of finding an ally against it
    static constexpr double kAllianceEnd = 0.01;   // chance a year an alliance lapses
    static constexpr std::size_t kMaxAllies = 3;
    static constexpr double kTribute = 0.1;        // share of a subject's goods sent each year
    static constexpr double kRebelRatio = 0.7;     // subjects rebel when this strong against their overlord
    static constexpr double kRebellion = 0.03;
    static constexpr double kAbsorb = 0.01;        // chance a year a firmly held vassal is absorbed
    static constexpr double kAbsorbControl = 0.5;
    // Nomad raids: chance a year, share of goods taken, share of people killed,
    // and chance a successful raid makes a weaker victim pay tribute.
    static constexpr double kRaid = 0.2;
    static constexpr double kRaidLoot = 0.3;
    static constexpr double kRaidDeaths = 0.01;
    static constexpr double kRaidTribute = 0.1;
};

// Each living polity's own fighting strength (levy x quality), by polity id.
std::vector<double> polity_strengths(const World& world, const SimState& state);

// The polity at the top of a chain of overlords.
std::uint32_t top_overlord(const SimState& state, std::uint32_t polity);

// Whether two polities are in an alliance now / at war now (on opposite sides).
bool allied(const SimState& state, std::uint32_t a, std::uint32_t b);
std::uint32_t war_between(const SimState& state, std::uint32_t a, std::uint32_t b);

// Start a war (leaders first on each side; allies and overlords are added).
// Returns its id.
std::uint32_t start_war(const World& world, SimState& state, Tick tick, std::uint32_t attacker,
                        std::uint32_t defender, WarGoal goal);

// "won by the attackers" etc.
const char* war_outcome_name(WarOutcome outcome);
const char* war_goal_name(WarGoal goal);

}  // namespace histsim
