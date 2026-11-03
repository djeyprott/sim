// Goods, markets and trade (M3 slice 2).
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include "histsim/simulation.hpp"
#include "histsim/systems/economy.hpp"
#include "histsim/systems/technology.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

std::size_t good_index(const Content& c, const std::string& id) {
    for (std::size_t g = 0; g < c.goods.size(); ++g) {
        if (c.goods[g].id == id) return g;
    }
    throw std::runtime_error("no good " + id);
}

std::size_t technique_index(const Content& c, const std::string& id) {
    for (std::size_t t = 0; t < c.techniques.size(); ++t) {
        if (c.techniques[t].id == id) return t;
    }
    throw std::runtime_error("no technique " + id);
}

const Simulation& traded_run() {
    static const Simulation sim = [] {
        WorldParams p;
        p.grid_level = 4;
        Simulation s = Simulation::create(p);
        s.step_years(1500);
        return s;
    }();
    return sim;
}

}  // namespace

TEST(goods_pack_loads_and_links_to_techniques) {
    const Content& c = default_content();
    CHECK(c.goods.size() >= 10);
    CHECK(c.food_good < c.goods.size());
    CHECK(c.goods[c.food_good].source == GoodSource::Food);
    const GoodDef& bronze = c.goods[good_index(c, "bronze")];
    CHECK(bronze.source == GoodSource::Craft);
    CHECK_EQ(bronze.requires_technique, static_cast<int>(technique_index(c, "bronze")));
    CHECK_EQ(bronze.inputs.size(), std::size_t{2});
    CHECK(c.goods[good_index(c, "salt")].source == GoodSource::Deposit);
    // Smelting needs ore on the market.
    const TechniqueDef& copper = c.techniques[technique_index(c, "copper")];
    CHECK_EQ(copper.requires_goods, 1u << good_index(c, "copper_ore"));
}

