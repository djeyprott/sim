// People systems: cohort demography and migration (M1 foragers).
#pragma once

#include "histsim/scheduler.hpp"

namespace histsim {

// Yearly births, deaths and aging for every cohort.
//
// Mortality follows the average hunter-gatherer life table (Gurven & Kaplan
// 2007; life expectancy 31 at birth, 38 more at 15) and fertility a natural-
// fertility age pattern with total fertility 5.4 (Marlowe 2005). Both come
// from tools/vital_rates.py. Crowding links them to the land: as a cell fills
// toward its foraging capacity, fertility falls to replacement (poorer
// nutrition, longer birth spacing); beyond capacity, famine raises mortality.
// So populations grow ~0.7% a year on empty land and settle near capacity.
//
// "Replacement" is worked out for each cell's actual death rates (the life
// table, towns and disease, but not famine, which is what pushes back past
// capacity): where disease kills more children, women bear more, as in
// historical high-mortality regimes, so disease shortens lives without
// leaving farmland empty. Fertility = replacement x (1 + headroom x (1 -
// crowding)), headroom such that on empty land foragers bear at the life
// table's full rate and farmers 25% more (farming.hpp): farmers grow faster
// into empty land but, like foragers, just replace themselves at capacity.
class DemographySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "demography"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Demography; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    static constexpr double kFamineMortalityPerCrowding = 2.5;  // mortality x(1 + 2.5 (crowding - 1))
    static constexpr double kMaxMortalityMultiplier = 6.0;
    static constexpr double kMinFertilityMultiplier = 0.2;
    // Women can only bear so many children: replacement is made up to at
    // most kMaxReplacement x the life table's fertility (TFR ~7), and
    // fertility never exceeds kMaxFertilityMultiplier (TFR ~8). Beyond that,
    // death outruns birth and disease does thin a people.
    static constexpr double kMaxReplacement = 1.3;
    static constexpr double kMaxFertilityMultiplier = 1.5;
};

