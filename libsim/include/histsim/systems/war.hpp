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
    // A side cannot take a cell it would hold with less control than this
    // (beyond its rulers' reach): armies outran no realm's grip for long.
    // Distance is counted less (spacing of cells - kConquestNearKm), so that
    // the capital's neighbours count as kConquestNearKm away at every grid
    // level (225 km spacing at level 5).
    static constexpr double kConquestControl = 0.15;
    static constexpr double kConquestNearKm = 112.5;
    static constexpr double kLevel5CellKm = 225.0;
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
    // Chance a year a firmly held vassal is absorbed: its capital within the
    // overlord's control kAbsorbControl, the distance counted less the cell
    // spacing beyond kConquestNearKm as for conquest (a neighbour of the
    // overlord's capital is 112.5 km off at any grid level).
    static constexpr double kAbsorb = 0.01;
    static constexpr double kAbsorbControl = 0.5;
    // Steppe confederations grow by taking in the herder tribes they beat:
    // a herder chiefdom (no writing, no city, mostly herding) beaten by
    // nomads, in war or by raids, becomes a vassal clan (adding half its
    // strength, and absorbed as other vassals are) rather than a tributary.
    static constexpr double kHerderTribe = 0.5;  // herds' share of its farm food
    // A herder clan bordering its confederation merges into it with this
    // chance a year (firm control is not needed: the steppe's ties are of
    // kin and loyalty).
    static constexpr double kClanAbsorb = 0.02;
    // Plunder: loot and tribute received, at base prices, fading kPlunderFade
    // a year. A confederation's followers come for it: its strength is x (1
    // + kPlunderStrength x plenty) and its stability target moves by
    // kPlunderStability x (2 x plenty - 1), where plenty = min(1, plunder /
    // (people x kPlunderPerPerson)). Cut off from loot, it loses followers.
    static constexpr double kPlunderFade = 0.2;
    static constexpr double kPlunderStrength = 0.5;
    static constexpr double kPlunderStability = 0.1;
    static constexpr double kPlunderPerPerson = 0.05;
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
    // Field armies. Each side's leader raises kFieldShare of its levy as a
    // field army at its capital, under its general. Each year it marches
    // up to kMarchKm (x kNomadMarch for mounted nomads, x (1 + land
    // transport)) by land toward the enemy army, if that is in its own
    // side's land or within kEngageKm, else toward the enemy capital. The
    // land its side takes in a year is taken next to it (the cells nearest
    // the army) and it moves into what it takes.
    static constexpr double kFieldShare = 0.5;
    static constexpr double kMarchKm = 450.0;
    static constexpr double kNomadMarch = 2.0;
    static constexpr double kEngageKm = 700.0;
    // Battle: armies in the same or neighbouring cells fight. The winner is
    // drawn with chance s_a^2 / (s_a^2 + s_b^2), strength = men x quality x
    // general. The loser loses kRoutLoss of its men and falls back a cell
    // toward its capital; the winner loses kWinLoss x the loser's strength
    // ratio. Its general gains kFameBattle.
    static constexpr double kRoutLoss = 0.4;
    static constexpr double kWinLoss = 0.1;
    static constexpr double kFameBattle = 0.15;
    // An army worn below kBroken of the men it was raised with breaks up;
    // its side fights on at kNoArmy of its strength (garrisons and local
    // levies), the war is kNoArmyPeace times as likely to end each year, and
    // after kReraiseYears it raises a new army.
    static constexpr double kBroken = 0.2;
    static constexpr double kNoArmy = 0.6;
    static constexpr double kNoArmyPeace = 2.0;
    static constexpr std::int64_t kReraiseYears = 5;
    // Wear: kAttrition of its men a year in enemy land (x kHomeAttrition at
    // home), x kHarshAttrition in desert, tundra and rainforest; in its own
    // land it is refilled by kRefill of its strength a year.
    static constexpr double kAttrition = 0.08;
    static constexpr double kHomeAttrition = 0.3;
    static constexpr double kHarshAttrition = 2.5;
    static constexpr double kRefill = 0.15;
    // Living off the land: the army eats kForage years of food per man from
    // the stores of the cell it stands in; where the stores run short,
    // kForageDeaths of the people starve (in proportion to the shortfall),
    // and the unrest of the land (province or crown) rises by kArmyUnrest.
    static constexpr double kForage = 0.5;
    static constexpr double kForageDeaths = 0.02;
    static constexpr double kArmyUnrest = 0.05;
};

// The cells within `km` of a cell (itself included), by walking neighbours.
void cells_within(const World& world, CellId cell, double km, std::vector<CellId>& out);

// What raiders could carry off from a cell: people in towns plus a twentieth
// of the rest, where settled farmers live (0 for herders and foragers).
double raid_wealth(const SimState& state, CellId cell);

// Each living polity's own fighting strength (levy x quality), by polity id.
std::vector<double> polity_strengths(const World& world, const SimState& state);

// How well a polity's plunder keeps its followers: min(1, plunder / (people
// x kPlunderPerPerson)), 0-1.
[[nodiscard]] inline double plunder_plenty(const Polity& polity, double people) {
    return people > 0.0 ? std::min(1.0, polity.plunder / (people * WarSystem::kPlunderPerPerson)) : 0.0;
}

// A herder chiefdom: no writing, no city at its seat, herds giving at least
// kHerderTribe of its farm food. Nomads take such tribes in as clans.
bool herder_tribe(const World& world, const SimState& state, std::uint32_t polity);

// The polity at the top of a chain of overlords.
std::uint32_t top_overlord(const SimState& state, std::uint32_t polity);

// Whether two polities are in an alliance now / at war now (on opposite sides).
bool allied(const SimState& state, std::uint32_t a, std::uint32_t b);
std::uint32_t war_between(const SimState& state, std::uint32_t a, std::uint32_t b);

// Kill `share` of the people of a cell (counted as deaths in strife).
// Returns how many died.
std::int64_t kill_in_strife(SimState& state, CellId cell, double share, RngStream& rng);

// Start a war (leaders first on each side; allies and overlords are added).
// Returns its id.
std::uint32_t start_war(const World& world, SimState& state, Tick tick, std::uint32_t attacker,
                        std::uint32_t defender, WarGoal goal);

// The living field army at a cell, if any (the largest).
std::uint32_t army_at(const SimState& state, CellId cell);
// A field army's fighting strength (men x the quality of its realm's
// soldiers x its general).
double army_strength(const World& world, const SimState& state, std::uint32_t army);

// "won by the attackers" etc.
const char* war_outcome_name(WarOutcome outcome);
const char* war_goal_name(WarGoal goal);

}  // namespace histsim
