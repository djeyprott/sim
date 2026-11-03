#include "histsim/systems/economy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

#include "histsim/systems/polity.hpp"
#include "histsim/systems/technology.hpp"

namespace histsim {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

std::size_t trait_index(const Content& c, const char* id) {
    for (std::size_t t = 0; t < c.traits.size(); ++t) {
        if (c.traits[t].id == id) return t;
    }
    return c.traits.size();
}

// Cost of each mode a link offers for a sender (infinite if not offered).
struct ModeCosts {
    double land = kInf, river = kInf, sea = kInf;
};

ModeCosts mode_costs(const World::TradeEdge& e, const TechEffects& from) {
    ModeCosts m;
    const double hundreds = e.distance_km / 100.0;
    const double water = 1.0 / (1.0 + std::max(0.0, from.water_transport));
    if (e.modes & kTradeLand) {
        m.land = EconomySystem::kLandCost * (1.0 + EconomySystem::kClimbCost * e.climb) /
                 (1.0 + std::max(0.0, from.land_transport)) * hundreds;
    }
    if (e.modes & kTradeRiver) m.river = EconomySystem::kRiverCost * water * hundreds;
    if ((e.modes & kTradeSea) && from.water_transport > 0.0) m.sea = EconomySystem::kSeaCost * water * hundreds;
    return m;
}

double price_ratio(double demand, double supply) {
    return std::sqrt(std::clamp((demand + 0.01) / (supply + 0.01), EconomySystem::kMinRatio, EconomySystem::kMaxRatio));
}

}  // namespace

double carry_cost(const World&, const World::TradeEdge& e, const TechEffects& from) {
    const ModeCosts m = mode_costs(e, from);
    return std::min({m.land, m.river, m.sea});
}

void EconomySystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    const std::size_t ng = content.goods.size();
    const std::size_t ne = w.trade_edges.size();
    if (ng == 0) return;
    if (s.stocks.size() != n * ng) s.stocks.assign(n * ng, 0.0);
    if (s.prices.size() != n * ng) {
        s.prices.assign(n * ng, 0.0);
        for (CellId c = 0; c < n; ++c) {
            for (std::size_t g = 0; g < ng; ++g) s.prices[c * ng + g] = content.goods[g].base_price;
        }
    }
    s.goods_available.assign(n, 0);
    s.food_import.assign(n, 0.0);
    s.trade_value.assign(n, 0.0);
    if (s.route_value.size() != ne) s.route_value.assign(ne, 0.0);
    const std::size_t food = content.food_good;
    const std::size_t i_hierarchy = trait_index(content, "hierarchy");

    // Who keeps livestock: herd animals that feed people (not dogs).
    std::uint32_t livestock = 0;
    for (std::size_t sp = 0; sp < content.species.size(); ++sp) {
        const SpeciesDef& d = content.species[sp];
        if (d.kind == SpeciesKind::Animal && d.food_value > 0.0 && !d.domesticated_before_start) livestock |= 1u << sp;
    }

    // ---- Markets: the inhabited cells, numbered 0..nm-1 in cell order.
    constexpr std::uint32_t kNone = ~0u;
    std::vector<std::uint32_t> market(n, kNone);
    std::vector<CellId> cell_of;
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] > 0) {
            market[c] = static_cast<std::uint32_t>(cell_of.size());
            cell_of.push_back(c);
        } else {
            // Abandoned: what was stored there is lost.
            for (std::size_t g = 0; g < ng; ++g) {
                s.stocks[c * ng + g] = 0.0;
                s.prices[c * ng + g] = content.goods[g].base_price;
            }
        }
    }
    const std::size_t nm = cell_of.size();

    // ---- People, their techniques and what carrying costs them.
    std::vector<double> pop(nm), farmers(nm), town(nm), elite(nm);
    std::vector<TechEffects> effects(nm);
    for (std::size_t m = 0; m < nm; ++m) {
        const CellId c = cell_of[m];
        double hierarchy = 0.0;
        for (const Cohort& co : s.cohorts_in(c)) {
            hierarchy += static_cast<double>(co.total()) * (i_hierarchy < kMaxTraits ? co.traits[i_hierarchy] : 0.5);
        }
        pop[m] = static_cast<double>(s.cell_population[c]);
        hierarchy /= pop[m];
        farmers[m] = pop[m] * (s.env.farming.empty() ? 0.0 : s.env.farming[c]);
        town[m] = pop[m] * (s.urban.empty() ? 0.0 : s.urban[c]);
        const bool ruled = !s.polity_of.empty() && s.polity_of[c] != 0;
        elite[m] = pop[m] * kEliteShare * (0.5 + hierarchy) * (ruled ? 2.0 : 1.0);
        effects[m] = technique_effects(w, c, techniques_in(s, c));
    }
    // Links between markets, with the carrying cost per unit weight each way:
    // links[m] lists (other market, link, cost from m, cost from the other).
    struct Link {
        std::uint32_t other, edge;
        double out, in;
    };
    // War cuts trade: pairs of polities on opposite sides of a war.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> enemies;
    for (const War& war : s.wars) {
        if (war.id == 0 || war.ended != kNever) continue;
        for (std::uint32_t a : war.attackers) {
            for (std::uint32_t d : war.defenders) enemies.emplace_back(std::min(a, d), std::max(a, d));
        }
    }
    std::sort(enemies.begin(), enemies.end());
    auto at_war = [&](CellId a, CellId b) {
        if (enemies.empty() || s.polity_of.empty()) return false;
        const std::uint32_t pa = s.polity_of[a], pb = s.polity_of[b];
        if (pa == 0 || pb == 0 || pa == pb) return false;
        return std::binary_search(enemies.begin(), enemies.end(), std::pair{std::min(pa, pb), std::max(pa, pb)});
    };
    std::vector<std::uint32_t> link_begin(nm + 1, 0);
    std::vector<Link> links;
    for (std::size_t m = 0; m < nm; ++m) {
        const CellId c = cell_of[m];
        for (std::uint32_t e : w.trade_links(c)) {
            const std::uint32_t o = market[w.trade_other(e, c)];
            if (o == kNone) continue;
            if (at_war(c, cell_of[o])) continue;  // no trade across the front
            const double out = carry_cost(w, w.trade_edges[e], effects[m]);
            const double in = carry_cost(w, w.trade_edges[e], effects[o]);
            if (out == kInf && in == kInf) continue;
            links.push_back({o, e, out, in});
        }
        link_begin[m + 1] = static_cast<std::uint32_t>(links.size());
    }
    auto links_of = [&](std::size_t m) {
        return std::span<const Link>(links.data() + link_begin[m], links.data() + link_begin[m + 1]);
    };

    // ---- Production and demand.
    std::vector<double> stock(nm * ng), demand(nm * ng, 0.0), wanted(nm * ng, 0.0), produced(nm * ng, 0.0);
    for (std::size_t m = 0; m < nm; ++m) {
        const CellId c = cell_of[m];
        std::copy_n(s.stocks.begin() + static_cast<std::ptrdiff_t>(c * ng), ng, stock.begin() + static_cast<std::ptrdiff_t>(m * ng));
        const std::uint64_t techs = techniques_in(s, c);
        const std::uint32_t known = s.domesticates.empty() ? 0 : s.domesticates[c];
        const std::size_t base = m * ng;
        for (std::size_t g = 0; g < ng; ++g) {
            if (g == food) continue;
            const GoodDef& def = content.goods[g];
            const std::size_t k = base + g;
            const double lean = std::clamp(s.prices[c * ng + g] / def.base_price, 0.5, 2.0);
            double made = 0.0;
            switch (def.source) {
                case GoodSource::Food: break;
                case GoodSource::Deposit: made = def.per_person * pop[m] * w.deposit(c, g) * lean; break;
                case GoodSource::Herds:
                    if (known & livestock) made = def.per_person * farmers[m] * lean;
                    break;
                case GoodSource::Craft: {
                    if (def.requires_technique >= 0 && !((techs >> def.requires_technique) & 1u)) break;
                    const double could = (def.per_person * pop[m] + def.per_townsperson * town[m]) * lean;
                    made = could;
                    for (const auto& [in, per] : def.inputs) {
                        wanted[base + in] += could * per;
                        made = std::min(made, stock[base + in] / per);
                    }
                    for (const auto& [in, per] : def.inputs) stock[base + in] = std::max(0.0, stock[base + in] - made * per);
                    break;
                }
            }
            stock[k] += made;
            produced[k] = made;
            const GoodDemand& d = def.demand;
            demand[k] = d.per_person * pop[m] + d.per_farmer * farmers[m] + d.per_townsperson * town[m] +
                        d.per_elite * elite[m];
            wanted[k] += demand[k];
        }
    }

    // ---- Prices, from what the market wants against what it holds.
    std::vector<double> price(nm * ng);
    for (std::size_t m = 0; m < nm; ++m) {
        const CellId c = cell_of[m];
        for (std::size_t g = 0; g < ng; ++g) {
            const std::size_t k = m * ng + g;
            const double base_price = content.goods[g].base_price;
            price[k] = g == food ? base_price * price_ratio(pop[m] * (1.0 + kTownFoodPremium * s.urban[c]), s.env.capacity[c])
                                 : base_price * price_ratio(wanted[k], stock[k]);
        }
    }

    // ---- Trade, from this year's stocks (every market decides at once).
    std::vector<double> delta(nm * ng, 0.0), received(nm * ng, 0.0);
    std::vector<double> carried(ne, 0.0);
    std::vector<double> traded(nm, 0.0);
    std::vector<std::pair<const Link*, double>> buyers;
    for (std::size_t m = 0; m < nm; ++m) {
        for (std::size_t g = 0; g < ng; ++g) {
            if (g == food) continue;
            const std::size_t k = m * ng + g;
            const double spare = stock[k] - wanted[k];
            if (spare <= 0.0) continue;
            const double weight = content.goods[g].weight;
            buyers.clear();
            double total = 0.0;
            for (const Link& l : links_of(m)) {
                const double gain = price[l.other * ng + g] - price[k] - weight * l.out;
                if (gain > 0.0) {
                    buyers.emplace_back(&l, gain);
                    total += gain;
                }
            }
            if (total <= 0.0) continue;
            const double send = kTradeShare * spare;
            for (const auto& [l, share] : buyers) {
                const double q = send * share / total;
                delta[k] -= q;
                delta[l->other * ng + g] += q;
                received[l->other * ng + g] += q;
                const double value = q * price[l->other * ng + g];
                carried[l->edge] += value;
                traded[m] += value;
                traded[l->other] += value;
            }
        }
    }

    // ---- Food: neighbours with food to spare feed hungry ones.
    std::vector<double> imported(nm, 0.0);
    if (food < ng) {
        std::vector<double> sent(nm, 0.0);
        std::vector<std::pair<const Link*, double>> flows;  // from the market owning the link
        std::vector<std::uint32_t> owner;
        for (std::size_t m = 0; m < nm; ++m) {
            const double cap = s.env.capacity[cell_of[m]];
            const double pa = price[m * ng + food];
            for (const Link& l : links_of(m)) {
                const double pb = price[l.other * ng + food];
                const double gap = pb - pa - l.out;
                if (gap <= 0.0) continue;
                const double q =
                    kFoodShare * gap / (pa + pb) * std::min(cap, s.env.capacity[cell_of[l.other]]);
                flows.emplace_back(&l, q);
                owner.push_back(static_cast<std::uint32_t>(m));
                sent[m] += q;
            }
        }
        for (std::size_t i = 0; i < flows.size(); ++i) {
            const std::uint32_t m = owner[i];
            const Link& l = *flows[i].first;
            double q = flows[i].second;
            const double limit = kMaxFoodExport * s.env.capacity[cell_of[m]];
            if (sent[m] > limit) q *= limit / sent[m];
            imported[m] -= q;
            imported[l.other] += q;
            const double value = q * price[l.other * ng + food];
            carried[l.edge] += value;
            traded[m] += value;
            traded[l.other] += value;
        }
    }

    // ---- Grain trade: stored food beyond a reserve goes where it sells dear.
    if (food < ng && s.env.food_store.size() == n) {
        std::vector<double> grain_in(nm, 0.0), grain_out(nm, 0.0);
        for (std::size_t m = 0; m < nm; ++m) {
            const CellId c = cell_of[m];
            const double spare = s.env.food_store[c] - kGrainReserve * pop[m];
            if (spare <= 0.0) continue;
            const double pa = price[m * ng + food];
            buyers.clear();
            double total = 0.0;
            for (const Link& l : links_of(m)) {
                const double gain = price[l.other * ng + food] - pa - content.goods[food].weight * l.out;
                if (gain > 0.0) {
                    buyers.emplace_back(&l, gain);
                    total += gain;
                }
            }
            if (total <= 0.0) continue;
            const double send = kGrainSale * spare;
            for (const auto& [l, gain] : buyers) {
                const double q = send * gain / total;
                grain_out[m] += q;
                grain_in[l->other] += q;
                const double value = q * price[l->other * ng + food];
                carried[l->edge] += value;
                traded[m] += value;
                traded[l->other] += value;
            }
        }
        for (std::size_t m = 0; m < nm; ++m) {
            s.env.food_store[cell_of[m]] += grain_in[m] - grain_out[m];
        }
    }

    // ---- State granaries: a literate polity feeds its hungry provinces
    // from its full granaries.
    if (s.env.food_store.size() == n && s.polity_of.size() == n && s.polities.size() > 1) {
        const std::size_t np = s.polities.size();
        std::size_t bureaucracy = content.techniques.size();
        for (std::size_t k = 0; k < content.techniques.size(); ++k) {
            if (content.techniques[k].id == "bureaucracy") bureaucracy = k;
        }
        std::vector<double> spare(np, 0.0), need(np, 0.0), reach(np, -1.0);
        std::vector<double> want(nm, 0.0), have(nm, 0.0);
        for (std::size_t m = 0; m < nm; ++m) {
            const CellId c = cell_of[m];
            const std::uint32_t p = s.polity_of[c];
            if (p == 0 || s.polities[p].ended != kNever) continue;
            if (reach[p] < 0.0) reach[p] = polity_has_writing(w, s, p) ? polity_reach(w, s, p) : 0.0;
            if (reach[p] <= 0.0) continue;
            have[m] = std::max(0.0, s.env.food_store[c] - kGrainReserve * pop[m]);
            spare[p] += have[m];
            const double hungry = pop[m] - (s.env.capacity[c] + imported[m]) - s.env.food_store[c];
            if (hungry > 0.0) {
                want[m] = hungry;
                need[p] += hungry;
            }
        }
        std::vector<double> sent(np, 0.0);
        for (std::size_t p = 1; p < np; ++p) {
            if (need[p] <= 0.0 || spare[p] <= 0.0) continue;
            const bool scribes = bureaucracy < 64 && ((techniques_in(s, s.polities[p].capital) >> bureaucracy) & 1u);
            sent[p] = std::min(need[p], (scribes ? kReliefBureaucracy : kRelief) * spare[p]);
        }
        for (std::size_t m = 0; m < nm; ++m) {
            const CellId c = cell_of[m];
            const std::uint32_t p = s.polity_of[c];
            if (p == 0 || sent[p] <= 0.0) continue;
            s.env.food_store[c] += sent[p] * (want[m] / need[p] - have[m] / spare[p]);
        }
    }

    // ---- Use, spoilage, what is on each market, and prices for next year.
    for (std::size_t m = 0; m < nm; ++m) {
        const CellId c = cell_of[m];
        s.trade_value[c] = traded[m];
        s.food_import[c] = imported[m];
        if (imported[m] != 0.0) s.env.capacity[c] = std::max(0.0, s.env.capacity[c] + imported[m]);
        std::uint32_t available = 0;
        for (std::size_t g = 0; g < ng; ++g) {
            const std::size_t k = m * ng + g;
            const GoodDef& def = content.goods[g];
            s.prices[c * ng + g] = price[k];
            if (g == food) {
                if (s.env.capacity[c] > 0.0) available |= 1u << g;
                continue;
            }
            double x = std::max(0.0, stock[k] + delta[k]);
            if (x >= kAvailable + kAvailablePerPerson * pop[m]) available |= 1u << g;
            x -= std::min(x, demand[k]);
            x *= 1.0 - def.spoilage;
            s.stocks[c * ng + g] = std::min(x, kMaxStockYears * (wanted[k] + produced[k] + received[k]) + 1.0);
        }
        s.goods_available[c] = available;
    }
    for (std::uint32_t e = 0; e < ne; ++e) s.route_value[e] += kRouteSmoothing * (carried[e] - s.route_value[e]);

    // ---- Food stores: put by in good years, drawn on in lean ones.
    if (s.env.food_store.size() != n) s.env.food_store.assign(n, 0.0);
    for (CellId c = 0; c < n; ++c) {
        double& store = s.env.food_store[c];
        const double people = static_cast<double>(s.cell_population[c]);
        if (!w.is_land[c] || people <= 0.0) {
            store = 0.0;
            continue;
        }
        const double gap = s.env.capacity[c] - people;
        if (gap >= 0.0) {
            const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            const double keep = kStoreShare * (kForagerStore + (1.0 - kForagerStore) * farming);
            const double storage = technique_effects(w, c, techniques_in(s, c)).harvest_buffer;
            const double room = people * (kStoreYears + kStoreYearsPerBuffer * storage);
            // Added up to the room there is; a store already fuller (grain
            // brought in as tribute or loot) is not added to.
            store = std::max(store, std::min(room, store + keep * gap));
        } else {
            const double draw = std::min(store, -gap);
            store -= draw;
            s.env.capacity[c] += draw;
        }
        store *= 1.0 - kStoreSpoilage;
    }
}