// Yearly moves, as push and pull (design: "Conditions and migration
// design"). People weigh what they live through at home (the conditions:
// SimState::conditions) against what they expect elsewhere, less the cost of
// getting there:
//
//   push(food, danger, burden, sickness) = hunger x (1 - food) when food is
//     short (kPlenty x (1 - food), negative, when there is more than
//     enough: plenty holds people and draws them) + kDangerPush x danger
//     + kBurdenPush x burden + kSicknessPush x sickness
//   gain of a place = push at home - push expected there (its food per head
//     with one more household, its own danger, burden and sickness)
//   cost = kMoveCost per 100 km x (1 + kClimbCostPerKm x climb in km),
//     x kSeaCost by sea
//   want(x) = x / (x + kHalfPush) for x > 0: the share who would go
//
// Each year, from every cell, want(best gain - cost) x kMaxLeave of each
// group go to its neighbours, chosen by gain - cost (plus a little
// exchange, kExploratoryRate, of young people marrying out); a cell with
// nothing to eat empties. Each cohort's movers travel together, as a band.
// Young adults move most. Farmers weigh land their crops could fill a
// level-5 spacing away (colonists, below). A people that most would leave
// (want(push x mobility) of at least kBodyShare, with nowhere better next
// door) goes in a body to open land further off: a folk migration.
//
// Afterwards, groups smaller than 20 people or 10% of their cell join the
// cell's largest group, so arrivals into settled land are absorbed while
// bands entering empty land carry their group with them.
class MigrationSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "migration"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Migration; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Push weights, per unit of each condition (food per head below need,
    // deaths in war, share of the harvest taken, deaths from disease), set
    // so that a famine with food at 0.7 of need, a sack killing 3% or a
    // plague killing 10% each bring half the people to want to leave (push
    // = kHalfPush), and a fifth of the harvest taken a quarter of them.
    static constexpr double kHungerPush = 3.3;
    static constexpr double kPlenty = 0.1;  // pull of food beyond need, per unit
    static constexpr double kDangerPush = 33.0;
    static constexpr double kBurdenPush = 1.7;
    static constexpr double kSicknessPush = 10.0;
    static constexpr double kHalfPush = 1.0;
    // At most this share of a group a year moves to the land next door (for
    // level-5 cells; more for finer grids, as many km a year), at full want:
    // some 5% a year left Ireland in the famine years.
    static constexpr double kMaxLeave = 0.1;
    static constexpr double kMoveCost = 0.02;  // per 100 km
    static constexpr double kSeaCost = 2.0;
    // A group goes in a body (a folk migration) where at least this share
    // would leave.
    static constexpr double kBodyShare = 0.5;
    // Young people marrying into the next band or village, a year: an
    // exchange that changes no numbers, but carries genes, words and ways.
    static constexpr double kExploratoryRate = 0.005;
    // Exchange goes to neighbours alike (this weight) beside where gain leads.
    static constexpr double kExchangeWeight = 0.05;
    // On finer grids the rates scale up (see World::per_step), to
    // at most this share a year.
    static constexpr double kMaxGridRate = 0.5;
    // Strength of mobility's effect (see mobility_factor) on wandering (only
    // ever more, for the more mobile) and on folk migrations.
    static constexpr double kMobileWander = 0.5;
    static constexpr double kMobileExodus = 0.25;
    static constexpr double kClimbCostPerKm = 2.0;  // a 500 m climb doubles the cost
    static constexpr std::int64_t kMinGroupSize = 20;
    static constexpr double kMinGroupShare = 0.10;
    // ...unless they farm (by this much more of their food) and the largest
    // group does not: colonists among foragers keep their own ways.
    static constexpr double kKeepApartFarming = 0.5;

    // Colonists. Farmers (or herders) weigh the land their crops and herds
    // could fill a level-5 spacing away (by sea too, for people with boats):
    // the food per head it would give them against their own, as above.
    // Where the gain is worth the journey, want(gain - cost) x kMaxLeave of
    // them a year go, as one group of the cell's largest farming people,
    // keeping their language and customs, and bring their crops: the farming
    // dispersals that spread a few language families over continents.
    static constexpr double kColonizeFarming = 0.5;
    // Cells look for new land every kColonizeYears (in turn), sending that
    // many years' colonists at once.
    static constexpr std::int64_t kColonizeYears = 5;
    // A colony is at least this share of the people already there (a band
    // too small would simply be absorbed), but at most kColonizeMaxShare of
    // the people it leaves (in one five-yearly party).
    static constexpr double kColonyMinShare = 0.15;
    static constexpr double kColonizeMaxShare = 0.15;

    // Folk migrations: a people leaving in a body. Each year in a cell of at
    // least kFolkMinPeople, where its largest people would mostly leave
    // (want(push x how readily they move) of at least kBodyShare), that
    // share of them, at most kMaxExodusShare, go to the most open land within
    // kExodusKm (spare capacity, less with distance; out of the realm that
    // took their land in war). The cause recorded is the largest part of
    // the push: hunger, danger (war or raid), burden or sickness. Arrivals
    // crowd those they join, whose own push follows next year: displacement
    // chains need no rule of their own. A people that leaves in hunger
    // within kChainYears of newcomers' arrival is counted as pushed out by
    // them.
    static constexpr std::int64_t kFolkMinPeople = 500;
    static constexpr double kMaxExodusShare = 0.6;
    static constexpr double kExodusKm = 700.0;
    static constexpr double kExodusDistanceKm = 300.0;  // appeal / (1 + distance / this)
    // A horde taking land drives off locals as they are outnumbered:
    // kDisplace x newcomers / locals, at most kMaxDisplace.
    static constexpr double kDisplace = 0.15;
    static constexpr double kMaxDisplace = 0.4;
    static constexpr std::int64_t kChainYears = 30;
    // Wandering peoples: a tribe or confederation (a polity without writing;
    // literate states stay put) of at least kWanderMinPeople moves as a whole
    // when its people would mostly leave: want(push over its lands, from their
    // 20-year conditions, x how readily its people move: mobility_factor,
    // strength kWanderMobility, x0.3 for settled farmers to x1.15 for
    // nomads) of at least kBodyShare, the push raised by its war shock and a
    // stronger neighbour (a realm seated within kWanderThreatKm with more
    // than kWanderThreat times its people, a full kHalfPush at kWanderThreat
    // + kWanderThreatSpan times); at most once every kWanderYears. Peoples
    // more mobile than
    // kWanderRoamMobility also roam unpressed, with chance kWanderRoam a
    // year at full mobility (herders moving on to pastures new). It goes as far as kWanderKm +
    // kWanderKmMobile x mobility to the most open land (pasture for herders)
    // not held by anyone at least kWanderStronger times its people, takes as
    // many cells around it as it held, and kWanderShare of its people (the
    // rulers' lineage) move there with their crops, rulers, name and faith.
    // They go step by step: each year a step toward the goal with chance
    // (kTrekKm + kTrekKmMobile x mobility) km / the cell spacing, keeping as
    // much land around them as they held; kTrekShare of them go on at each
    // step (the rest settle along the way), and the land they leave is free.
    // A trek ends at its goal, or where a stronger people blocks the way.
    // Its wars end in its flight (lost where it led a side). The land left
    // behind is free; the locals where they arrive are pushed
    // on (as in chain reactions, by how outnumbered they are) and their
    // rulers shaken; the rest stay and live under the newcomers.
    static constexpr std::int64_t kWanderMinPeople = 5000;
    static constexpr double kWanderMobility = 0.75;
    static constexpr double kWanderKm = 600.0;
    static constexpr double kWanderKmMobile = 1400.0;
    static constexpr double kWanderShare = 0.6;
    static constexpr std::int64_t kWanderYears = 50;
    static constexpr double kWanderStronger = 0.5;
    static constexpr double kWanderShock = 0.3;
    static constexpr double kWanderDistanceKm = 1000.0;  // appeal / (1 + distance / this): a whole people looks far
    static constexpr double kTrekKm = 30.0;
    static constexpr double kTrekKmMobile = 120.0;
    static constexpr double kTrekShare = 0.9;
    // Who goes on a wandering: of the rulers' people share + (1 - share) x
    // kMobileTrek x mobility (nearly all of a herding people); of the realm's
    // other peoples, those at least kFollowMobility mobile (herders under the
    // horde), share x mobility.
    static constexpr double kMobileTrek = 0.7;
    static constexpr double kFollowMobility = 0.6;
    static constexpr double kLevel5SpacingKm = 225.0;
    // A step moves no more of the people into a cell than fill it to
    // (kTrekRoom + kTrekRoomMobile x mobility) x what it feeds (with those
    // already there: a horde crowds in on the locals and drives them off);
    // the rest stay behind where they were, as real migrations shed people
    // on the way.
    static constexpr double kTrekRoom = 1.1;
    static constexpr double kTrekRoomMobile = 1.0;
    // The march kills: kMarchDeaths of those who take a step die on the way,
    // x kMarchHarsh into desert (half that into dry scrub or tundra), x
    // kMarchHostile into land held by another people (fighting through).
    static constexpr double kMarchDeaths = 0.03;
    static constexpr double kMarchHarsh = 3.0;
    static constexpr double kMarchHostile = 2.0;
    static constexpr double kWanderThreatKm = 600.0;
    static constexpr double kWanderThreat = 5.0;
    static constexpr double kWanderRoam = 0.006;
    static constexpr double kWanderRoamMobility = 0.4;
    static constexpr double kWanderThreatSpan = 6.0;  // to the rulers of land the newcomers take
    // Herders follow their horde: when a nomad confederation takes land in
    // war, next year kSettleShare x mobility of the largest group of its own
    // people in the cell it struck from move into it (filling it to at most
    // (kTrekRoom + kTrekRoomMobile x mobility) x what it feeds).
    static constexpr double kSettleShare = 0.3;
    // Folk migrations of at least this many people are recorded.
    static constexpr std::int64_t kFolkRecordPeople = 1000;
};

// Appends one YearRecord per year (last in the tick).
class HistorySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "history"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Events; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;
};

// Life expectancy at birth and the net reproduction rate (daughters per
// woman at full fertility) under a cell's death rates this year: the life
// table, raised in towns, plus disease (famine excluded).
struct LifeTable {
    double life_expectancy = 0.0;
    double net_reproduction = 0.0;
};
LifeTable cell_life_table(const SimState& state, CellId cell);
// Fertility multiplier that just replaces the people under those death rates.
double cell_replacement_fertility(const SimState& state, CellId cell);

// A cohort of n people in the stationary age structure of the life table,
// split between the sexes by the sex ratio at birth. Integer-exact.
Cohort make_stable_cohort(CellId cell, std::uint32_t culture, std::uint32_t language, std::uint32_t n);

// Life-table helpers exposed for tests and the inspector.
double forager_death_probability(int age_bin);
double forager_fertility(int age_bin);
double stable_age_share(int age_bin);
double female_share_of_births();
double replacement_fertility_multiplier();

}  // namespace histsim
