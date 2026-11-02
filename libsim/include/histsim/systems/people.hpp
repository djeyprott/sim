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

// Yearly moves between neighboring cells.
//
// Some people move every year (0.5%: bands splitting off to explore); crowded
// cells send more (4.5% a year at capacity, rising with the square of
// crowding); a cell with nothing to eat empties. Each cohort's
// movers travel together, as a band, to one neighbor chosen by spare
// capacity and ease of travel (elevation change). Young adults move most.
// Farmers who outgrow their land also send colonists to settle land their
// crops could fill, and peoples struck by famine, war or raids leave in a body
// for open land further off, sometimes pushing others on (see below).
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

    static constexpr double kExploratoryRate = 0.005;
    static constexpr double kRateAtCapacity = 0.04;
    static constexpr double kMaxRate = 0.15;
    // On finer grids the rates scale up (see World::per_step_diffusion), to
    // at most this share a year.
    static constexpr double kMaxGridRate = 0.5;
    // Strength of mobility's effect (see mobility_factor) on wandering (only
    // ever more, for the more mobile) and on folk migrations.
    static constexpr double kMobileWander = 0.5;
    static constexpr double kMobileExodus = 0.25;
    static constexpr double kClimbCostPerKm = 2.0;  // a 500 m climb halves a destination's appeal
    static constexpr std::int64_t kMinGroupSize = 20;
    static constexpr double kMinGroupShare = 0.10;
    // ...unless they farm (by this much more of their food) and the largest
    // group does not: colonists among foragers keep their own ways.
    static constexpr double kKeepApartFarming = 0.5;

    // Colonists. Farmers (or herders) at least kColonizeCrowding of their
    // land's capacity send kColonizeRate of their people a year (scaled up to
    // that as crowding rises by kColonizeRamp more) to the neighbouring land
    // their crops and herds could fill best, where at least kColonizeRoom of
    // what it could feed is still free; by sea too, for people with boats.
    // They go as one group of the cell's largest farming people, keeping
    // their language and customs, and bring their crops: the farming
    // dispersals that spread a few language families over continents.
    static constexpr double kColonizeFarming = 0.5;
    static constexpr double kColonizeCrowding = 0.75;
    static constexpr double kColonizeRamp = 0.3;
    static constexpr double kColonizeRate = 0.03;
    // Cells look for new land every kColonizeYears (in turn), sending that
    // many years' colonists at once.
    static constexpr std::int64_t kColonizeYears = 5;
    // A colony is at least this share of the people already there (a band
    // too small would simply be absorbed), but at most kColonizeMaxShare of
    // the people it leaves (in one five-yearly party).
    static constexpr double kColonyMinShare = 0.15;
    static constexpr double kColonizeMaxShare = 0.15;
    static constexpr double kColonizeRoom = 0.3;
    static constexpr double kSeaColonyAppeal = 0.5;  // a sea crossing halves a destination's appeal

    // Folk migrations: a people leaving in a body. Each year a cell of at
    // least kFolkMinPeople whose land was taken in war (chance kExodusWar),
    // raided by nomads (kExodusRaid) or struck by a dearth (below) sends
    // kExodusShare of its largest people to the most open land within
    // kExodusKm (spare capacity, less with distance). Arriving at least
    // kDisplaceRatio as many as the largest people there, and no less
    // warlike, they push some of them on in turn (chain reactions): kDisplace
    // x newcomers / locals, at most kMaxDisplace. Outnumbered locals flee;
    // where the newcomers are few, nearly all stay and live under them.
    static constexpr std::int64_t kFolkMinPeople = 500;
    static constexpr double kExodusWar = 0.3;
    static constexpr double kExodusRaid = 0.1;
    // Famine: when the land has lately fed less than it usually does (capacity
    // smoothed by kRecentRate a year, below kDearth of capacity smoothed by
    // kUsualRate) and the people are beyond what it feeds now, chance
    // kExodusFamine x how far the dearth goes past kDearth, over kDearthSpan.
    // One bad harvest is weathered; a run of them, as a climate shift (Bond
    // events, eruptions, droughts) begins, sets peoples moving.
    static constexpr double kExodusFamine = 0.3;
    static constexpr double kDearth = 0.85;
    static constexpr double kDearthSpan = 0.25;
    static constexpr double kRecentRate = 0.3;
    static constexpr double kUsualRate = 0.02;
    static constexpr double kExodusShare = 0.25;
    static constexpr double kExodusKm = 700.0;
    static constexpr double kExodusDistanceKm = 300.0;  // appeal / (1 + distance / this)
    static constexpr double kDisplaceRatio = 0.3;
    static constexpr double kDisplace = 0.15;
    static constexpr double kMaxDisplace = 0.4;
    // A displaced people may push on others in turn, up to this many links
    // in one year; and a people that leaves in famine within kChainYears of
    // newcomers' arrival is counted as pushed out by them.
    static constexpr int kMaxChainLinks = 5;
    static constexpr std::int64_t kChainYears = 30;
    // Wandering peoples: a tribe or confederation (a polity without writing;
    // literate states stay put) of at least kWanderMinPeople moves as a whole
    // when hard pressed: pressure = the largest of its war shock, its famine,
    // a lasting dearth on its land, land hunger (people beyond what the
    // land feeds) and a stronger neighbour (a realm seated within
    // kWanderThreatKm with more than kWanderThreat times its people, full at
    // kWanderThreat + kWanderThreatSpan times); at kWanderPressure or more it sets out with chance kWander
    // x pressure x how readily its people move (mobility_factor, strength
    // kWanderMobility: x0.3 for settled farmers to x1.15 for nomads) a year,
    // at most once every kWanderYears. Peoples more mobile than
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
    static constexpr double kWander = 0.03;
    static constexpr double kWanderPressure = 0.3;
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
    static constexpr double kLevel5SpacingKm = 225.0;
    static constexpr double kWanderThreatKm = 600.0;
    static constexpr double kWanderThreat = 5.0;
    static constexpr double kWanderRoam = 0.006;
    static constexpr double kWanderRoamMobility = 0.4;
    static constexpr double kWanderThreatSpan = 6.0;  // to the rulers of land the newcomers take
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