// ------------------------------------------------------------------ routes

double route_threshold(const SimState& s) {
    std::vector<double> busy;
    for (double v : s.route_value) {
        if (v > 0.0) busy.push_back(v);
    }
    if (busy.empty()) return kRouteMinValue;
    const auto k = static_cast<std::ptrdiff_t>(static_cast<double>(busy.size()) * (1.0 - kRouteShare));
    std::nth_element(busy.begin(), busy.begin() + k, busy.end());
    return std::max(kRouteMinValue, busy[static_cast<std::size_t>(k)]);
}

std::vector<TradeRoute> trade_routes(const World& w, const SimState& s, double min_value) {
    if (min_value < 0.0) min_value = route_threshold(s);
    std::vector<TradeRoute> routes;
    const std::size_t ne = std::min(w.trade_edges.size(), s.route_value.size());
    const std::size_t n = w.cell_count();
    // Join the cells of busy links.
    std::vector<CellId> parent(n);
    std::iota(parent.begin(), parent.end(), CellId{0});
    auto find = [&](CellId c) {
        while (parent[c] != c) c = parent[c] = parent[parent[c]];
        return c;
    };
    for (std::uint32_t e = 0; e < ne; ++e) {
        if (s.route_value[e] < min_value) continue;
        const CellId a = find(w.trade_edges[e].a), b = find(w.trade_edges[e].b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
    std::vector<std::int64_t> route_of(n, -1);
    std::vector<std::array<double, 3>> by_mode;
    for (std::uint32_t e = 0; e < ne; ++e) {
        if (s.route_value[e] < min_value) continue;
        const World::TradeEdge& edge = w.trade_edges[e];
        const CellId root = find(edge.a);
        if (route_of[root] < 0) {
            route_of[root] = static_cast<std::int64_t>(routes.size());
            routes.emplace_back();
            by_mode.push_back({0.0, 0.0, 0.0});
        }
        TradeRoute& r = routes[static_cast<std::size_t>(route_of[root])];
        r.edges.push_back(e);
        r.cells.push_back(edge.a);
        r.cells.push_back(edge.b);
        r.value += s.route_value[e];
        // The mode the busier end would use.
        TechEffects best;
        if (!s.techniques.empty()) {
            const TechEffects ea = technique_effects(w, edge.a, s.techniques[edge.a]);
            const TechEffects eb = technique_effects(w, edge.b, s.techniques[edge.b]);
            best.land_transport = std::max(ea.land_transport, eb.land_transport);
            best.water_transport = std::max(ea.water_transport, eb.water_transport);
        }
        const ModeCosts m = mode_costs(edge, best);
        const int mode = m.sea <= m.land && m.sea <= m.river ? 2 : (m.river <= m.land ? 1 : 0);
        by_mode[static_cast<std::size_t>(route_of[root])][mode] += s.route_value[e];
    }
    for (std::size_t i = 0; i < routes.size(); ++i) {
        TradeRoute& r = routes[i];
        std::sort(r.cells.begin(), r.cells.end());
        r.cells.erase(std::unique(r.cells.begin(), r.cells.end()), r.cells.end());
        const auto& bm = by_mode[i];
        r.modes = bm[2] >= bm[1] && bm[2] >= bm[0] ? kTradeSea : (bm[1] >= bm[0] ? kTradeRiver : kTradeLand);
        // Named after its largest city, else its busiest market's people.
        double biggest = -1.0;
        CellId busiest = r.cells.front();
        for (CellId c : r.cells) {
            const std::uint32_t city = c < s.city_of.size() ? s.city_of[c] : 0;
            const double people = c < s.urban.size() ? s.urban[c] * s.cell_population[c] : 0.0;
            if (city != 0 && people > biggest) biggest = people, r.city = city;
            if (s.trade_value.size() == n && s.trade_value[c] > s.trade_value[busiest]) busiest = c;
        }
        std::string place;
        if (r.city != 0) {
            place = s.cities[r.city].name;
        } else {
            const Cohort* lead = nullptr;
            for (const Cohort& co : s.cohorts_in(busiest)) {
                if (lead == nullptr || co.total() > lead->total()) lead = &co;
            }
            place = lead != nullptr ? s.cultures[lead->culture].name : "Unnamed";
        }
        r.name = place + (r.modes == kTradeSea ? " sea lanes" : r.modes == kTradeRiver ? " river trade" : " road");
    }
    // A lone busy link is a market's trade, not a route.
    routes.erase(std::remove_if(routes.begin(), routes.end(), [](const TradeRoute& r) { return r.edges.size() < 2; }),
                 routes.end());
    std::sort(routes.begin(), routes.end(), [](const TradeRoute& a, const TradeRoute& b) {
        return a.value != b.value ? a.value > b.value : a.edges.front() < b.edges.front();
    });
    return routes;
}

}  // namespace histsim