TEST(goods_pack_rejects_bad_input) {
    ContentSources src = default_sources();
    auto fails = [&](const char* goods) {
        src.goods = goods;
        try {
            (void)load_content(src);
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };
    // Inputs must be listed earlier; a good needs exactly one source.
    CHECK(fails(R"({"version": 1, "goods": [{"id": "a", "name": "A", "craft": true, "inputs": {"b": 1}}]})"));
    CHECK(fails(R"({"version": 1, "goods": [{"id": "a", "name": "A"}]})"));
    CHECK(fails(R"({"version": 1, "goods": [{"id": "a", "name": "A", "craft": true, "requires_technique": "nope"}]})"));
}

TEST(world_places_deposits_and_trade_links) {
    WorldParams p;
    p.grid_level = 4;
    const World w = generate_test_world(p);
    const Content& c = *w.content;
    const std::size_t n = w.cell_count();
    // Deposits only where their place allows, and rarer goods are rarer.
    std::size_t salt = 0, gold = 0;
    for (CellId cell = 0; cell < n; ++cell) {
        for (std::size_t g = 0; g < c.goods.size(); ++g) {
            if (w.deposit(cell, g) > 0.0) {
                CHECK(c.goods[g].source == GoodSource::Deposit);
                CHECK(good_place_ok(w, cell, c.goods[g].where));
            }
        }
        salt += w.deposit(cell, good_index(c, "salt")) > 0.0;
        gold += w.deposit(cell, good_index(c, "gold")) > 0.0;
    }
    CHECK(gold > 0 && salt > 5 * gold);
    // Links are unique, between land cells, listed under both ends.
    std::set<std::pair<CellId, CellId>> seen;
    std::size_t sea = 0, river = 0;
    for (std::uint32_t e = 0; e < w.trade_edges.size(); ++e) {
        const World::TradeEdge& edge = w.trade_edges[e];
        CHECK(edge.a < edge.b);
        CHECK(w.is_land[edge.a] && w.is_land[edge.b]);
        CHECK(seen.insert({edge.a, edge.b}).second);
        CHECK(edge.distance_km > 0.0);
        if (edge.modes & kTradeSea) {
            ++sea;
            CHECK(edge.distance_km <= 1.2 * kSeaReachKm + 1e-9);
        }
        river += (edge.modes & kTradeRiver) != 0;
        bool at_a = false, at_b = false;
        for (std::uint32_t x : w.trade_links(edge.a)) at_a |= x == e;
        for (std::uint32_t x : w.trade_links(edge.b)) at_b |= x == e;
        CHECK(at_a && at_b);
    }
    CHECK(sea > 0);
    CHECK(river > 0);
    CHECK_EQ(w.trade_edge_ids.size(), 2 * w.trade_edges.size());
}

TEST(carrying_is_cheaper_by_water_and_needs_boats_at_sea) {
    World::TradeEdge land{0, 1, 200.0, 0.0, kTradeLand};
    World::TradeEdge steep{0, 1, 200.0, 1.0, kTradeLand};
    World::TradeEdge river{0, 1, 200.0, 0.0, static_cast<std::uint8_t>(kTradeLand | kTradeRiver)};
    World::TradeEdge sea{0, 1, 200.0, 0.0, kTradeSea};
    const World w;
    const TechEffects none;
    CHECK_NEAR(carry_cost(w, land, none), 2.0 * EconomySystem::kLandCost, 1e-12);
    CHECK(carry_cost(w, steep, none) > carry_cost(w, land, none));
    CHECK(carry_cost(w, river, none) < carry_cost(w, land, none));
    CHECK(carry_cost(w, sea, none) == std::numeric_limits<double>::infinity());  // no boats
    TechEffects canoes;
    canoes.water_transport = 0.25;
    TechEffects sails;
    sails.water_transport = 1.5;
    CHECK(carry_cost(w, sea, canoes) < std::numeric_limits<double>::infinity());
    CHECK(carry_cost(w, sea, sails) < carry_cost(w, sea, canoes));
    TechEffects carts;
    carts.land_transport = 0.4;
    CHECK(carry_cost(w, land, carts) < carry_cost(w, land, none));
}

TEST(bronze_needs_copper_and_tin_on_the_market) {
    WorldParams p;
    p.grid_level = 3;
    const Simulation sim = Simulation::create(p);
    const World& w = sim.world();
    const Content& c = *w.content;
    const TechniqueDef& bronze = c.techniques[technique_index(c, "bronze")];
    CellId cell = 0;
    while (!w.is_land[cell]) ++cell;
    Keeps kept;
    kept.crops = kept.herds = true;
    kept.town = 1e9;
    const std::uint64_t copper = std::uint64_t{1} << technique_index(c, "copper");
    kept.goods = ~0u;
    CHECK(technique_usable(w, bronze, cell, copper, 0, kept));
    kept.goods = 1u << good_index(c, "copper");  // no tin
    CHECK(!technique_usable(w, bronze, cell, copper, 0, kept));
    kept.goods |= 1u << good_index(c, "tin_ore");
    CHECK(technique_usable(w, bronze, cell, copper, 0, kept));
}

TEST(markets_trade_goods_beyond_their_deposits) {
    const Simulation& sim = traded_run();
    const World& w = sim.world();
    const SimState& s = sim.state();
    const Content& c = *w.content;
    const std::size_t ng = c.goods.size();
    const std::size_t ore = good_index(c, "copper_ore");
    std::size_t markets = 0, carried_in = 0;
    double food_balance = 0.0, food_moved = 0.0;
    for (CellId cell = 0; cell < w.cell_count(); ++cell) {
        food_balance += s.food_import[cell];
        food_moved += std::fabs(s.food_import[cell]);
        if (s.cell_population[cell] == 0) {
            CHECK_EQ(s.goods_available[cell], 0u);
            continue;
        }
        ++markets;
        if (w.deposit(cell, ore) == 0.0 && ((s.goods_available[cell] >> ore) & 1u)) ++carried_in;
        for (std::size_t g = 0; g < ng; ++g) {
            const double ratio = s.prices[cell * ng + g] / c.goods[g].base_price;
            CHECK(ratio >= std::sqrt(EconomySystem::kMinRatio) - 1e-9);
            CHECK(ratio <= std::sqrt(EconomySystem::kMaxRatio) + 1e-9);
            CHECK(s.stocks[cell * ng + g] >= 0.0);
        }
    }
    // Copper ore is dug in a few places but on sale in many more.
    CHECK(carried_in > markets / 5);
    // Food sent equals food received.
    CHECK(std::fabs(food_balance) <= 1e-9 * (1.0 + food_moved));
    CHECK(s.history.back().trade_value > 0.0);
}

TEST(busy_links_become_named_trade_routes) {
    const Simulation& sim = traded_run();
    const auto routes = trade_routes(sim.world(), sim.state());
    CHECK(!routes.empty());
    const double threshold = route_threshold(sim.state());
    for (std::size_t i = 0; i < routes.size(); ++i) {
        const TradeRoute& r = routes[i];
        CHECK(!r.name.empty());
        CHECK(r.edges.size() >= 2);
        CHECK(r.cells.size() >= 3);
        for (std::uint32_t e : r.edges) CHECK(sim.state().route_value[e] >= threshold);
        if (i > 0) CHECK(routes[i - 1].value >= r.value);
    }
}

TEST(food_stores_fill_in_good_years_and_feed_people_in_lean_ones) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    SimState s = sim.state();
    const World& w = sim.world();
    CellId c = 0;
    while (s.cell_population[c] < 1000) ++c;
    const double people = static_cast<double>(s.cell_population[c]);
    EconomySystem economy;
    // A good year: the land feeds twice its people; some of the surplus is
    // put by, no more than the room there is.
    s.env.capacity[c] = 2.0 * people;
    StepContext good{w, s, 5, s.tick};
    economy.step(good);
    const double room = people * (EconomySystem::kStoreYears + EconomySystem::kStoreYearsPerBuffer *
                                                                   technique_effects(w, c, techniques_in(s, c)).harvest_buffer);
    CHECK(s.env.food_store[c] > 0.0);
    CHECK(s.env.food_store[c] <= room + 1e-9);
    // A lean year: the store feeds those the harvest does not.
    s.env.food_store[c] = 0.3 * people;
    s.env.capacity[c] = 0.5 * people;
    StepContext lean{w, s, 5, s.tick};
    economy.step(lean);
    const double fed = s.env.capacity[c] - s.food_import[c] - 0.5 * people;
    CHECK_NEAR(fed, 0.3 * people, 1e-6 * people);  // all of it: the gap was larger
    CHECK(s.env.food_store[c] < 1e-9 * people);
}

