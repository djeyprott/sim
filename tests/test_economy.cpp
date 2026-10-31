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
