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
// becomes a tributary. A truce follows. Every loss shakes the loser's
// stability (polity.hpp), which can bring it down.
//
// Diplomacy. A polity facing a much stronger neighbour seeks a defensive
// ally among its other neighbours, more readily one of related culture.
// Tributaries send kTribute of their goods to their overlord each year;
// vassals also fight for it. A subject that grows strong against its
// overlord rebels (a war for independence); a vassal held firmly is absorbed.
// Civil wars (polity.hpp) are wars for independence too.
//
// Nomads. On open pasture where herds feed most people, horses are ridden and
// the people are warlike, confederations of mounted herders form (polity.hpp),
// more readily the richer the farmers within riding range: the steppe empires
// rose opposite rich agrarian states (Barfield 1989). Their rule reaches far,
// they raise large levies, and each year they may raid the richest settled
// farmers within kRaidKm (towns first), carrying off goods and killing some
// people, until the victims' rulers agree to pay tribute. Nomads who take a
// settled realm's capital in war rule it from there as a conquest dynasty and
// settle down over a few generations.
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
    // Nomad raids: chance a year, reach (from any cell the nomads hold),
    // share of goods taken, share of people killed, and chance a successful
    // raid makes the victim's ruler pay tribute (if the nomads are at least
    // kRaidTributeRatio times as strong). The raiders' strength falls with
    // their control at the distance; defenders fight with their ruler's
    // control there, or with the villagers' own levy.
    static constexpr double kRaid = 0.3;
    static constexpr double kRaidKm = 600.0;
    static constexpr double kRaidLoot = 0.3;
    static constexpr double kRaidDeaths = 0.01;
    static constexpr double kRaidTribute = 0.2;
    static constexpr double kRaidTributeRatio = 0.5;
    // Nomads who take the capital of a state (with writing) or of a polity
    // seated in a city, in a war for tribute, and are this many times as
    // strong rule it instead.
    static constexpr double kConquestDynastyRatio = 1.5;
    // Share of the people of the cell a conquest was launched from who move
    // into the taken cell (settlers and garrisons), and of the nomads'
    // homeland who follow a conquest dynasty to its new capital.
    static constexpr double kConquestSettlers = 0.03;
    static constexpr double kDynastyHerders = 0.1;
    // The toll of war on stability (Polity::shock, which PolitySystem lets
    // fade): land lost (per share of people in it), war dead (per share of
    // people), a lost war, a raid; a won war eases it.
    static constexpr double kShockLand = 1.0;
    static constexpr double kShockDead = 5.0;
    static constexpr double kShockDefeat = 0.3;
    static constexpr double kShockRaid = 0.05;
    static constexpr double kVictoryRelief = 0.1;
};

// The cells within `km` of a cell (itself included), by walking neighbours.
void cells_within(const World& world, CellId cell, double km, std::vector<CellId>& out);

// What raiders could carry off from a cell: people in towns plus a twentieth
// of the rest, where settled farmers live (0 for herders and foragers).
double raid_wealth(const SimState& state, CellId cell);

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