namespace {

// A level-4 world, its state, and an inhabited cell whose neighbours are
// all inhabited land.
struct GrainSetup {
    Simulation sim;
    SimState s;
    CellId a = kInvalidCell;
};

GrainSetup grain_setup() {
    WorldParams p;
    p.grid_level = 4;
    GrainSetup x{Simulation::create(p), {}, kInvalidCell};
    x.sim.step_years(1);  // markets and stocks in place
    x.s = x.sim.state();
    const World& w = x.sim.world();
    for (CellId c = 0; c < w.cell_count() && x.a == kInvalidCell; ++c) {
        if (x.s.cell_population[c] < 1000) continue;
        bool ok = true;
        for (CellId nb : w.grid.neighbors(c)) ok = ok && x.s.cell_population[nb] >= 1000;
        if (ok) x.a = c;
    }
    CHECK(x.a != kInvalidCell);
    return x;
}

void run_economy(const World& w, SimState& s) {
    EconomySystem economy;
    StepContext ctx{w, s, 5, s.tick};
    economy.step(ctx);
}

}  // namespace

TEST(grain_goes_from_full_stores_to_hungry_neighbours) {
    GrainSetup x = grain_setup();
    SimState& s = x.s;
    const World& w = x.sim.world();
    const double people = static_cast<double>(s.cell_population[x.a]);
    s.env.food_store.assign(w.cell_count(), 0.0);
    s.env.food_store[x.a] = 10.0 * people;
    s.env.capacity[x.a] = 2.0 * people;  // a good year here
    // Famine around (food dear enough to pay for hauling grain that far).
    for (CellId nb : w.grid.neighbors(x.a)) s.env.capacity[nb] = 0.05 * static_cast<double>(s.cell_population[nb]);
    double around = 0.0;
    for (CellId nb : w.grid.neighbors(x.a)) around += s.env.capacity[nb];
    run_economy(w, s);
    // Sold: much less left than spoilage alone would leave.
    CHECK(s.env.food_store[x.a] < 0.8 * (1.0 - EconomySystem::kStoreSpoilage) * 10.0 * people);
    // And the hungry neighbours ate it (more food than harvest and imports).
    double fed = 0.0;
    for (CellId nb : w.grid.neighbors(x.a)) fed += s.env.capacity[nb];
    CHECK(fed > around);
}

