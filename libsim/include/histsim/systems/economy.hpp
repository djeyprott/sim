// Goods, markets and trade (M3).
//
// Every inhabited cell is a market. Each year its people dig and gather what
// its deposits hold (salt, flint and obsidian, ores, gold, timber, fish),
// raise livestock, and make crafts (pottery, cloth, copper, bronze) with the
// techniques they know and the inputs in stock; everyone wants some goods,
// and farmers, townspeople and elites want more (data/goods.json). A good's
// local price follows scarcity: base x sqrt(demand / supply), from 1/5 to 5
// times its base. Production leans toward goods that sell well.
//
// Merchants carry goods along trade links (World::trade_edges): overland,
// cheaper down rivers, and by sea between nearby coasts for people with boats.
// Carrying costs per unit of weight per 100 km: kLandCost (more over steep
// ground; less with horses, carts and roads), kRiverCost and kSeaCost (less
// with sails). Each year every market sends part of what it can spare (stock
// beyond its own needs) to neighbours where the good sells for more than here
// plus the carrying, more to the better buyers. A market that neither makes
// nor wants a good prices it at its base, between the glutted market that
// makes it and the starved one that wants it, so goods pass through on their
// way: a link a year, across a continent over decades, as obsidian and tin
// did. Quantities limit it: a hungry village does not draw a continent's salt.
//
// War cuts trade: no goods, food or grain cross a link between polities on
// opposite sides of a war.
//
// Food is bulky and spoils, so it moves only between neighbours: from cells
// with food to spare to hungry ones and to towns (whose people buy their
// food and outbid farmers), when the price difference pays for the carrying. Food brought in feeds people (it adds to the capacity this year)
// and lets towns grow past what their own fields feed.
//
// What is on a cell's market gates techniques that need materials: copper
// smelting needs copper ore, bronze needs copper and tin. Links that carry a
// lot of value are trade routes (trade_routes()).
#pragma once

#include <string>
#include <vector>

#include "histsim/scheduler.hpp"

namespace histsim {

class EconomySystem final : public System {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "economy"; }
    [[nodiscard]] Phase phase() const noexcept override { return Phase::Markets; }
    [[nodiscard]] Tick period() const noexcept override { return kTicksPerYear; }
    void step(StepContext& ctx) override;

    // Carrying cost of a unit of weight 1 (a person-year of grain) per 100 km,
    // in food prices.
    static constexpr double kLandCost = 0.3;
    static constexpr double kClimbCost = 2.0;  // overland cost x (1 + kClimbCost x height difference in km)
    static constexpr double kRiverCost = 0.1;
    static constexpr double kSeaCost = 0.15;
    // Price is base x sqrt(demand / supply), the ratio kept within these.
    static constexpr double kMinRatio = 0.04;
    static constexpr double kMaxRatio = 25.0;
    // Share of what a market can spare (stock beyond its own needs) sent on
    // each year, split among the buyers by how much more they pay.
    static constexpr double kTradeShare = 0.5;
    // Townspeople buy their food and outbid farmers for it: the food price
    // is base x sqrt(people x (1 + this x town share) / capacity).
    static constexpr double kTownFoodPremium = 2.0;
    // Food: carried = kFoodShare x price gap / price sum x the smaller
    // capacity, at most kMaxFoodExport of the sender's capacity in all.
    static constexpr double kFoodShare = 0.5;
    static constexpr double kMaxFoodExport = 0.25;
    // Food stores, after this year's harvest and food trade (in people fed
    // for a year). A good year's surplus (what the land feeds beyond its
    // people) is put by: kStoreShare of it, x (kForagerStore + (1 -
    // kForagerStore) x farming share), as foragers keep little. A store
    // holds at most (kStoreYears + kStoreYearsPerBuffer x storage) years of
    // food for the cell's people, storage being the harvest_buffer of its
    // techniques (pottery, granaries, calendars, scribes' accounts). In a
    // lean year the store is drawn on to feed those the harvest does not
    // (adding to this year's capacity); famine comes when it is empty. It
    // spoils by kStoreSpoilage a year; an empty cell's store is lost.
    static constexpr double kStoreShare = 0.7;
    static constexpr double kForagerStore = 0.1;
    static constexpr double kStoreYears = 0.25;
    static constexpr double kStoreYearsPerBuffer = 3.0;
    static constexpr double kStoreSpoilage = 0.1;
    // Grain trade: stored food beyond kGrainReserve years of a cell's own
    // needs is for sale. Each year kGrainSale of it goes to neighbouring
    // markets where food sells for more than here plus the carrying (food
    // weighs 1), split by that gain, into their stores: it feeds them if
    // they are hungry, and passes on a link a year if they are not. Cheap by
    // river and sea, so a wet coast can feed a dry one.
    static constexpr double kGrainReserve = 0.5;
    static constexpr double kGrainSale = 0.5;
    // State granaries: a polity with writing moves stored grain from its
    // full granaries (beyond kGrainReserve) to its hungry provinces (people
    // beyond what this year's harvest and trade feed, after their own
    // stores), anywhere it rules: up to kRelief of what is spare, or
    // kReliefBureaucracy with a scribal bureaucracy (the Chinese and
    // Egyptian granary systems).
    static constexpr double kRelief = 0.3;
    static constexpr double kReliefBureaucracy = 0.7;
    // Elites: this share of people at hierarchy 0.5, twice as many under a
    // polity.
    static constexpr double kEliteShare = 0.02;
    // Stocks beyond this many years of demand, production and imports are
    // not kept.
    static constexpr double kMaxStockYears = 5.0;
    // A good is on the market when at least kAvailable + kAvailablePerPerson
    // x people of it is there (a trickle is not enough to work with).
    static constexpr double kAvailable = 0.01;
    static constexpr double kAvailablePerPerson = 1.0e-5;
    // Route value smoothing (share of the gap closed a year).
    static constexpr double kRouteSmoothing = 0.1;
};

// Cost to carry a unit of weight 1 along a link, for a sender with these
// technique effects (infinite if the link cannot be used: by sea without
// boats).
double carry_cost(const World& world, const World::TradeEdge& edge, const TechEffects& sender);

// A trade route: connected links each among the busiest kRouteShare of links
// that carry anything and carrying at least kRouteMinValue a year (or
// `min_value`, if given), at least two links long, named after the largest
// city on it.
struct TradeRoute {
    std::string name;
    std::vector<std::uint32_t> edges;
    std::vector<CellId> cells;
    double value = 0.0;  // carried a year, summed over its links
    std::uint32_t city = 0;  // the largest city on it (0 = none)
    std::uint8_t modes = 0;  // most of its value goes by this mode (kTradeLand, kTradeRiver or kTradeSea)
};
inline constexpr double kRouteMinValue = 200.0;
inline constexpr double kRouteShare = 0.05;
std::vector<TradeRoute> trade_routes(const World& world, const SimState& state, double min_value = -1.0);

// The value a link must carry a year to be part of a trade route now.
double route_threshold(const SimState& state);

}  // namespace histsim