TEST(grain_war_cuts_trade_across_the_front) {
    GrainSetup x = grain_setup();
    const World& w = x.sim.world();
    // Polity 1 holds the cell, polity 2 everything around it.
    auto rule = [&](SimState& s) {
        s.polities.assign(3, Polity{});
        for (std::uint32_t id = 1; id <= 2; ++id) s.polities[id].id = id;
        s.polities[1].capital = x.a;
        s.polity_of.assign(w.cell_count(), 0);
        s.polity_of[x.a] = 1;
        for (CellId nb : w.grid.neighbors(x.a)) s.polity_of[nb] = 2;
        s.polities[2].capital = w.grid.neighbors(x.a).front();
    };
    SimState peace = x.s;
    rule(peace);
    run_economy(w, peace);
    CHECK(peace.trade_value[x.a] > 0.0);
    SimState war = x.s;
    rule(war);
    War fight;
    fight.id = static_cast<std::uint32_t>(war.wars.size());
    fight.attackers = {1};
    fight.defenders = {2};
    war.wars.push_back(fight);
    run_economy(w, war);
    CHECK_EQ(war.trade_value[x.a], 0.0);
    CHECK_EQ(war.food_import[x.a], 0.0);
}

TEST(grain_literate_states_feed_hungry_provinces_from_their_granaries) {
    GrainSetup x = grain_setup();
    const World& w = x.sim.world();
    const CellId province = w.grid.neighbors(x.a).front();
    auto setup = [&](bool literate) {
        SimState s = x.s;
        s.polities.assign(2, Polity{});
        s.polities[1].id = 1;
        s.polities[1].capital = x.a;
        s.polity_of.assign(w.cell_count(), 0);
        s.polity_of[x.a] = 1;
        s.polity_of[province] = 1;
        std::uint64_t writing = 0;
        for (std::size_t k = 0; k < w.content->techniques.size(); ++k) {
            if (w.content->techniques[k].id == "writing") writing = std::uint64_t{1} << k;
        }
        if (literate) {
            s.techniques[x.a] |= writing;
        } else {
            s.techniques[x.a] &= ~writing;
        }
        s.env.food_store.assign(w.cell_count(), 0.0);
        s.env.food_store[x.a] = 5.0 * static_cast<double>(s.cell_population[x.a]);
        s.env.capacity[province] = 0.2 * static_cast<double>(s.cell_population[province]);
        return s;
    };
    SimState with = setup(true), without = setup(false);
    const double base = with.env.capacity[province];
    run_economy(w, with);
    run_economy(w, without);
    const double fed_with = with.env.capacity[province] - with.food_import[province] - base;
    const double fed_without = without.env.capacity[province] - without.food_import[province] - base;
    CHECK(fed_with > fed_without);  // the state's granaries fed it more
}
