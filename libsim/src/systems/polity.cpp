#include "histsim/systems/polity.hpp"
#include "histsim/systems/diplomacy.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

#include "histsim/figures.hpp"
#include "histsim/language.hpp"
#include "histsim/parallel.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/systems/farming.hpp"
#include "histsim/systems/religion.hpp"
#include "histsim/systems/technology.hpp"
#include "histsim/systems/war.hpp"
#include "vital_rates.inc"

namespace histsim {

namespace {

constexpr std::uint32_t kCivilWarEntity = 0x40000000u;
constexpr std::uint32_t kFormationEntity = 0x20000000u;
constexpr std::uint32_t kNameEntity = 0x60000000u;
constexpr std::uint32_t kCollapseEntity = 0x10000000u;
constexpr std::uint32_t kOfficialsEntity = 0x70000000u;
constexpr std::uint32_t kInstitutionEntity = 0x80000000u;
constexpr std::uint32_t kRidersEntity = 0x90000000u;
constexpr std::uint32_t kProvinceNameEntity = 0xA0000000u;
constexpr std::uint32_t kRevoltEntity = 0xB0000000u;
constexpr std::uint32_t kGovernorEntity = 0x10000000u;  // Stream::Figures
constexpr std::uint32_t kUprisingEntity = 0xC0000000u;
constexpr std::uint32_t kRebelEntity = 0x08000000u;     // Stream::Figures
constexpr std::uint32_t kGeneralEntity = 0x04000000u;   // Stream::Figures
constexpr std::uint32_t kPriestEntity = 0x02000000u;    // Stream::Figures
constexpr std::uint32_t kCoupEntity = 0xD0000000u;
constexpr std::uint32_t kRecallEntity = 0xE0000000u;
constexpr std::uint32_t kLawEntity = 0xF0000000u;
constexpr std::uint32_t kFactionEntity = 0x01000000u;  // Stream::Figures
constexpr std::uint32_t kIntrigueEntity = 0x0C000000u;
constexpr std::uint32_t kReplaceEntity = 0x00800000u;  // Stream::Figures

std::size_t trait_index(const Content& c, const char* id) {
    for (std::size_t t = 0; t < c.traits.size(); ++t) {
        if (c.traits[t].id == id) return t;
    }
    return c.traits.size();
}

// The largest cohort of a cell (its main culture and language).
const Cohort* lead_cohort(const SimState& s, CellId c) {
    const Cohort* best = nullptr;
    for (const Cohort& co : s.cohorts_in(c)) {
        if (best == nullptr || co.total() > best->total()) best = &co;
    }
    return best;
}

// A trait averaged over a cell's people (0.5 if absent).
double cell_trait(const SimState& s, CellId c, std::size_t t) {
    double sum = 0.0, total = 0.0;
    for (const Cohort& co : s.cohorts_in(c)) {
        sum += static_cast<double>(co.total()) * co.traits[t];
        total += static_cast<double>(co.total());
    }
    return total > 0.0 && t < kMaxTraits ? sum / total : 0.5;
}

// A new name in a language, not used before by any `taken` name.
template <typename Taken>
std::string coin_name(const Content& c, const Language& lang, RngStream& rng, Taken&& taken) {
    const std::uint64_t inv = inventory(lang);
    for (int attempt = 0; attempt < 12; ++attempt) {
        const std::string name = romanize(c, coin_word(c, inv, lang.syllables, 2, 3, rng), true);
        if (!taken(name)) return name;
    }
    for (int i = 2;; ++i) {
        const std::string name = lang.name + " " + std::to_string(i);
        if (!taken(name)) return name;
    }
}

void ensure(SimState& s, std::size_t n) {
    if (s.urban.size() != n) s.urban.assign(n, 0.0);
    if (s.city_of.size() != n) s.city_of.assign(n, 0);
    if (s.polity_of.size() != n) s.polity_of.assign(n, 0);
    if (s.cities.empty()) s.cities.emplace_back();
    if (s.polities.empty()) s.polities.emplace_back();
}

}  // namespace

void hinterland_people(const World& w, const SimState& s, std::vector<double>& people, std::vector<double>& town) {
    const std::size_t n = w.cell_count();
    people.assign(n, 0.0);
    town.assign(n, 0.0);
    const bool towns = s.urban.size() == n;
    // Each cell sums its own hinterland in a fixed order: the same with any
    // number of threads.
    parallel_for(n, [&](std::size_t c_begin, std::size_t c_end) {
        for (std::size_t c = c_begin; c < c_end; ++c) {
            double p = 0.0, t = 0.0;
            w.for_hinterland(static_cast<CellId>(c), [&](CellId h) {
                const double pop = static_cast<double>(s.cell_population[h]);
                p += pop;
                if (towns) t += s.urban[h] * pop;
            });
            people[c] = p;
            town[c] = t;
        }
    }, 256);
}

double polity_reach_km(const World& w, const SimState& s, CellId capital) {
    const TechEffects e = technique_effects(w, capital, techniques_in(s, capital));
    return PolitySystem::kBaseReachKm * (1.0 + e.administration) * (1.0 + 0.3 * e.contact);
}

double rule_terrain(const World& w, CellId c) {
    double f = 1.0;
    switch (static_cast<Biome>(w.biome[c])) {
        case Biome::TemperateGrassland:
        case Biome::TropicalSavanna: f = 0.9; break;
        case Biome::BorealForest: f = 1.4; break;
        case Biome::Desert:
        case Biome::Tundra:
        case Biome::TropicalRainforest: f = 2.0; break;
        case Biome::IceSheet: f = 4.0; break;
        default: f = 1.0; break;  // temperate and Mediterranean lands, shrubland
    }
    return f * (1.0 + std::max(0.0, w.elevation_m[c] - RuleTravel::kHighM) / RuleTravel::kHighScale);
}

namespace {

// Travel distance along one link under the techniques of the realm.
double rule_link_km(const World& w, const World::TradeEdge& e, const TechEffects& t) {
    double best = std::numeric_limits<double>::infinity();
    if (e.modes & kTradeLand) {
        best = e.distance_km * 0.5 * (rule_terrain(w, e.a) + rule_terrain(w, e.b)) * (1.0 + RuleTravel::kClimb * e.climb) /
               (1.0 + 0.5 * std::max(0.0, t.land_transport));
    }
    // (Reach already grows with these techniques; half their effect again here.)
    const double water = 1.0 + 0.5 * std::max(0.0, t.water_transport);
    if (e.modes & kTradeRiver) best = std::min(best, e.distance_km * RuleTravel::kRiver / water);
    if ((e.modes & kTradeSea) && t.water_transport > 0.0) best = std::min(best, e.distance_km * RuleTravel::kSea / water);
    return best;
}

// Distances from `from` out to `limit` km (cells and km, in the order found).
void rule_search(const World& w, const TechEffects& t, CellId from, double limit, std::vector<double>& dist,
                 std::vector<std::pair<CellId, double>>& out) {
    out.clear();
    using Q = std::pair<double, CellId>;
    std::priority_queue<Q, std::vector<Q>, std::greater<>> q;
    q.emplace(0.0, from);
    dist[from] = 0.0;
    std::vector<CellId> touched{from};
    while (!q.empty()) {
        const auto [d, c] = q.top();
        q.pop();
        if (d > dist[c]) continue;
        out.emplace_back(c, d);
        for (std::uint32_t k = w.trade_edge_begin[c]; k < w.trade_edge_begin[c + 1]; ++k) {
            const World::TradeEdge& e = w.trade_edges[w.trade_edge_ids[k]];
            const CellId nb = e.a == c ? e.b : e.a;
            const double nd = d + rule_link_km(w, e, t);
            if (nd > limit || nd >= dist[nb]) continue;
            if (dist[nb] == std::numeric_limits<double>::infinity()) touched.push_back(nb);
            dist[nb] = nd;
            q.emplace(nd, nb);
        }
    }
    for (CellId c : touched) dist[c] = std::numeric_limits<double>::infinity();
}

}  // namespace

void RuleField::build(const World& w, const SimState& s, const std::vector<double>& reach) {
    const std::size_t np = s.polities.size();
    near_.assign(np, {});
    limit_.assign(np, 0.0);
    if (w.trade_edge_begin.size() != w.cell_count() + 1) return;
    parallel_for(np, [&](std::size_t begin, std::size_t end) {
        std::vector<double> dist(w.cell_count(), std::numeric_limits<double>::infinity());
        for (std::size_t id = std::max<std::size_t>(begin, 1); id < end; ++id) {
            const Polity& p = s.polities[id];
            if (p.ended != kNever || id >= reach.size() || reach[id] <= 0.0 || p.capital >= w.cell_count()) continue;
            limit_[id] = RuleTravel::kSearch * reach[id];
            const TechEffects t = technique_effects(w, p.capital, techniques_in(s, p.capital));
            rule_search(w, t, p.capital, limit_[id], dist, near_[id]);
            std::sort(near_[id].begin(), near_[id].end());
        }
    }, 8);
}

double RuleField::km(const World& w, const SimState& s, std::uint32_t id, CellId c) const {
    if (id < near_.size()) {
        const auto& v = near_[id];
        const auto it = std::lower_bound(v.begin(), v.end(), std::pair<CellId, double>{c, -1.0});
        if (it != v.end() && it->first == c) return it->second;
    }
    const double straight = id < s.polities.size() ? w.grid.distance_km(s.polities[id].capital, c) : 0.0;
    return std::max(id < limit_.size() ? limit_[id] : 0.0, RuleTravel::kFar * straight);
}

double rule_km_between(const World& w, const SimState& s, CellId from, CellId to, double limit) {
    if (from == to) return 0.0;
    if (w.trade_edge_begin.size() != w.cell_count() + 1) return w.grid.distance_km(from, to);
    std::vector<double> dist(w.cell_count(), std::numeric_limits<double>::infinity());
    std::vector<std::pair<CellId, double>> found;
    rule_search(w, technique_effects(w, from, techniques_in(s, from)), from, limit, dist, found);
    for (const auto& [c, d] : found) {
        if (c == to) return d;
    }
    return std::max(limit, RuleTravel::kFar * w.grid.distance_km(from, to));
}

double rule_km(const World& w, const SimState& s, std::uint32_t id, CellId c) {
    if (id == 0 || id >= s.polities.size()) return 0.0;
    return rule_km_between(w, s, s.polities[id].capital, c, RuleTravel::kSearch * polity_reach(w, s, id));
}

double admin_capacity(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.polities.size()) return 0.0;
    const Polity& p = s.polities[id];
    const TechEffects e = technique_effects(w, p.capital, techniques_in(s, p.capital));
    return PolitySystem::kAdminLoad * (1.0 + e.administration) * (1.0 + 0.3 * e.contact) *
           nomad_reach_factor(w, s, id);
}

std::vector<double> admin_loads(const World& w, const SimState& s, const RuleField& rule, const std::vector<double>& reach) {
    std::vector<double> load(s.polities.size(), 0.0);
    if (s.polity_of.size() != w.cell_count()) return load;
    const double share = std::min(1.0, w.cell_scale() * w.cell_scale());  // of a level-5 cell
    for (CellId c = 0; c < w.cell_count(); ++c) {
        const std::uint32_t id = s.polity_of[c];
        if (id == 0 || id >= reach.size() || reach[id] <= 0.0) continue;
        load[id] += share / std::max(1e-3, polity_control(rule.km(w, s, id, c), reach[id]));
    }
    return load;
}

double nomad_reach_factor(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.polities.size() || !s.polities[id].nomadic) return 1.0;
    const auto& techs = w.content->techniques;
    for (std::size_t k = 0; k < techs.size(); ++k) {
        if (techs[k].id == "bronze") {
            return (techniques_in(s, s.polities[id].capital) >> k) & 1u ? PolitySystem::kNomadReach : PolitySystem::kNomadReachEarly;
        }
    }
    return PolitySystem::kNomadReach;
}

double polity_reach(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.polities.size()) return 0.0;
    const Polity& p = s.polities[id];
    return polity_reach_km(w, s, p.capital) * nomad_reach_factor(w, s, id);
}

bool polity_has_writing(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.polities.size()) return false;
    const auto& techs = w.content->techniques;
    for (std::size_t k = 0; k < techs.size(); ++k) {
        if (techs[k].id == "writing") return (techniques_in(s, s.polities[id].capital) >> k) & 1u;
    }
    return false;
}

const char* polity_level(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.polities.size()) return "";
    if (!polity_has_writing(w, s, id)) return s.polities[id].nomadic ? "confederation" : "chiefdom";
    return polity_size(w, s, id).area_km2 >= 1.0e6 ? "empire" : "state";
}

PolitySize polity_size(const World& w, const SimState& s, std::uint32_t id) {
    PolitySize z;
    for (CellId c = 0; c < s.polity_of.size(); ++c) {
        if (s.polity_of[c] != id) continue;
        ++z.cells;
        z.people += s.cell_population[c];
        z.area_km2 += w.grid.area_km2(c);
    }
    return z;
}

const char* pressure_name(Pressure p) {
    switch (p) {
        case Pressure::Famine: return "famine";
        case Pressure::War: return "war";
        case Pressure::Overreach: return "overreach";
        case Pressure::Poverty: return "poverty";
        case Pressure::Epidemic: return "epidemic";
        case Pressure::Division: return "division";
        case Pressure::Count: break;
    }
    return "";
}

std::string polity_fate_text(const Polity& p) {
    switch (p.fate) {
        case PolityFate::Living: return p.ended == kNever ? "living" : "ended";
        case PolityFate::Collapsed:
            if (p.fate_cause == kSuccessionCrisis) return "broke up in a succession crisis";
            if (p.fate_cause < kPressureCount) {
                return std::string("collapsed under ") + pressure_name(static_cast<Pressure>(p.fate_cause));
            }
            return "collapsed";
        case PolityFate::Conquered: return "conquered";
        case PolityFate::Absorbed: return "absorbed by its overlord";
        case PolityFate::Dwindled: return "dwindled away";
    }
    return "";
}

// ------------------------------------------------------------------ towns

void SettlementSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    ensure(s, n);
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) {
            s.urban[c] = 0.0;
        } else {
            // People fed per km^2 by farmland, and by food brought in last year.
            const double farmed = s.env.farm_capacity.empty() ? 0.0 : s.env.farm_capacity[c] * s.env.farming[c];
            const double imported = s.food_import.empty() ? 0.0 : std::max(0.0, s.food_import[c]);
            const double fed = (farmed + imported) / w.grid.area_km2(c);
            double target = std::clamp(kPerSurplus * (fed - kThreshold), 0.0, kMax);
            const std::uint32_t p = s.polity_of[c];
            if (target > 0.0 && p != 0 && s.polities[p].capital == c) target += kCapital;
            s.urban[c] += kRate * (target - s.urban[c]);
        }
    }
    // A city is named for the town of a hinterland (the cell itself at level
    // 5), at its centre: the cell with the most townspeople around it.
    std::vector<double> around_people, around_town;
    hinterland_people(w, s, around_people, around_town);
    for (CellId c = 0; c < n; ++c) {
        const double town = around_town[c];
        std::uint32_t& city = s.city_of[c];
        if (city == 0 && town < kCityPeople) continue;  // no city, and none to found
        bool centre = true, taken = false;
        if (w.hinterland_steps() > 0) {
            w.for_hinterland(c, [&](CellId h) {
                if (h == c) return;
                if (around_town[h] > town || (around_town[h] == town && h < c)) centre = false;
                if (s.city_of[h] != 0) taken = true;
            });
        }
        if (city == 0 && town >= kCityPeople && centre && !taken) {
            const Cohort* lead = lead_cohort(s, c);
            const Language& lang = s.languages[lead->language];
            RngStream rng = ctx.rng(Stream::Settlement, kNameEntity | c);
            City nc;
            nc.id = static_cast<std::uint32_t>(s.cities.size());
            nc.cell = c;
            nc.founded = ctx.tick;
            nc.name = coin_name(*w.content, lang, rng, [&](const std::string& name) {
                return std::any_of(s.cities.begin(), s.cities.end(), [&](const City& x) { return x.name == name; });
            });
            city = nc.id;
            s.cities.push_back(std::move(nc));
            s.events.push_back({ctx.tick, EventType::CityFounded, c, static_cast<std::int64_t>(city)});
        } else if (city != 0 && town < kCityPeople / 3.0) {
            s.cities[city].abandoned = ctx.tick;
            city = 0;
        }
    }
}

// ------------------------------------------------------------------ polities

void PolitySystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    ensure(s, n);
    const std::size_t i_hierarchy = trait_index(content, "hierarchy");
    const std::size_t i_warfare = trait_index(content, "warfare");
    auto taken = [&](const std::string& name) {
        return std::any_of(s.polities.begin(), s.polities.end(), [&](const Polity& x) { return x.name == name; });
    };
    auto found = [&](CellId capital, std::uint32_t parent, bool nomadic) {
        const Cohort* lead = lead_cohort(s, capital);
        RngStream rng = ctx.rng(Stream::Polity, kNameEntity | capital);
        Polity p;
        p.id = static_cast<std::uint32_t>(s.polities.size());
        p.capital = capital;
        p.founded = ctx.tick;
        p.culture = lead->culture;
        p.language = lead->language;
        p.parent = parent;
        p.nomadic = nomadic;
        // A broken state's core and successors keep its temples, councils
        // and laws; its army does not survive the break.
        if (parent != 0 && parent < s.polities.size()) {
            p.institutions = s.polities[parent].institutions &
                             static_cast<std::uint8_t>(~(1u << static_cast<int>(Institution::Army)));
            p.laws = s.polities[parent].laws;
            if (p.laws[Laws::Army] == 0) p.laws[Laws::Army] = 1;  // no royal army without the army
        }
        // Named after its capital city if it has one, else coined.
        const std::uint32_t city = s.city_of[capital];
        p.name = city != 0 && !taken(s.cities[city].name) ? s.cities[city].name
                                                            : coin_name(content, s.languages[lead->language], rng, taken);
        s.polities.push_back(std::move(p));
        s.events.push_back({ctx.tick, EventType::PolityFounded, capital, static_cast<std::int64_t>(s.polities.back().id)});
        return s.polities.back().id;
    };

    // ---- Empty land is ruled by no one.
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) s.polity_of[c] = 0;
    }

    // ---- Each living polity's cells; capitals move or the polity ends.
    const std::size_t np = s.polities.size();
    std::vector<std::vector<CellId>> cells(np);
    for (CellId c = 0; c < n; ++c) {
        if (s.polity_of[c] != 0) cells[s.polity_of[c]].push_back(c);
    }
    for (std::size_t id = 1; id < np; ++id) {
        Polity& p = s.polities[id];
        if (p.ended != kNever) continue;
        if (cells[id].empty()) {
            p.ended = ctx.tick;
            // Overrun if it had been losing a war; else it simply dwindled.
            if (p.fate == PolityFate::Living) p.fate = p.shock > 0.2 ? PolityFate::Conquered : PolityFate::Dwindled;
            s.events.push_back({ctx.tick, EventType::PolityEnded, p.capital, static_cast<std::int64_t>(id)});
            continue;
        }
        if (s.polity_of[p.capital] != id) {
            CellId best = cells[id][0];
            for (CellId c : cells[id]) {
                if (s.cell_population[c] > s.cell_population[best]) best = c;
            }
            p.capital = best;
        } else {
            // The seat moves to a town (or, for nomads, a camp) that has far
            // outgrown it.
            auto seat_size = [&](CellId c) {
                return static_cast<double>(s.cell_population[c]) * (p.nomadic ? 1.0 : s.urban[c]);
            };
            CellId best = p.capital;
            for (CellId c : cells[id]) {
                if (seat_size(c) > seat_size(best) || (seat_size(c) == seat_size(best) && c < best)) best = c;
            }
            if (best != p.capital && seat_size(best) > kCapitalMove * seat_size(p.capital)) p.capital = best;
        }
    }

    // ---- Conquest dynasties settle down.
    for (std::size_t id = 1; id < np; ++id) {
        Polity& p = s.polities[id];
        if (p.ended == kNever && p.nomadic && p.settling_since != kNever &&
            ctx.tick >= p.settling_since + kSettleYears * kTicksPerYear) {
            p.nomadic = false;
            p.settling_since = kNever;
        }
    }

    // ---- Reach and strength.
    std::vector<double> reach(np, 0.0), strength(np, 0.0);
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended == kNever) reach[id] = polity_reach(w, s, static_cast<std::uint32_t>(id));
    }
    // Rule travels by land, river and sea, not as the crow flies.
    RuleField rule;
    rule.build(w, s, reach);
    auto control_of = [&](std::size_t id, CellId c) {
        return reach[id] > 0.0 ? polity_control(rule.km(w, s, static_cast<std::uint32_t>(id), c), reach[id]) : 0.0;
    };
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended != kNever) continue;
        for (CellId c : cells[id]) strength[id] += static_cast<double>(s.cell_population[c]) * control_of(id, c);
    }

    // ---- Expansion over stateless land: one attempt a year per polity (per
    // level-5 cell's worth of land on finer grids), at the neighbouring cells
    // it would hold best. (Land changes hands between polities only in war.)
    const std::size_t attempts = std::max<std::size_t>(1, w.cells_for(1));
    const double cell_share = std::min(1.0, w.cell_scale() * w.cell_scale());  // of a level-5 cell
    const double coarse = std::max(1.0, w.cell_scale() * w.cell_scale());       // level-5 cells in one
    std::vector<double> load = admin_loads(w, s, rule, reach);
    std::vector<std::pair<double, CellId>> frontier;
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended != kNever) continue;
        // At the limit of what its administration can run, it takes no more.
        const double capacity = admin_capacity(w, s, static_cast<std::uint32_t>(id));
        if (load[id] >= capacity) continue;
        frontier.clear();
        for (CellId c : cells[id]) {
            for (CellId nb : w.grid.neighbors(c)) {
                if (s.polity_of[nb] != 0 || s.cell_population[nb] == 0) continue;
                frontier.emplace_back(control_of(id, nb), nb);
            }
        }
        // Best held first (ties: lower cell), each cell once.
        std::sort(frontier.begin(), frontier.end(), [](const auto& a, const auto& b) {
            return a.first > b.first || (a.first == b.first && a.second < b.second);
        });
        frontier.erase(std::unique(frontier.begin(), frontier.end()), frontier.end());
        RngStream rng = ctx.rng(Stream::Polity, static_cast<std::uint32_t>(id));
        const double warfare = cell_trait(s, s.polities[id].capital, i_warfare);
        const CellId seat = s.polities[id].capital;
        const bool herding = s.polities[id].nomadic || (!s.env.pastoral.empty() && s.env.pastoral[seat] >= 0.5);
        std::size_t tried = 0;
        for (std::size_t k = 0; tried < attempts && k < frontier.size(); ++k) {
            const auto [best, target] = frontier[k];
            if (best < kMinControl) break;
            if (load[id] + cell_share / best > capacity) continue;
            if (!herding) {
                // Worth ruling? Crops to tax, or trade passing through.
                const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[target];
                const double herds = s.env.pastoral.empty() ? 0.0 : s.env.pastoral[target];
                const double trade = s.trade_value.empty() ? 0.0 : s.trade_value[target];
                const double people = static_cast<double>(s.cell_population[target]);
                if (farming * (1.0 - herds) < kWorthCrops && trade < kTradePerPerson * people) continue;
            }
            ++tried;
            // Strength against the people of a level-5 cell's worth of land.
            const double people = static_cast<double>(s.cell_population[target]) / cell_share;
            const double p = kAnnex * best * (0.5 + warfare) * std::min(1.0, strength[id] / (4.0 * people)) / coarse;
            if (rng.uniform() < p) {
                s.polity_of[target] = static_cast<std::uint32_t>(id);
                load[id] += cell_share / best;
                // Rebuild later from polity_of; capitals are rechecked next year.
            }
        }
    }

    // ---- Stability: resilience against this year's pressures.
    const std::size_t i_mobility = trait_index(content, "mobility");
    const std::size_t i_ritual = trait_index(content, "ritual");
    std::size_t writing = content.techniques.size(), bureaucracy = content.techniques.size();
    for (std::size_t k = 0; k < content.techniques.size(); ++k) {
        if (content.techniques[k].id == "writing") writing = k;
        if (content.techniques[k].id == "bureaucracy") bureaucracy = k;
    }
    for (std::size_t id = 1; id < np; ++id) {
        Polity& pol = s.polities[id];
        if (pol.ended != kNever) continue;
        const std::uint32_t root = s.cultures[pol.culture].root;
        double people = 0.0, unfed = 0.0, held = 0.0, trade = 0.0, sick = 0.0, others = 0.0;
        double faithful = 0.0, heretics = 0.0, enslaved = 0.0;
        // Mobile subjects (herders, foragers) are held more loosely: they can
        // move away from tax collectors and garrisons.
        const bool faiths = s.faith_of.size() == s.cell_population.size();
        const bool organized = faiths && religion_organized(w, s, pol.religion);
        const std::uint32_t creed = organized ? religion_family(s, pol.religion) : 0;
        for (CellId c : cells[id]) {
            const double pop = static_cast<double>(s.cell_population[c]);
            if (faiths && s.faith_of[c] != 0) {
                const std::uint32_t family = religion_family(s, s.faith_of[c]);
                if (organized && family == creed) faithful += pop;
                else if (religion_organized(w, s, s.faith_of[c])) heretics += pop;
            }
            people += pop;
            unfed += std::max(0.0, pop - s.env.capacity[c]);
            held += pop * control_of(id, c) * (1.0 - kMobileRule * cell_trait(s, c, i_mobility));
            if (!s.trade_value.empty()) trade += s.trade_value[c];
            if (!s.disease_deaths.empty()) sick += s.disease_deaths[c];
            for (const Cohort& co : s.cohorts_in(c)) {
                if (s.cultures[co.culture].root != root) others += static_cast<double>(co.total());
                enslaved += co.bonded * static_cast<double>(co.total());
            }
        }
        pol.shock *= 1.0 - kShockDecay;
        auto& pr = pol.pressure;
        pr.fill(0.0);
        if (people > 0.0) {
            pr[static_cast<std::size_t>(Pressure::Famine)] = std::min(1.0, kFamineScale * unfed / people);
            pr[static_cast<std::size_t>(Pressure::Overreach)] = 1.0 - held / people;
            pr[static_cast<std::size_t>(Pressure::Poverty)] =
                std::clamp(1.0 - trade / people / kTradePerPerson, 0.0, 1.0);
            pr[static_cast<std::size_t>(Pressure::Epidemic)] =
                std::clamp(kEpidemicScale * (sick / people - kUsualDiseaseDeaths), 0.0, 1.0);
            pr[static_cast<std::size_t>(Pressure::Division)] =
                std::min(1.0, (others + kFaithDivision * heretics + kBondDivision * enslaved) / people);
            if (has_institution(pol.institutions, Institution::LawCode)) {
                pr[static_cast<std::size_t>(Pressure::Division)] *= Institutions::kLawDivision;
            }
            if (has_institution(pol.institutions, Institution::Army)) {
                pr[static_cast<std::size_t>(Pressure::Overreach)] *= Institutions::kArmyOverreach;
            }
        }
        pr[static_cast<std::size_t>(Pressure::War)] = std::min(1.0, pol.shock);
        const bool marching = pol.trek_to != kInvalidCell ||
                              (pol.arrived != kNever && ctx.tick - pol.arrived < kArrivalYears * kTicksPerYear);
        if (marching) pr[static_cast<std::size_t>(Pressure::Famine)] *= kMarchFamine;
        const std::uint64_t known = techniques_in(s, pol.capital);
        double target = kStabilityBase + kHierarchyWeight * cell_trait(s, pol.capital, i_hierarchy) +
                        kRitualWeight * cell_trait(s, pol.capital, i_ritual) +
                        (writing < 64 && ((known >> writing) & 1u) ? kWritingWeight : 0.0) +
                        (bureaucracy < 64 && ((known >> bureaucracy) & 1u) ? kBureaucracyWeight : 0.0) +
                        (people > 0.0 ? kFaithWeight * faithful / people : 0.0);
        if (has_institution(pol.institutions, Institution::Temple)) target += Institutions::kTempleStability;
        if (has_institution(pol.institutions, Institution::LawCode)) target += Institutions::kLawStability;
        // An able ruler steadies the realm, a poor one or a regency shakes it.
        if (pol.ruler != 0 && pol.ruler < s.rulers.size()) {
            const Ruler& r = s.rulers[pol.ruler];
            target += RulerSystem::kAbilityWeight * (r.ability - 0.5) * 2.0;
            // The high priest blesses a pious ruler and turns on an impious one.
            if (pol.priest != 0 && pol.priest < s.figures.size() && r.figure != 0 && r.figure < s.figures.size()) {
                target += kPriestLegitimacy * 2.0 * (s.figures[r.figure].piety - 0.5) * (0.5 + s.figures[pol.priest].piety);
            }
            if (ruler_age(r, ctx.tick) < RulerSystem::kAdultAge) target -= RulerSystem::kRegencyPenalty;
        }
        if (marching) target += kMarchCohesion;
        // A confederation holds together while the plunder flows.
        if (pol.nomadic) target += WarSystem::kPlunderStability * (2.0 * plunder_plenty(pol, people) - 1.0);
        for (std::size_t k = 0; k < kPressureCount; ++k) target -= kPressureWeight[k] * pr[k];
        target = std::clamp(target, 0.0, 1.0);
        pol.stability += (target < pol.stability ? kStabilityFall : kStabilityRise) * (target - pol.stability);
    }

    // ---- Institutions founded, and lost in hard times.
    {
        std::vector<double> around_people, around_town;
        hinterland_people(w, s, around_people, around_town);
        const std::size_t i_ritual_k = trait_index(content, "ritual");
        std::size_t writing_k = content.techniques.size(), bronze_k = writing_k, riding_k = writing_k;
        for (std::size_t k = 0; k < content.techniques.size(); ++k) {
            if (content.techniques[k].id == "writing") writing_k = k;
            if (content.techniques[k].id == "bronze") bronze_k = k;
            if (content.techniques[k].id == "horse_riding") riding_k = k;
        }
        auto knows = [&](CellId c, std::size_t k) { return k < 64 && ((techniques_in(s, c) >> k) & 1u); };
        for (std::size_t id = 1; id < np; ++id) {
            Polity& pol = s.polities[id];
            if (pol.ended != kNever || cells[id].empty()) continue;
            RngStream rng = ctx.rng(Stream::Polity, kInstitutionEntity | static_cast<std::uint32_t>(id));
            double people = 0.0;
            for (CellId c : cells[id]) people += static_cast<double>(s.cell_population[c]);
            const bool literate = knows(pol.capital, writing_k);
            for (std::size_t k = 0; k < kInstitutionCount; ++k) {
                const auto inst = static_cast<Institution>(k);
                const std::uint8_t bit = static_cast<std::uint8_t>(1u << k);
                const std::int64_t tag = static_cast<std::int64_t>(id) * 16 + static_cast<std::int64_t>(k);
                if (pol.institutions & bit) {
                    if (pol.stability < kFragile && rng.uniform() < Institutions::kDecay) {
                        pol.institutions &= static_cast<std::uint8_t>(~bit);
                        s.events.push_back({ctx.tick, EventType::InstitutionLost, pol.capital, tag});
                    }
                    continue;
                }
                bool qualifies = false;
                switch (inst) {
                    case Institution::Temple:
                        qualifies = religion_organized(w, s, pol.religion) ||
                                    (cell_trait(s, pol.capital, i_ritual_k) >= Institutions::kTempleRitual &&
                                     around_town[pol.capital] >= Institutions::kTempleTown);
                        break;
                    case Institution::Council:
                        qualifies = cells[id].size() >= w.cells_for(Institutions::kCouncilCells) &&
                                    cell_trait(s, pol.capital, i_hierarchy) >= Institutions::kCouncilHierarchy;
                        break;
                    case Institution::LawCode: qualifies = literate; break;
                    case Institution::Army:
                        qualifies = literate && people >= Institutions::kArmyPeople &&
                                    (knows(pol.capital, bronze_k) || knows(pol.capital, riding_k));
                        break;
                    case Institution::Count: break;
                }
                // The ruler's leanings: a pious one builds up the priesthood,
                // an able one councils and laws, a martial one an army.
                const Figure* king = reigning_person(s, static_cast<std::uint32_t>(id));
                const double leaning = inst == Institution::Temple ? trait_factor(king, &Figure::piety)
                                       : inst == Institution::Army ? trait_factor(king, &Figure::martial)
                                                                   : trait_factor(king, &Figure::ability);
                if (qualifies && rng.uniform() < Institutions::kFound * leaning) {
                    pol.institutions |= bit;
                    s.events.push_back({ctx.tick, EventType::InstitutionFounded, pol.capital, tag});
                }
            }
        }
    }

    // ---- Provinces: seats, land, governors and loyalty; disloyal provinces
    // break away.
    if (s.provinces.empty()) s.provinces.emplace_back();
    if (s.province_of.size() != n) s.province_of.assign(n, 0);
    std::vector<std::uint8_t> provincial(np, 0);
    {
        std::vector<std::vector<CellId>> held(np);
        for (CellId c = 0; c < n; ++c) {
            const std::uint32_t q = s.polity_of[c];
            if (q != 0 && q < np) held[q].push_back(c);
        }
        const std::size_t min_cells = std::max<std::size_t>(2, w.cells_for(kProvinceMinCells));
        std::vector<std::vector<std::uint32_t>> seats_of(np);
        // Provinces end with their realm, when their seat is lost (or becomes
        // the capital), or when the realm is too small to have them.
        for (Province& pv : s.provinces) {
            if (pv.id == 0 || pv.ended != kNever) continue;
            const bool gone = pv.polity >= np || s.polities[pv.polity].ended != kNever || s.polity_of[pv.seat] != pv.polity ||
                              s.polities[pv.polity].capital == pv.seat || held[pv.polity].size() < min_cells ||
                              !polity_has_writing(w, s, pv.polity);
            if (gone) {
                pv.ended = ctx.tick;
            } else {
                seats_of[pv.polity].push_back(pv.id);
            }
        }
        auto town_score = [&](CellId c) {
            const double people = static_cast<double>(s.cell_population[c]);
            return (s.urban.empty() ? 0.0 : s.urban[c] * people) + kSeatRural * people;
        };
        std::vector<CellId> order;
        for (std::size_t id = 1; id < np; ++id) {
            Polity& pol = s.polities[id];
            // Governed provinces need scribes: states only (chiefs rule
            // through kin and lesser chiefs).
            if (pol.ended != kNever || held[id].size() < min_cells || !polity_has_writing(w, s, static_cast<std::uint32_t>(id))) {
                continue;
            }
            provincial[id] = 1;
            // New seats at the largest towns far enough from the capital and
            // from every seat.
            order = held[id];
            std::stable_sort(order.begin(), order.end(), [&](CellId a, CellId b) {
                return town_score(a) > town_score(b) || (town_score(a) == town_score(b) && a < b);
            });
            for (CellId c : order) {
                if (w.grid.distance_km(c, pol.capital) < kProvinceSpacingKm) continue;
                bool near = false;
                for (std::uint32_t v : seats_of[id]) near = near || w.grid.distance_km(c, s.provinces[v].seat) < kProvinceSpacingKm;
                if (near) continue;
                Province pv;
                pv.id = static_cast<std::uint32_t>(s.provinces.size());
                pv.polity = static_cast<std::uint32_t>(id);
                pv.seat = c;
                pv.founded = ctx.tick;
                const std::uint32_t city = s.city_of[c];
                if (city != 0) {
                    pv.name = s.cities[city].name;
                } else {
                    const Cohort* lead = lead_cohort(s, c);
                    RngStream nr = ctx.rng(Stream::Polity, kProvinceNameEntity | c);
                    pv.name = coin_name(content, s.languages[lead->language], nr, [](const std::string&) { return false; });
                }
                pv.loyalty = -1.0;  // set below, at its target
                seats_of[id].push_back(pv.id);
                s.provinces.push_back(std::move(pv));
            }
            // Each cell to the nearest seat; the capital's land is the crown's.
            for (CellId c : held[id]) {
                double best = w.grid.distance_km(c, pol.capital);
                std::uint32_t to = 0;
                for (std::uint32_t v : seats_of[id]) {
                    const double d = w.grid.distance_km(c, s.provinces[v].seat);
                    if (d < best || (d == best && v < to)) best = d, to = v;
                }
                s.province_of[c] = to;
            }
        }
        for (CellId c = 0; c < n; ++c) {
            const std::uint32_t q = s.polity_of[c];
            if (q == 0 || q >= np || !provincial[q]) s.province_of[c] = 0;
        }
        // A partible inheritance: the provinces farther from the capital go
        // to a younger son, as a realm of his own.
        for (std::size_t id = 1; id < np; ++id) {
            Polity& pol = s.polities[id];
            if (!pol.divide) continue;
            pol.divide = false;
            if (!provincial[id] || pol.ended != kNever) continue;
            std::vector<std::uint32_t> live;
            for (std::uint32_t v : seats_of[id]) {
                if (s.provinces[v].ended == kNever) live.push_back(v);
            }
            if (live.size() < 2) continue;
            const CellId capital = pol.capital;
            std::stable_sort(live.begin(), live.end(), [&](std::uint32_t a, std::uint32_t b) {
                return w.grid.distance_km(capital, s.provinces[a].seat) > w.grid.distance_km(capital, s.provinces[b].seat);
            });
            live.resize(live.size() / 2);
            const std::uint32_t realm = static_cast<std::uint32_t>(id);
            const std::uint32_t son = found(s.provinces[live.front()].seat, realm, pol.nomadic);
            s.polities[son].pretender_to = realm;  // the brothers' realms are rival heirs
            s.polities[son].stability = s.polities[id].stability;
            for (CellId c : held[id]) {
                const std::uint32_t v = s.province_of[c];
                if (s.polity_of[c] == realm && v != 0 && std::find(live.begin(), live.end(), v) != live.end()) {
                    s.polity_of[c] = son;
                    s.province_of[c] = 0;
                }
            }
            for (std::uint32_t v : live) {
                s.provinces[v].ended = ctx.tick;
                s.provinces[v].became = son;
                if (s.provinces[v].loyalty < 0.0) s.provinces[v].loyalty = kStayLoyal;  // new this year: never governed
            }
            s.events.push_back({ctx.tick, EventType::RealmDivided, s.polities[son].capital, static_cast<std::int64_t>(son)});
        }
        // Governors, loyalty and revolts. How the war each crown leads is
        // going for it (its war score against it, 0-1).
        std::vector<double> losing(np, 0.0), crown_at_war(np, 0.0);
        for (std::size_t wi = 1; wi < s.wars.size(); ++wi) {
            const War& war = s.wars[wi];
            if (war.ended != kNever) continue;
            for (std::uint32_t q : war.attackers) if (q < np) crown_at_war[q] = 1.0;
            for (std::uint32_t q : war.defenders) if (q < np) crown_at_war[q] = 1.0;
            const std::uint32_t a = war.attackers.front(), d = war.defenders.front();
            if (a < np) losing[a] = std::max(losing[a], -war.score);
            if (d < np) losing[d] = std::max(losing[d], war.score);
        }
        std::vector<std::uint32_t> defiant;  // provinces whose governors refused recall
        for (std::size_t id = 1; id < np; ++id) {
            if (!provincial[id]) continue;
            const Polity& crown = s.polities[id];
            const Ruler* king = crown.ruler != 0 && crown.ruler < s.rulers.size() ? &s.rulers[crown.ruler] : nullptr;
            const bool new_reign = king != nullptr && king->reign_start + kTicksPerYear > ctx.tick && king->reign_start <= ctx.tick;
            // Foreign realms that think ill of the crown court its governors.
            std::vector<CellId> courting;
            for (const Relation& r : s.relations) {
                if ((r.a != id && r.b != id) || r.opinion >= GovernorPolitics::kCourtOpinion) continue;
                const std::uint32_t q = r.a == id ? r.b : r.a;
                if (q < s.polities.size() && s.polities[q].ended == kNever && s.polities[q].overlord != id) courting.push_back(s.polities[q].capital);
            }
            for (std::uint32_t v : seats_of[id]) {
                Province& pv = s.provinces[v];
                if (pv.ended != kNever) continue;
                // A strong crown at peace recalls a wayward governor.
                if (pv.governor != 0 && pv.loyalty >= 0.0 && pv.loyalty < kAutonomyBelow && crown.stability >= kRecallStability &&
                    crown_at_war[id] == 0.0) {
                    RngStream rr = ctx.rng(Stream::Polity, kRecallEntity | v);
                    if (rr.uniform() < kRecall) {
                        const double ambition = s.figures[pv.governor].ambition;
                        if (ambition > kDefiant && rr.uniform() < 2.0 * (ambition - kDefiant)) {
                            defiant.push_back(v);
                        } else {
                            pv.governor = 0;
                            pv.loyalty = -1.0;  // the new governor starts at the target
                        }
                    }
                }
                const std::uint8_t tenure = crown.laws[Laws::Tenure];
                // Appointed governors serve a term; then another is sent.
                if (tenure == 0 && pv.governor != 0 && ctx.tick - pv.governor_since >= Laws::kTermYears * kTicksPerYear) {
                    pv.governor = 0;
                    pv.held_back = 0.0;
                }
                if (pv.governor == 0 || !figure_alive(s.figures[pv.governor], ctx.tick)) {
                    const bool heir = tenure == 2 && pv.governor != 0;  // the office passes to his heir
                    RngStream fr = ctx.rng(Stream::Figures, kGovernorEntity | v);
                    pv.governor = record_person(s, content, pv.seat, ctx.tick, heir ? 20 : 30, 55, kRoleGovernor, fr);
                    if (!heir) {
                        pv.governor_since = ctx.tick;
                        pv.held_back = 0.0;
                    }
                    s.figures[pv.governor].polity = static_cast<std::uint32_t>(id);
                }
                if (new_reign && pv.loyalty >= 0.0) pv.loyalty = std::max(0.0, pv.loyalty - kNewReignLoyalty);
                const Polity& pol = s.polities[id];
                const Figure& gov = s.figures[pv.governor];
                const Cohort* lead = lead_cohort(s, pv.seat);
                const bool same_people = lead != nullptr && s.cultures[lead->culture].root == s.cultures[pol.culture].root;
                const bool same_faith = !s.faith_of.empty() && s.faith_of[pv.seat] == pol.religion;
                const double target = kLoyaltyBase + kLoyaltyStability * pol.stability + kLoyaltyControl * control_of(id, pv.seat) +
                                      (same_people ? kLoyaltyPeople : 0.0) + (same_faith ? kLoyaltyFaith : 0.0) +
                                      kLoyaltyAmbition * (0.5 - gov.ambition) -
                                      Authority::kAuthorityLoyalty * (1.0 - pol.authority) -
                                      (std::any_of(courting.begin(), courting.end(),
                                                   [&](CellId cap) { return w.grid.distance_km(cap, pv.seat) <= GovernorPolitics::kCourtKm; })
                                           ? GovernorPolitics::kCourted * gov.ambition
                                           : 0.0) +
                                      kLoyaltyAbility * ((king != nullptr ? king->ability : 0.5) - 0.5) -
                                      kLoyaltyDefeat * std::clamp(losing[id], 0.0, 1.0) -
                                      kLoyaltyTenure * std::min(1.0, static_cast<double>(ctx.tick - pv.governor_since) /
                                                                         (kTenureYears * kTicksPerYear)) +
                                      (tenure == 0 ? Laws::kAppointedLoyalty : tenure == 2 ? -Laws::kHereditaryLoyalty : 0.0);
                pv.loyalty = std::clamp(pv.loyalty < 0.0 ? target : pv.loyalty + kLoyaltyRate * (target - pv.loyalty), 0.0, 1.0);
                // What the governor keeps back for himself.
                const double hold = std::clamp(gov.ambition - pv.loyalty, 0.0, Laws::kHoldCap[std::min<std::uint8_t>(crown.laws[Laws::Taxes], 2)]);
                pv.held_back += Laws::kHoldRate * (hold - pv.held_back);
            }
        }
        // ---- The crown's authority recovers, the more under an able ruler.
        for (std::size_t id = 1; id < np; ++id) {
            Polity& pol = s.polities[id];
            if (pol.ended != kNever) continue;
            const double ability = pol.ruler != 0 && pol.ruler < s.rulers.size() ? s.rulers[pol.ruler].ability : 0.5;
            pol.authority = std::min(1.0, pol.authority + Authority::kAuthorityRecover * (0.5 + ability));
        }
        // ---- Governors' politics: what they think of each other, and intrigue.
        {
            std::vector<SimState::GovernorTie> ties;
            auto old_tie = [&](std::uint32_t a, std::uint32_t b) -> const SimState::GovernorTie* {
                const auto it = std::lower_bound(s.governor_ties.begin(), s.governor_ties.end(), std::pair{a, b},
                                                 [](const SimState::GovernorTie& t, const std::pair<std::uint32_t, std::uint32_t>& k) {
                                                     return t.a != k.first ? t.a < k.first : t.b < k.second;
                                                 });
                return it != s.governor_ties.end() && it->a == a && it->b == b ? &*it : nullptr;
            };
            for (std::size_t id = 1; id < np; ++id) {
                if (!provincial[id] || s.polities[id].ended != kNever) continue;
                std::vector<std::uint32_t> live;
                for (std::uint32_t v : seats_of[id]) {
                    if (s.provinces[v].ended == kNever && s.provinces[v].governor != 0 && s.provinces[v].loyalty >= 0.0) live.push_back(v);
                }
                std::sort(live.begin(), live.end());
                for (std::size_t i = 0; i < live.size(); ++i) {
                    for (std::size_t j = i + 1; j < live.size(); ++j) {
                        const Province& pa = s.provinces[live[i]];
                        const Province& pb = s.provinces[live[j]];
                        const Figure& ga = s.figures[pa.governor];
                        const Figure& gb = s.figures[pb.governor];
                        const Cohort* ca = lead_cohort(s, pa.seat);
                        const Cohort* cb = lead_cohort(s, pb.seat);
                        const bool kin = ca != nullptr && cb != nullptr && s.cultures[ca->culture].root == s.cultures[cb->culture].root;
                        const double target = std::clamp(
                            (w.grid.distance_km(pa.seat, pb.seat) <= GovernorPolitics::kTieNearKm ? GovernorPolitics::kTieNear : 0.0) +
                                (kin ? GovernorPolitics::kTieKin : 0.0) +
                                (pa.loyalty < Laws::kPactLoyalty && pb.loyalty < Laws::kPactLoyalty ? GovernorPolitics::kTieCause : 0.0) -
                                (ga.ambition >= GovernorPolitics::kTieAmbition && gb.ambition >= GovernorPolitics::kTieAmbition
                                     ? GovernorPolitics::kTieRivals
                                     : 0.0),
                            -1.0, 1.0);
                        SimState::GovernorTie t{live[i], live[j], pa.governor, pb.governor, target};
                        const SimState::GovernorTie* was = old_tie(live[i], live[j]);
                        if (was != nullptr && was->gov_a == pa.governor && was->gov_b == pb.governor) {
                            t.opinion = was->opinion + GovernorPolitics::kTieRate * (target - was->opinion);
                        }
                        ties.push_back(t);
                    }
                }
                // Rivals intrigue at court.
                const Polity& crown = s.polities[id];
                if (crown.stability < GovernorPolitics::kDenounceStability || crown.laws[Laws::Tenure] == 2) continue;
                RngStream rng = ctx.rng(Stream::Polity, kIntrigueEntity | static_cast<std::uint32_t>(id));
                for (const SimState::GovernorTie& t : ties) {
                    if (s.provinces[t.a].polity != id || t.opinion >= GovernorPolitics::kRivalTie) continue;
                    Province& pa = s.provinces[t.a];
                    Province& pb = s.provinces[t.b];
                    if (pa.ended != kNever || pb.ended != kNever) continue;
                    const bool a_accuses = s.figures[pa.governor].ambition >= s.figures[pb.governor].ambition;
                    Province& accuser = a_accuses ? pa : pb;
                    Province& accused = a_accuses ? pb : pa;
                    if (rng.uniform() >= GovernorPolitics::kDenounce * s.figures[accuser.governor].ambition) continue;
                    s.events.push_back({ctx.tick, EventType::Denounced, accused.seat,
                                        static_cast<std::int64_t>((static_cast<std::uint64_t>(accuser.id) << 32) | accused.id)});
                    if (s.figures[accused.governor].ambition > kDefiant && accused.loyalty < kStayLoyal) {
                        defiant.push_back(accused.id);  // he will not go quietly
                        continue;
                    }
                    RngStream fr = ctx.rng(Stream::Figures, kReplaceEntity | (accused.id & 0x007fffffu));
                    accused.governor = record_person(s, content, accused.seat, ctx.tick, 30, 55, kRoleGovernor, fr);
                    s.figures[accused.governor].polity = static_cast<std::uint32_t>(id);
                    accused.governor_since = ctx.tick;
                    accused.held_back = 0.0;
                    accused.loyalty = std::max(0.0, accused.loyalty - GovernorPolitics::kDenounceLoyalty);
                }
            }
            std::sort(ties.begin(), ties.end(), [](const SimState::GovernorTie& x, const SimState::GovernorTie& y) {
                return x.a != y.a ? x.a < y.a : x.b < y.b;
            });
            s.governor_ties.swap(ties);
        }
        // ---- Laws: the crown reforms, concedes, and faces its governors' pacts.
        std::size_t writing_t = content.techniques.size(), scribes_t = content.techniques.size();
        for (std::size_t k = 0; k < content.techniques.size(); ++k) {
            if (content.techniques[k].id == "writing") writing_t = k;
            if (content.techniques[k].id == "bureaucracy") scribes_t = k;
        }
        for (std::size_t id = 1; id < np; ++id) {
            Polity& crown = s.polities[id];
            if (crown.ended != kNever) continue;
            const std::uint64_t known = techniques_in(s, crown.capital);
            // Can a law stand at `level`?
            auto allowed = [&](std::size_t law, std::uint8_t level) {
                if (law == Laws::Tenure && level == 0) return writing_t < 64 && ((known >> writing_t) & 1u) != 0;
                if (law == Laws::Taxes && level == 0) return scribes_t < 64 && ((known >> scribes_t) & 1u) != 0;
                if (law == Laws::Army && level == 0) return has_institution(crown.institutions, Institution::Army);
                if (law == Laws::Succession && level == 1) return has_institution(crown.institutions, Institution::Council);
                return true;
            };
            auto change = [&](std::size_t law, std::uint8_t level) {
                crown.laws[law] = level;
                s.events.push_back({ctx.tick, EventType::LawChanged, crown.capital,
                                    static_cast<std::int64_t>((static_cast<std::uint64_t>(id) << 16) | (law << 8) | level)});
            };
            // A law whose prerequisite is gone falls back (noted only in states).
            const bool state = polity_has_writing(w, s, static_cast<std::uint32_t>(id));
            for (std::size_t law = 0; law < 4; ++law) {
                if (allowed(law, crown.laws[law])) continue;
                const auto to = static_cast<std::uint8_t>(law == Laws::Succession ? 0 : crown.laws[law] + 1);
                if (state) {
                    change(law, to);
                } else {
                    crown.laws[law] = to;
                }
            }
            if (!provincial[id]) continue;
            // One step toward the centre or away from it, if any law can move.
            auto centralize = [&]() {
                for (std::size_t law : {std::size_t{Laws::Taxes}, std::size_t{Laws::Army}, std::size_t{Laws::Tenure}, std::size_t{Laws::Succession}}) {
                    const std::uint8_t now = crown.laws[law];
                    if (now == 0) continue;
                    const std::uint8_t to = law == Laws::Succession ? 0 : static_cast<std::uint8_t>(now - 1);
                    if (!allowed(law, to)) continue;
                    change(law, to);
                    return true;
                }
                return false;
            };
            auto decentralize = [&]() {
                for (std::size_t law : {std::size_t{Laws::Tenure}, std::size_t{Laws::Taxes}, std::size_t{Laws::Army}, std::size_t{Laws::Succession}}) {
                    const std::uint8_t now = crown.laws[law];
                    if (now == 2) continue;
                    std::uint8_t to = static_cast<std::uint8_t>(now + 1);
                    if (!allowed(law, to)) to = 2;  // no council to elect: the sons share it out
                    change(law, to);
                    return true;
                }
                return false;
            };
            auto sway = [&](double by, const std::vector<std::uint32_t>& who) {
                for (std::uint32_t v : who) {
                    Province& pv = s.provinces[v];
                    if (pv.ended == kNever && pv.loyalty >= 0.0) pv.loyalty = std::clamp(pv.loyalty + by, 0.0, 1.0);
                }
            };
            RngStream rng = ctx.rng(Stream::Polity, kLawEntity | static_cast<std::uint32_t>(id));
            const Ruler* king = crown.ruler != 0 && crown.ruler < s.rulers.size() ? &s.rulers[crown.ruler] : nullptr;
            const bool child = king != nullptr && ruler_age(*king, ctx.tick) < RulerSystem::kAdultAge;
            // The governors' pact, against the rest of the realm.
            std::vector<double> people_of(s.provinces.size(), 0.0);
            double crown_people = 0.0, pact_people = 0.0;
            for (CellId c : held[id]) {
                if (s.polity_of[c] != id) continue;
                const std::uint32_t v = s.province_of[c];
                if (v != 0 && v < people_of.size()) people_of[v] += static_cast<double>(s.cell_population[c]);
            }
            std::vector<std::uint32_t> pact;
            for (CellId c : held[id]) {
                if (s.polity_of[c] == id) crown_people += static_cast<double>(s.cell_population[c]);
            }
            // A pact is made among friends: from each disloyal, ambitious
            // governor, those who think well of him and waver, and theirs in
            // turn. The largest such circle presses the crown.
            {
                auto tie = [&](std::uint32_t a, std::uint32_t b) {
                    const auto [x, y] = a < b ? std::pair{a, b} : std::pair{b, a};
                    const auto it = std::lower_bound(s.governor_ties.begin(), s.governor_ties.end(), std::pair{x, y},
                                                     [](const SimState::GovernorTie& t, const std::pair<std::uint32_t, std::uint32_t>& k) {
                                                         return t.a != k.first ? t.a < k.first : t.b < k.second;
                                                     });
                    return it != s.governor_ties.end() && it->a == x && it->b == y ? it->opinion : 0.0;
                };
                std::vector<std::uint32_t> live;
                for (std::uint32_t v : seats_of[id]) {
                    const Province& pv = s.provinces[v];
                    if (pv.ended == kNever && pv.governor != 0 && pv.loyalty >= 0.0) live.push_back(v);
                }
                std::vector<std::uint8_t> in_circle(live.size(), 0);
                double best = 0.0;
                for (std::size_t i = 0; i < live.size(); ++i) {
                    const Province& seed = s.provinces[live[i]];
                    if (in_circle[i] || seed.loyalty >= Laws::kPactLoyalty || s.figures[seed.governor].ambition < Laws::kPactAmbition) continue;
                    std::vector<std::uint32_t> circle{live[i]};
                    in_circle[i] = 1;
                    for (std::size_t k = 0; k < circle.size(); ++k) {
                        for (std::size_t j = 0; j < live.size(); ++j) {
                            if (in_circle[j] || s.provinces[live[j]].loyalty >= GovernorPolitics::kWaver) continue;
                            if (tie(circle[k], live[j]) >= GovernorPolitics::kPactTie) {
                                in_circle[j] = 1;
                                circle.push_back(live[j]);
                            }
                        }
                    }
                    double weight = 0.0;
                    for (std::uint32_t v : circle) weight += people_of[v] * (0.5 + s.provinces[v].held_back);
                    if (weight > best) best = weight, pact = circle;
                }
                pact_people = best;
                for (std::uint32_t v : pact) crown_people -= people_of[v];
            }
            const double draw = rng.uniform();
            if (pact.size() >= 2 && pact_people >= Laws::kPactRatio * crown_people && draw < Laws::kPactAct) {
                if (pact_people >= crown_people || rng.uniform() < Laws::kYield) {
                    if (decentralize()) sway(Laws::kConcessionLoyalty, pact);
                } else {
                    // Refused, they rise together under the boldest of them.
                    std::uint32_t lead = pact.front();
                    for (std::uint32_t v : pact) {
                        if (s.figures[s.provinces[v].governor].ambition > s.figures[s.provinces[lead].governor].ambition) lead = v;
                    }
                    const std::uint32_t league = found(s.provinces[lead].seat, static_cast<std::uint32_t>(id), crown.nomadic);
                    s.polities[league].founder = s.provinces[lead].governor;
                    for (CellId c : held[id]) {
                        const std::uint32_t v = s.province_of[c];
                        if (s.polity_of[c] == id && std::find(pact.begin(), pact.end(), v) != pact.end()) {
                            s.polity_of[c] = league;
                            s.province_of[c] = 0;
                        }
                    }
                    for (std::uint32_t v : pact) {
                        s.provinces[v].ended = ctx.tick;
                        s.provinces[v].became = league;
                    }
                    s.events.push_back({ctx.tick, EventType::GovernorsRise, s.provinces[lead].seat, static_cast<std::int64_t>(league)});
                    lose_authority(s, static_cast<std::uint32_t>(id), Authority::kAuthorityProvince * static_cast<double>(pact.size()));
                    start_war(w, s, ctx.tick, league, static_cast<std::uint32_t>(id), WarGoal::Independence);
                    s.polities[id].stability = std::max(0.0, s.polities[id].stability - kRevoltShock);
                }
                continue;
            }
            // A weak crown concedes; a strong one at peace reforms.
            const double weakness = std::max({child ? 1.0 : 0.0, std::clamp(losing[id], 0.0, 1.0),
                                              std::clamp((Laws::kWeakStability - crown.stability) / Laws::kWeakStability, 0.0, 1.0)});
            if (weakness > 0.0 && draw < Laws::kPactAct + Laws::kConcede * weakness) {
                if (decentralize()) sway(Laws::kConcessionLoyalty, seats_of[id]);
            } else if (king != nullptr && !child && crown_at_war[id] == 0.0 && crown.stability >= Laws::kReformStability &&
                       rng.uniform() < Laws::kReform * king->ability * crown.stability) {
                if (centralize()) sway(-Laws::kReformLoyalty, seats_of[id]);
            }
        }
        for (std::size_t id = 1; id < np; ++id) {
            if (!provincial[id]) continue;
            for (std::uint32_t v : seats_of[id]) {
                Province& pv = s.provinces[v];
                const bool refused = std::find(defiant.begin(), defiant.end(), v) != defiant.end();
                if (pv.ended != kNever || (pv.loyalty >= kRevoltBelow && !refused) || s.polities[id].ended != kNever) continue;
                RngStream rng = ctx.rng(Stream::Polity, kRevoltEntity | v);
                const double ambition = s.figures[pv.governor].ambition;
                if (!refused && rng.uniform() >= kRevolt * (kRevoltBelow - pv.loyalty) / kRevoltBelow * (0.5 + ambition)) continue;
                // The governor breaks away with the province and founds a house.
                const std::uint32_t rebel = found(pv.seat, static_cast<std::uint32_t>(id), s.polities[id].nomadic);
                s.polities[rebel].founder = pv.governor;
                for (CellId c : held[id]) {
                    if (s.province_of[c] == v && s.polity_of[c] == id) {
                        s.polity_of[c] = rebel;
                        s.province_of[c] = 0;
                    }
                }
                pv.ended = ctx.tick;
                pv.became = rebel;
                s.events.push_back({ctx.tick, EventType::ProvinceRevolt, pv.seat, static_cast<std::int64_t>(v)});
                start_war(w, s, ctx.tick, rebel, static_cast<std::uint32_t>(id), WarGoal::Independence);
                // The realm is shaken, and the other governors take note.
                lose_authority(s, static_cast<std::uint32_t>(id), Authority::kAuthorityProvince);
                Polity& realm = s.polities[id];
                realm.stability = std::max(0.0, realm.stability - kRevoltShock);
                for (std::uint32_t o : seats_of[id]) {
                    if (o != v && s.provinces[o].ended == kNever) {
                        s.provinces[o].loyalty = std::max(0.0, s.provinces[o].loyalty - kRevoltContagion);
                    }
                }
            }
        }
    }

    // ---- Generals and high priests: appointed, famed and fallen; coups.
    {
        if (s.commanders.empty()) s.commanders.emplace_back();
        std::vector<std::uint8_t> warring(np, 0);
        for (const War& war : s.wars) {
            if (war.id == 0 || war.ended != kNever) continue;
            for (std::uint32_t a : war.attackers) if (a < np) warring[a] = 1;
            for (std::uint32_t d : war.defenders) if (d < np) warring[d] = 1;
        }
        // Each realm's living field army: its general and the battles it has won.
        std::vector<std::uint32_t> army_wins(np, 0), army_general(np, 0);
        for (const Army& a : s.armies) {
            if (a.to != kNever || a.polity == 0 || a.polity >= np) continue;
            army_wins[a.polity] = std::max(army_wins[a.polity], a.victories);
            army_general[a.polity] = a.general;
        }
        auto retire = [&](Polity& pol) {
            if (pol.commander != 0 && pol.commander < s.commanders.size()) s.commanders[pol.commander].to = ctx.tick;
            pol.commander = 0;
        };
        for (std::size_t id = 1; id < np; ++id) {
            Polity& pol = s.polities[id];
            const bool state = pol.ended == kNever && polity_has_writing(w, s, static_cast<std::uint32_t>(id));
            // The general: while at war or keeping a standing army.
            const bool needs_general = state && (warring[id] || has_institution(pol.institutions, Institution::Army));
            if (pol.commander != 0) {
                Commander& m = s.commanders[pol.commander];
                if (!needs_general || !figure_alive(s.figures[m.figure], ctx.tick)) {
                    retire(pol);
                } else {
                    m.fame *= 1.0 - kFameFade;
                }
            }
            if (needs_general && pol.commander == 0) {
                Commander m;
                RngStream fr = ctx.rng(Stream::Figures, kGeneralEntity | static_cast<std::uint32_t>(s.commanders.size() & 0x03ffffffu));
                m.figure = record_person(s, content, pol.capital, ctx.tick, 30, 55, kRoleGeneral, fr);
                s.figures[m.figure].polity = static_cast<std::uint32_t>(id);
                m.polity = static_cast<std::uint32_t>(id);
                m.from = ctx.tick;
                pol.commander = static_cast<std::uint32_t>(s.commanders.size());
                s.commanders.push_back(m);
            }
            // The high priest: where there is a priesthood or an organized faith.
            const bool needs_priest = state && (has_institution(pol.institutions, Institution::Temple) ||
                                                religion_organized(w, s, pol.religion));
            if (!needs_priest) {
                pol.priest = 0;
            } else if (pol.priest == 0 || pol.priest >= s.figures.size() || !figure_alive(s.figures[pol.priest], ctx.tick)) {
                RngStream fr = ctx.rng(Stream::Figures, kPriestEntity | static_cast<std::uint32_t>(id & 0x01ffffffu));
                pol.priest = record_person(s, content, pol.capital, ctx.tick, 35, 60, kRolePriest, fr);
                s.figures[pol.priest].polity = static_cast<std::uint32_t>(id);
            }
            // A famous, ambitious general may seize the throne of a shaky realm.
            if (pol.commander == 0 || pol.rebel != 0 || pol.ruler == 0 || pol.ruler >= s.rulers.size()) continue;
            Commander& m = s.commanders[pol.commander];
            if (m.fame <= 0.0) continue;
            const Figure& general = s.figures[m.figure];
            const Ruler& king = s.rulers[pol.ruler];
            const double regency = ruler_age(king, ctx.tick) < RulerSystem::kAdultAge ? 2.0 : 1.0;
            const double loyal_army = army_wins[id] != 0 && army_general[id] == m.figure ? std::min(2.0, static_cast<double>(army_wins[id])) : 0.0;
            const double chance = kCoup * m.fame * (0.5 + general.ambition) * (1.5 - pol.stability) * (1.5 - king.ability) *
                                  regency * (1.0 + kArmyCoup * loyal_army);
            RngStream rng = ctx.rng(Stream::Polity, kCoupEntity | static_cast<std::uint32_t>(id));
            if (rng.uniform() >= chance) continue;
            pol.rebel = m.figure;  // takes the throne next year (RulerSystem), if he lives
        }
    }

    // ---- Peasant uprisings: unrest in each province and in the crown's land
    // of every state; the people may rise.
    {
        std::vector<std::uint8_t> at_war(np, 0);
        for (const War& war : s.wars) {
            if (war.id == 0 || war.ended != kNever) continue;
            for (std::uint32_t a : war.attackers) if (a < np) at_war[a] = 1;
            for (std::uint32_t d : war.defenders) if (d < np) at_war[d] = 1;
        }
        std::vector<std::vector<CellId>> held(np);
        for (CellId c = 0; c < n; ++c) {
            const std::uint32_t q = s.polity_of[c];
            if (q != 0 && q < np) held[q].push_back(c);
        }
        for (std::size_t id = 1; id < np; ++id) {
            Polity& pol0 = s.polities[id];
            if (pol0.ended != kNever || held[id].empty() || !polity_has_writing(w, s, static_cast<std::uint32_t>(id))) {
                pol0.unrest = 0.0;
                continue;
            }
            // The regions: the crown's land (key 0) and each province.
            std::vector<std::pair<std::uint32_t, std::vector<CellId>>> regions;
            regions.emplace_back(0u, std::vector<CellId>{});
            for (CellId c : held[id]) {
                const std::uint32_t v = s.province_of.size() == n ? s.province_of[c] : 0;
                std::size_t r = 0;
                while (r < regions.size() && regions[r].first != v) ++r;
                if (r == regions.size()) regions.emplace_back(v, std::vector<CellId>{});
                regions[r].second.push_back(c);
            }
            for (auto& [v, region] : regions) {
                if (region.empty()) continue;
                Polity& pol = s.polities[id];
                const bool crown = v == 0;
                Province* pv = crown ? nullptr : &s.provinces[v];
                const CellId seat = crown ? pol.capital : pv->seat;
                // What they suffer.
                double people = 0.0, unfed = 0.0, fed = 0.0;
                for (CellId c : region) {
                    const double p = static_cast<double>(s.cell_population[c]);
                    people += p;
                    fed += s.env.capacity[c];
                    unfed += std::max(0.0, p - s.env.capacity[c]);
                }
                if (people <= 0.0) continue;
                const double crowding = fed > 0.0 ? std::clamp((people / fed - kCrowdedFrom) / kCrowdedSpan, 0.0, 1.0) : 1.0;
                const Cohort* lead = lead_cohort(s, seat);
                const bool foreign = lead != nullptr && s.cultures[lead->culture].root != s.cultures[pol.culture].root;
                const bool other_faith = !s.faith_of.empty() && s.faith_of[seat] != pol.religion;
                const Figure* head = crown ? reigning_person(s, static_cast<std::uint32_t>(id))
                                           : (pv->governor != 0 ? &s.figures[pv->governor] : nullptr);
                const double ability = head != nullptr ? head->ability : 0.5;
                // A zealous high priest stirs the people of other faiths.
                const double zeal = pol.priest != 0 && pol.priest < s.figures.size()
                                        ? kPriestZeal * std::max(0.0, 2.0 * (s.figures[pol.priest].piety - 0.5))
                                        : 0.0;
                const double target = std::clamp(
                    kUnrestHunger * unfed / people + kUnrestCrowding * crowding +
                        (at_war[id] ? kUnrestAtWar : 0.0) + kUnrestWar * std::min(1.0, pol.shock) +
                        kUnrestPlague * pol.pressure[static_cast<std::size_t>(Pressure::Epidemic)] +
                        (pol.bond == Bond::Tributary ? kUnrestTribute : 0.0) + (foreign ? kUnrestForeign : 0.0) +
                        (other_faith ? kUnrestFaith + zeal : 0.0) + kUnrestInept * (0.5 - ability),
                    0.0, 1.0);
                double& unrest = crown ? pol.unrest : pv->unrest;
                unrest += kUnrestRate * (target - unrest);
                if (unrest <= kRiseAbove || pol.rebel != 0) continue;
                RngStream rng = ctx.rng(Stream::Polity, kUprisingEntity | (static_cast<std::uint32_t>(id) << 8) | (v & 0xffu));
                if (rng.uniform() >= kRise * (unrest - kRiseAbove) / (1.0 - kRiseAbove)) continue;
                // They rise. Rebels against the realm's force there.
                const double rebels = people * unrest * kRebelShare;
                const double force = kGarrison * strength[id] * control_of(id, seat) * (0.5 + pol.stability) *
                                     (has_institution(pol.institutions, Institution::Army) ? kArmyGarrison : 1.0);
                Uprising u;
                u.tick = ctx.tick;
                u.polity = static_cast<std::uint32_t>(id);
                u.province = v;
                u.cell = seat;
                u.rebels = static_cast<std::int64_t>(rebels);
                RngStream fr = ctx.rng(Stream::Figures, kRebelEntity | static_cast<std::uint32_t>(s.uprisings.size() & 0x07ffffffu));
                u.leader = record_person(s, content, seat, ctx.tick, 20, 45, kRoleRebel, fr);
                s.figures[u.leader].polity = static_cast<std::uint32_t>(id);
                const bool crushed = rng.uniform() < force / (force + rebels);
                const double die = crushed ? kCrushDeaths : kRebelDeaths;
                for (CellId c : region) u.dead += kill_in_strife(s, c, die, rng);
                if (crushed) {
                    u.outcome = UprisingOutcome::Crushed;
                    unrest *= kCrushCalm;
                    pol.stability = std::max(0.0, pol.stability - kUprisingShock);
                } else if (crown || rebels > kOverthrowRatio * force) {
                    // The dynasty falls; the leader takes the throne next year.
                    u.outcome = UprisingOutcome::Overthrew;
                    pol.rebel = u.leader;
                    unrest *= kCrushCalm;
                    pol.stability = std::max(0.0, pol.stability - kUprisingWinShock);
                } else {
                    // The province goes its own way under the leader.
                    u.outcome = UprisingOutcome::BrokeAway;
                    const bool nomadic = pol.nomadic;
                    const std::uint32_t free = found(seat, static_cast<std::uint32_t>(id), nomadic);
                    s.polities[free].founder = u.leader;
                    for (CellId c : region) {
                        if (s.polity_of[c] != id) continue;
                        s.polity_of[c] = free;
                        if (s.province_of.size() == n) s.province_of[c] = 0;
                    }
                    s.provinces[v].ended = ctx.tick;
                    s.provinces[v].became = free;
                    u.became = free;
                    start_war(w, s, ctx.tick, free, static_cast<std::uint32_t>(id), WarGoal::Independence);
                    Polity& realm = s.polities[id];
                    realm.stability = std::max(0.0, realm.stability - kUprisingWinShock);
                }
                s.uprisings.push_back(u);
                s.events.push_back({ctx.tick, EventType::PeasantUprising, seat, static_cast<std::int64_t>(s.uprisings.size() - 1)});
            }
        }
    }

    // ---- Civil wars: over when one side alone is left, or the wars among them are.
    for (std::size_t i = 1; i < s.civil_wars.size(); ++i) {
        CivilWar& cw = s.civil_wars[i];
        if (cw.outcome != CivilWarOutcome::Ongoing || cw.started == ctx.tick) continue;
        std::vector<std::uint32_t> sides{cw.realm};
        sides.insert(sides.end(), cw.factions.begin(), cw.factions.end());
        std::vector<std::uint32_t> left;
        for (std::uint32_t p : sides) {
            if (p < s.polities.size() && s.polities[p].ended == kNever) left.push_back(p);
        }
        bool fighting = false;
        for (std::size_t a = 0; a < left.size() && !fighting; ++a) {
            for (std::size_t b = a + 1; b < left.size() && !fighting; ++b) fighting = war_between(s, left[a], left[b]) != 0;
        }
        if (left.size() > 1 && fighting) continue;
        cw.ended = ctx.tick;
        cw.outcome = left.size() == 1 ? CivilWarOutcome::Reunified : CivilWarOutcome::Divided;
        cw.victor = left.size() == 1 ? left.front() : 0;
        const std::uint32_t at = left.empty() ? cw.realm : left.front();
        s.events.push_back({ctx.tick, EventType::CivilWarEnded, s.polities[at].capital, static_cast<std::int64_t>(i)});
    }

    // ---- Break-up of fragile polities, and succession disputes.
    std::size_t writing_k = 64, bureaucracy_k = 64;
    for (std::size_t k = 0; k < content.techniques.size() && k < 64; ++k) {
        if (content.techniques[k].id == "writing") writing_k = k;
        if (content.techniques[k].id == "bureaucracy") bureaucracy_k = k;
    }
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended != kNever) continue;
        const bool state = polity_has_writing(w, s, static_cast<std::uint32_t>(id));
        const double stability = s.polities[id].stability;
        const double fragile = stability < kFragile ? (kFragile - stability) / kFragile : 0.0;
        const double crisis = kCollapse * fragile * fragile;
        // A disputed succession (RulerSystem) that tore the realm apart.
        const double succession = s.polities[id].contested ? 1.0 : 0.0;
        s.polities[id].contested = false;
        RngStream rng = ctx.rng(Stream::Polity, kCollapseEntity | static_cast<std::uint32_t>(id));
        const double roll = rng.uniform();
        if (roll >= crisis + succession) continue;
        // A disputed succession is fought out: the claimant's party
        // takes the provinces least loyal to the crown (or the half of the
        // realm farthest from the capital) and fights for the throne.
        if (roll >= crisis && succession > 0.0 && s.polities[id].bond == Bond::None) {
            const std::uint32_t realm = static_cast<std::uint32_t>(id);
            std::vector<CellId> land;
            for (CellId c = 0; c < n; ++c) {
                if (s.polity_of[c] == realm) land.push_back(c);
            }
            if (land.size() >= std::max<std::size_t>(2, w.cells_for(kPretenderMinCells))) {
                const CellId capital = s.polities[id].capital;
                std::vector<CellId> theirs;
                CellId seat = kInvalidCell;
                if (provincial[id]) {
                    // Provinces below kStayLoyal side with the claimant; at least the least loyal one.
                    std::uint32_t worst = 0;
                    for (const Province& pv : s.provinces) {
                        if (pv.id == 0 || pv.polity != realm || pv.ended != kNever) continue;
                        if (worst == 0 || pv.loyalty < s.provinces[worst].loyalty) worst = pv.id;
                    }
                    for (CellId c : land) {
                        const std::uint32_t v = s.province_of[c];
                        if (v != 0 && v < s.provinces.size() && (v == worst || s.provinces[v].loyalty < kStayLoyal)) theirs.push_back(c);
                    }
                    if (worst != 0) seat = s.provinces[worst].seat;
                }
                if (theirs.empty()) {
                    std::vector<std::pair<double, CellId>> by_distance;
                    for (CellId c : land) by_distance.emplace_back(w.grid.distance_km(capital, c), c);
                    std::stable_sort(by_distance.begin(), by_distance.end(),
                                     [](const auto& a, const auto& b) { return a.first > b.first; });
                    for (std::size_t i = 0; i < by_distance.size() / 2; ++i) theirs.push_back(by_distance[i].second);
                }
                if (seat == kInvalidCell || std::find(theirs.begin(), theirs.end(), seat) == theirs.end()) {
                    seat = theirs.front();
                    for (CellId c : theirs) {
                        if (s.cell_population[c] > s.cell_population[seat]) seat = c;
                    }
                }
                std::uint32_t claimant = 0;
                for (auto e = s.events.rbegin(); e != s.events.rend() && e->tick == ctx.tick; ++e) {
                    if (e->type == EventType::SuccessionDisputed && e->cell == capital) {
                        claimant = static_cast<std::uint32_t>(e->value);
                        break;
                    }
                }
                const std::uint32_t pretender = found(seat, realm, s.polities[id].nomadic);
                s.polities[pretender].founder = claimant;
                s.polities[pretender].pretender_to = realm;
                s.polities[pretender].stability = kSuccessorStability;
                for (CellId c : theirs) {
                    s.polity_of[c] = pretender;
                    if (s.province_of.size() == n) s.province_of[c] = 0;
                }
                for (Province& pv : s.provinces) {
                    if (pv.id != 0 && pv.polity == realm && pv.ended == kNever && s.polity_of[pv.seat] == pretender) {
                        pv.ended = ctx.tick;
                        pv.became = pretender;
                    }
                }
                start_war(w, s, ctx.tick, pretender, realm, WarGoal::Succession);
                continue;
            }
        }
        // The cause: the heaviest pressure, unless the polity was sound.
        std::uint8_t cause = kSuccessionCrisis;
        if (roll < crisis) {
            double worst = 0.0;
            for (std::size_t k = 0; k < kPressureCount; ++k) {
                const double weight = kPressureWeight[k] * s.polities[id].pressure[k];
                if (weight > worst) worst = weight, cause = static_cast<std::uint8_t>(k);
            }
        }
        // A great realm's crisis is fought out among factions.
        if (cause != kSuccessionCrisis && s.polities[id].bond == Bond::None) {
            const std::uint32_t realm = static_cast<std::uint32_t>(id);
            std::vector<CellId> land;
            for (CellId c = 0; c < n; ++c) {
                if (s.polity_of[c] == realm) land.push_back(c);
            }
            if (provincial[id] || land.size() >= std::max<std::size_t>(3, w.cells_for(kCivilWarMinCells))) {
                const CellId seat0 = s.polities[id].capital;
                const double core_control = cause == static_cast<std::uint8_t>(Pressure::Epidemic) ? kEpidemicCore : kCivilCore;
                struct Faction {
                    CellId seat;
                    std::uint32_t province;  // 0 if none
                    double weight;
                };
                std::vector<Faction> sides;
                if (provincial[id]) {
                    std::vector<double> people(s.provinces.size(), 0.0);
                    for (CellId c : land) {
                        const std::uint32_t v = s.province_of[c];
                        if (v != 0 && v < people.size()) people[v] += static_cast<double>(s.cell_population[c]);
                    }
                    for (const Province& pv : s.provinces) {
                        if (pv.id == 0 || pv.polity != realm || pv.ended != kNever || pv.loyalty >= kStayLoyal) continue;
                        sides.push_back({pv.seat, pv.id, people[pv.id] * (0.5 + pv.held_back)});
                    }
                } else {
                    for (CellId c : land) {
                        if (c == seat0 || control_of(id, c) >= core_control) continue;
                        sides.push_back({c, 0, static_cast<double>(s.cell_population[c])});
                    }
                }
                std::stable_sort(sides.begin(), sides.end(), [](const Faction& a, const Faction& b) {
                    return a.weight > b.weight || (a.weight == b.weight && a.seat < b.seat);
                });
                std::vector<Faction> chosen;
                for (const Faction& f : sides) {
                    if (chosen.size() >= kMaxFactions || f.weight <= 0.0) break;
                    bool apart = f.province != 0;
                    if (!apart) {
                        apart = true;
                        for (const Faction& g : chosen) apart = apart && w.grid.distance_km(g.seat, f.seat) >= kFactionSpacingKm;
                    }
                    if (apart) chosen.push_back(f);
                }
                if (!chosen.empty()) {
                    CivilWar cw;
                    cw.id = static_cast<std::uint32_t>(std::max<std::size_t>(1, s.civil_wars.size()));
                    if (s.civil_wars.empty()) s.civil_wars.emplace_back();
                    cw.realm = realm;
                    cw.cause = cause;
                    cw.started = ctx.tick;
                    const bool nomads = s.polities[id].nomadic;
                    std::vector<std::uint32_t> faction(chosen.size());
                    for (std::size_t i = 0; i < chosen.size(); ++i) {
                        faction[i] = found(chosen[i].seat, realm, nomads);
                        Polity& f = s.polities[faction[i]];
                        f.pretender_to = realm;
                        f.stability = kSuccessorStability;
                        if (chosen[i].province != 0 && s.provinces[chosen[i].province].governor != 0) {
                            f.founder = s.provinces[chosen[i].province].governor;
                        } else {
                            RngStream fr = ctx.rng(Stream::Figures, kFactionEntity | (faction[i] & 0x00ffffffu));
                            const bool rebel = i == 0 && cause == static_cast<std::uint8_t>(Pressure::Famine);
                            f.founder = record_person(s, content, chosen[i].seat, ctx.tick, 25, 50, rebel ? kRoleRebel : kRoleClaimant, fr);
                            s.figures[f.founder].polity = faction[i];
                        }
                        cw.factions.push_back(faction[i]);
                    }
                    // The land: the crown keeps what it holds firmly and its
                    // loyal provinces; the rest goes to the nearest side that
                    // reaches it, or falls away.
                    for (CellId c : land) {
                        const std::uint32_t v = s.province_of.size() == n ? s.province_of[c] : 0;
                        if (provincial[id]) {
                            std::size_t side = chosen.size();
                            for (std::size_t i = 0; i < chosen.size(); ++i) {
                                if (v != 0 && v == chosen[i].province) side = i;
                            }
                            if (side < chosen.size()) {
                                s.polity_of[c] = faction[side];
                                s.province_of[c] = 0;
                            }
                            continue;
                        }
                        if (c == seat0 || control_of(id, c) >= core_control) continue;
                        std::size_t side = chosen.size();
                        double best = kMinControl;
                        for (std::size_t i = 0; i < chosen.size(); ++i) {
                            const double ctl = polity_control(w.grid.distance_km(chosen[i].seat, c),
                                                              std::max(polity_reach_km(w, s, chosen[i].seat), kProvinceReachKm));
                            if (ctl >= best) best = ctl, side = i;
                        }
                        s.polity_of[c] = side < chosen.size() ? faction[side] : 0;  // local strongmen go their own way
                    }
                    for (Province& pv : s.provinces) {
                        if (pv.id == 0 || pv.polity != realm || pv.ended != kNever) continue;
                        for (std::size_t i = 0; i < chosen.size(); ++i) {
                            if (pv.id == chosen[i].province) {
                                pv.ended = ctx.tick;
                                pv.became = faction[i];
                                if (pv.loyalty < 0.0) pv.loyalty = 0.0;
                            }
                        }
                    }
                    s.polities[id].stability = kCivilWarStability;
                    lose_authority(s, realm, Authority::kAuthorityProvince * static_cast<double>(faction.size()));
                    for (std::uint32_t f : faction) start_war(w, s, ctx.tick, f, realm, WarGoal::Succession);
                    s.civil_wars.push_back(cw);
                    s.events.push_back({ctx.tick, EventType::CivilWarBegan, seat0, static_cast<std::int64_t>(cw.id)});
                    continue;
                }
            }
        }
        const CellId capital = s.polities[id].capital;
        const bool nomadic = s.polities[id].nomadic;
        const double nomad_factor = nomad_reach_factor(w, s, static_cast<std::uint32_t>(id));
        // All its land, including any taken earlier this year.
        std::vector<CellId> mine;
        for (CellId c = 0; c < n; ++c) {
            if (s.polity_of[c] == id) mine.push_back(c);
        }
        // Does an heir hold the old capital and its core?
        const bool heir_holds = cause == kSuccessionCrisis || rng.uniform() < kHeirHolds;
        Polity& pol = s.polities[id];
        pol.ended = ctx.tick;
        pol.fate = PolityFate::Collapsed;
        pol.fate_cause = cause;
        s.events.push_back({ctx.tick, EventType::PolityEnded, capital, static_cast<std::int64_t>(id)});
        for (CellId c : mine) s.polity_of[c] = 0;
        // A realm with provinces breaks along them: each province not
        // solidly loyal goes its own way under its governor; the loyal ones
        // and the crown's land stay with the rump at the old capital.
        if (provincial[id]) {
            const std::uint32_t rump = found(capital, static_cast<std::uint32_t>(id), nomadic);
            s.polities[rump].stability = kSuccessorStability;
            std::vector<std::uint32_t> went(s.provinces.size(), 0);
            for (CellId c : mine) {
                const std::uint32_t v = s.province_of[c];
                Province* pv = v != 0 && v < s.provinces.size() && s.provinces[v].polity == id && s.provinces[v].ended == kNever
                                   ? &s.provinces[v]
                                   : nullptr;
                if (pv == nullptr || (heir_holds && pv->loyalty >= kStayLoyal)) {
                    s.polity_of[c] = rump;
                    continue;
                }
                if (went[v] == 0) {
                    went[v] = found(pv->seat, static_cast<std::uint32_t>(id), nomadic);
                    s.polities[went[v]].stability = kSuccessorStability;
                    s.polities[went[v]].founder = s.provinces[v].governor;
                }
                s.polity_of[c] = went[v];
                s.province_of[c] = 0;
            }
            for (Province& pv : s.provinces) {
                if (pv.id == 0 || pv.polity != id || pv.ended != kNever) continue;
                if (went[pv.id] != 0) {
                    pv.ended = ctx.tick;
                    pv.became = went[pv.id];
                } else {
                    pv.polity = rump;  // a loyal province serves the rump
                }
            }
            continue;
        }
        // A small chiefdom dissolves into its villages; a large one breaks
        // into smaller chiefdoms around its largest villages.
        if (!state && mine.size() < std::max<std::size_t>(2, w.cells_for(kSplitChiefdomCells))) continue;
        // A state leaves a core under its old capital. Elsewhere the realm
        // breaks into successors by how advanced each place is: the most
        // urban and literate seats choose first and hold as far as their own
        // administration reaches (a great city with scribes a province, a
        // market town its valley), lesser places with farming and chiefs
        // become chiefdoms, and the rest fall back to villages.
        std::vector<CellId> core, rest;
        for (CellId c : mine) (state && heir_holds && (control_of(id, c) >= kCoreControl || c == capital) ? core : rest).push_back(c);
        struct Seat {
            CellId cell;
            double score, reach;
        };
        std::vector<Seat> candidates;
        for (CellId c : rest) {
            const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            const double people = static_cast<double>(s.cell_population[c]);
            const double density = people / w.grid.area_km2(c);
            const double hierarchy = cell_trait(s, c, i_hierarchy);
            if (farming < kMinFarming || density < kMinDensity || hierarchy < kMinHierarchy) continue;
            const std::uint64_t known = techniques_in(s, c);
            const bool literate = writing_k < 64 && ((known >> writing_k) & 1u);
            const bool scribes = bureaucracy_k < 64 && ((known >> bureaucracy_k) & 1u);
            const double town = s.urban.empty() ? 0.0 : s.urban[c] * people;
            // The fallen court is discredited and its city sacked or deserted,
            // and every place's fortunes turn on its local strongmen.
            const double luck = kSeatLuckMin + (1.0 - kSeatLuckMin) * 2.0 * rng.uniform();
            const double score = (town + kSeatRural * people) * hierarchy * (literate ? kSeatWriting : 1.0) *
                                 (scribes ? kSeatBureaucracy : 1.0) * (c == capital ? kFallenCapital : 1.0) * luck;
            // A seat in a fallen state held a province under it: it keeps at
            // least a province's reach (its governor's, or a local dynasty's).
            const double own = polity_reach_km(w, s, c);
            candidates.push_back({c, score, (state ? std::max(own, kProvinceReachKm) : own) * nomad_factor});
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Seat& a, const Seat& b) {
            return a.score > b.score || (a.score == b.score && a.cell < b.cell);
        });
        // Greedily, strongest first: a place another seat already holds
        // (control at least kMinControl) follows that seat.
        std::vector<Seat> seats;
        auto held_by = [&](CellId c) {
            std::size_t best = seats.size();
            double most = 0.0;
            for (std::size_t i = 0; i < seats.size(); ++i) {
                const double ctl = polity_control(w.grid.distance_km(seats[i].cell, c), seats[i].reach);
                if (ctl > most) most = ctl, best = i;
            }
            return most >= kMinControl ? best : seats.size();
        };
        for (const Seat& cand : candidates) {
            if (held_by(cand.cell) == seats.size()) seats.push_back(cand);
        }
        if (state && heir_holds) {
            const std::uint32_t heir = found(capital, static_cast<std::uint32_t>(id), nomadic);
            s.polities[heir].stability = kSuccessorStability;
            for (CellId c : core) s.polity_of[c] = heir;
        }
        std::vector<std::uint32_t> successor(seats.size());
        for (std::size_t i = 0; i < seats.size(); ++i) {
            successor[i] = found(seats[i].cell, static_cast<std::uint32_t>(id), nomadic);
            s.polities[successor[i]].stability = kSuccessorStability;
        }
        for (CellId c : rest) {
            const std::size_t i = held_by(c);
            if (i < seats.size()) s.polity_of[c] = successor[i];
        }
    }

    // ---- Secession of land held too loosely, and civil war (realms with
    // provinces lose land by provincial revolts instead).
    for (std::size_t id = 1; id < np; ++id) {
        Polity& pol = s.polities[id];
        if (pol.ended != kNever || provincial[id]) continue;
        RngStream rng = ctx.rng(Stream::Polity, kCivilWarEntity | static_cast<std::uint32_t>(id));
        std::vector<CellId> mine;
        for (CellId c : cells[id]) {
            if (s.polity_of[c] == id) mine.push_back(c);
        }
        const double unrest = 2.0 * (1.0 - pol.stability);
        double mean_control = 0.0;
        for (CellId c : mine) {
            const double ctl = control_of(id, c);
            mean_control += ctl;
            if (c != pol.capital && ctl < kSecedeBelow && rng.uniform32() < kSecession * unrest) s.polity_of[c] = 0;
        }
        if (mine.size() >= w.cells_for(kCivilWarCells)) {
            mean_control /= static_cast<double>(mine.size());
            if (rng.uniform() < kCivilWar * (1.0 - mean_control) * unrest) {
                // The half farther from the capital breaks away under its
                // largest town.
                std::vector<std::pair<double, CellId>> by_distance;
                for (CellId c : mine) by_distance.emplace_back(w.grid.distance_km(pol.capital, c), c);
                std::stable_sort(by_distance.begin(), by_distance.end(),
                                 [](const auto& a, const auto& b) { return a.first > b.first; });
                const std::size_t half = by_distance.size() / 2;
                CellId seat = by_distance[0].second;
                for (std::size_t i = 0; i < half; ++i) {
                    if (s.cell_population[by_distance[i].second] > s.cell_population[seat]) seat = by_distance[i].second;
                }
                const std::uint32_t rebel = found(seat, static_cast<std::uint32_t>(id), pol.nomadic);
                for (std::size_t i = 0; i < half; ++i) s.polity_of[by_distance[i].second] = rebel;
                // The rest fight to keep them.
                start_war(w, s, ctx.tick, rebel, static_cast<std::uint32_t>(id), WarGoal::Independence);
            }
        }
    }

    // ---- Officials, garrisons and merchants of the rulers' people settle in
    // the provinces they hold well, so the rulers' language takes root there.
    {
        std::vector<Cohort> settlers;
        for (std::size_t id = 1; id < np; ++id) {
            const Polity& pol = s.polities[id];
            if (pol.ended != kNever) continue;
            Cohort* home = nullptr;
            for (auto i = s.cohort_begin[pol.capital]; i < s.cohort_begin[pol.capital + 1]; ++i) {
                Cohort& co = s.cohorts[i];
                if (co.language == pol.language && (home == nullptr || co.total() > home->total())) home = &co;
            }
            if (home == nullptr) continue;
            RngStream rng = ctx.rng(Stream::Polity, kOfficialsEntity | static_cast<std::uint32_t>(id));
            for (CellId c : cells[id]) {
                if (c == pol.capital || s.polity_of[c] != id || s.cell_population[c] == 0) continue;
                const double control = control_of(id, c);
                if (control < kMinControl) continue;
                const double want = kOfficials * control * static_cast<double>(s.cell_population[c]);
                const double share = want / static_cast<double>(std::max<std::int64_t>(1, home->total()));
                Cohort band = *home;
                band.cell = c;
                for (int b = 0; b < kAgeBins; ++b) {
                    band.female[b] = static_cast<std::uint32_t>(
                        std::min<std::int64_t>(home->female[b], stochastic_round(share * home->female[b], rng)));
                    band.male[b] = static_cast<std::uint32_t>(
                        std::min<std::int64_t>(home->male[b], stochastic_round(share * home->male[b], rng)));
                    home->female[b] -= band.female[b];
                    home->male[b] -= band.male[b];
                }
                if (band.total() > 0) {
                    s.ledger.migrants += band.total();
                    settlers.push_back(band);
                }
            }
        }
        if (!settlers.empty()) {
            s.cohorts.insert(s.cohorts.end(), settlers.begin(), settlers.end());
            s.normalize_cohorts(n);
        }
    }

    // ---- New polities, where farming, density, hierarchy and
    // circumscription come together.
    const std::int64_t year = ctx.tick / kTicksPerYear;
    if (year % kFormationYears == 0) {
        std::size_t riding = content.techniques.size();
        for (std::size_t k = 0; k < content.techniques.size(); ++k) {
            if (content.techniques[k].id == "horse_riding") riding = k;
        }
        std::uint32_t camels = 0;
        for (std::size_t sp = 0; sp < content.species.size(); ++sp) {
            if (content.species[sp].id == "wild_camels") camels = 1u << sp;
        }
        // Country for mounted nomads: open pasture where people ride, or
        // desert and dry scrub where they keep and ride camels.
        auto mounted_country = [&](CellId c) {
            if (riding >= content.techniques.size() || !((techniques_in(s, c) >> riding) & 1u)) return false;
            const auto biome = static_cast<Biome>(w.biome[c]);
            if (pasture_quality(biome) >= kNomadPasture) return true;
            const std::uint32_t known = s.domesticates.empty() ? 0 : s.domesticates[c];
            return (known & camels) != 0 && (biome == Biome::Desert || biome == Biome::DryShrubland);
        };
        // The shadow empire (Barfield): steppe confederations arise beside
        // agrarian states, living off their tribute and trade, and do not
        // form until there is a state within kShadowKm.
        std::vector<char> state_polity(s.polities.size(), 0);
        for (std::size_t id = 1; id < s.polities.size(); ++id) {
            state_polity[id] = s.polities[id].ended == kNever && !s.polities[id].nomadic &&
                               polity_has_writing(w, s, static_cast<std::uint32_t>(id));
        }
        std::vector<CellId> near_state;
        auto beside_a_state = [&](CellId c) {
            cells_within(w, c, kShadowKm, near_state);
            for (CellId nb : near_state) {
                // Polities founded this year (past the end) are chiefdoms, not states.
                const std::uint32_t q = s.polity_of[nb];
                if (q < state_polity.size() && state_polity[q] != 0) return true;
            }
            return false;
        };
        auto herders_at = [&](CellId c) {
            return !s.env.pastoral.empty() && s.env.pastoral[c] >= kNomadPastoral && !s.env.farming.empty() &&
                   s.env.farming[c] >= kMinFarming;
        };
        // Herder chiefdoms that have taken to riding become confederations.
        for (std::size_t id = 1; id < s.polities.size(); ++id) {
            Polity& pol = s.polities[id];
            if (pol.ended != kNever || pol.nomadic || polity_has_writing(w, s, static_cast<std::uint32_t>(id))) continue;
            if (herders_at(pol.capital) && cell_trait(s, pol.capital, i_warfare) >= kNomadWarfare &&
                mounted_country(pol.capital)) {
                RngStream rng = ctx.rng(Stream::Polity, kRidersEntity | static_cast<std::uint32_t>(id));
                if (rng.uniform() < (beside_a_state(pol.capital) ? kRiderUnite : kRiderUnite * kNoShadow)) pol.nomadic = true;
            }
        }
        std::vector<CellId> in_reach;
        for (CellId c = 0; c < n; ++c) {
            if (s.polity_of[c] != 0 || s.cell_population[c] == 0) continue;
            const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            const double density = static_cast<double>(s.cell_population[c]) / w.grid.area_km2(c);
            const double hierarchy = cell_trait(s, c, i_hierarchy);
            RngStream rng = ctx.rng(Stream::Polity, kFormationEntity | c);
            if (herders_at(c)) {
                // Mounted herders band together in confederations, the
                // richer their farming neighbours the likelier (the shadow
                // empire).
                const double warfare = cell_trait(s, c, i_warfare);
                if (warfare >= kNomadWarfare && mounted_country(c)) {
                    const bool shadow = beside_a_state(c);
                    double wealth = 0.0;
                    cells_within(w, c, WarSystem::kRaidKm, in_reach);
                    for (CellId nb : in_reach) wealth += raid_wealth(s, nb);
                    const double lure = std::min(kShadowMax, 1.0 + wealth / kShadowWealth);
                    const double roaming = mobility_factor(cell_trait(s, c, trait_index(content, "mobility")), 0.5);
                    if (rng.uniform() < w.per_area(kNomadFormation * warfare * lure * roaming * (shadow ? 1.0 : kNoShadow))) {
                        s.polity_of[c] = found(c, 0, true);
                        continue;
                    }
                    if (shadow) continue;
                }
                // Herders without mounts, or with no state to raid and trade
                // with, form tribal chiefdoms, from the
                // thinner numbers herds can feed.
                const double enough = kHerderDensity * kMinDensity;
                if (hierarchy < kMinHierarchy || density < enough) continue;
                if (rng.uniform() < w.per_area(kFormation * hierarchy * 0.5 * std::min(2.0, density / enough))) {
                    s.polity_of[c] = found(c, 0, false);
                }
                continue;
            }
            if (farming < kMinFarming || density < kMinDensity || hierarchy < kMinHierarchy) continue;
            // Circumscription: neighbours that are sea or much poorer land.
            const double own = s.env.farm_capacity[c] / w.grid.area_km2(c);
            int hemmed = 0, around = 0;
            for (CellId nb : w.grid.neighbors(c)) {
                ++around;
                if (!w.is_land[nb] || s.env.farm_capacity[nb] / w.grid.area_km2(nb) < 0.25 * own) ++hemmed;
            }
            const double circumscription = around > 0 ? static_cast<double>(hemmed) / around : 0.0;
            const double p =
                w.per_area(kFormation * hierarchy * (0.5 + circumscription) * std::min(2.0, density / kMinDensity));
            if (rng.uniform() < p) s.polity_of[c] = found(c, 0, false);
        }
    }
}

// ------------------------------------------------------------------ rulers

void lose_authority(SimState& s, std::uint32_t id, double by) {
    if (id == 0 || id >= s.polities.size()) return;
    Polity& p = s.polities[id];
    p.authority = std::clamp(p.authority - by, 0.0, 1.0);
}

const char* law_name(Laws::Law law) {
    switch (law) {
        case Laws::Tenure: return "governors";
        case Laws::Taxes: return "taxes";
        case Laws::Army: return "army";
        case Laws::Succession: return "succession";
    }
    return "";
}

const char* law_level_name(Laws::Law law, std::uint8_t level) {
    static constexpr const char* names[4][3] = {
        {"appointed for a term", "for life", "hereditary"},
        {"collected by the crown", "collected and sent on by governors", "kept by governors"},
        {"a royal army", "governors' levies", "governors' private armies"},
        {"an heir", "elected by the council", "shared among the sons"},
    };
    return law < 4 && level < 3 ? names[law][level] : "";
}

const char* institution_name(Institution i) {
    switch (i) {
        case Institution::Temple: return "temple priesthood";
        case Institution::Council: return "council of notables";
        case Institution::LawCode: return "law code";
        case Institution::Army: return "standing army";
        case Institution::Count: break;
    }
    return "";
}

std::string ruler_name(const Ruler& r) {
    static const char* const kNumerals[] = {"", "", " II", " III", " IV", " V", " VI", " VII", " VIII", " IX", " X"};
    return r.name + (r.number < 11 ? kNumerals[r.number] : " " + std::to_string(r.number));
}

int ruler_age(const Ruler& r, Tick now) { return static_cast<int>((now - r.born) / kTicksPerYear); }

void RulerSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    if (s.polities.size() < 2) return;
    if (s.rulers.empty()) s.rulers.emplace_back();
    if (s.dynasties.empty()) s.dynasties.emplace_back();
    const Tick tick = ctx.tick;
    const std::size_t i_warfare = trait_index(content, "warfare");

    auto language_of = [&](const Polity& p) -> const Language& {
        return s.languages[p.language < s.languages.size() ? p.language : 0];
    };
    // Each house's rulers, oldest first (ruler 0 is no one).
    std::vector<std::vector<std::uint32_t>> by_house(s.dynasties.size());
    for (std::size_t i = 1; i < s.rulers.size(); ++i) {
        const std::uint32_t d = s.rulers[i].dynasty;
        if (d < by_house.size()) by_house[d].push_back(static_cast<std::uint32_t>(i));
    }
    // A new ruler of a house, adult (founders and usurpers) or the given age.
    auto crown = [&](std::uint32_t polity, std::uint32_t dynasty, int age, RngStream& rng) {
        const Polity& p = s.polities[polity];
        Ruler r;
        r.id = static_cast<std::uint32_t>(s.rulers.size());
        r.polity = polity;
        r.dynasty = dynasty;
        // Heirs often bear the names of their forebears: the house's last
        // six rulers, newest first.
        std::vector<std::uint32_t> house;
        if (dynasty < by_house.size()) {
            const auto& all = by_house[dynasty];
            for (std::size_t i = all.size(); i-- > 0 && house.size() < 6;) house.push_back(all[i]);
        }
        if (!house.empty() && rng.uniform() < 0.4) {
            r.name = s.rulers[house[static_cast<std::size_t>(rng.uniform() * static_cast<double>(house.size())) % house.size()]].name;
        } else {
            r.name = coin_name(content, language_of(p), rng, [](const std::string&) { return false; });
        }
        for (std::uint32_t h : house) {
            if (s.rulers[h].name == r.name) r.number = std::max(r.number, s.rulers[h].number + 1);
        }
        if (r.number == 1) {
            for (std::uint32_t h : house) {
                if (s.rulers[h].name == r.name) r.number = 2;
            }
        }
        r.born = tick - static_cast<Tick>(age) * kTicksPerYear;
        r.reign_start = tick;
        r.ability = rng.uniform();
        // Warlike peoples breed warlike rulers, mostly.
        const double warfare = cell_trait(s, p.capital, i_warfare);
        r.martial = std::clamp(0.5 * warfare + 0.5 * rng.uniform(), 0.0, 1.0);
        s.rulers.push_back(std::move(r));
        if (dynasty < by_house.size()) by_house[dynasty].push_back(s.rulers.back().id);
        return s.rulers.back().id;
    };
    auto found_house = [&](std::uint32_t polity, std::uint32_t founder) {
        Dynasty d;
        d.id = static_cast<std::uint32_t>(s.dynasties.size());
        d.name = "House of " + s.rulers[founder].name;
        d.polity = polity;
        d.founder = founder;
        d.founded = tick;
        // The founder leaves the list of his old (or no) house for his own.
        const std::uint32_t was = s.rulers[founder].dynasty;
        if (was < by_house.size()) std::erase(by_house[was], founder);
        s.rulers[founder].dynasty = d.id;
        s.dynasties.push_back(std::move(d));
        by_house.emplace_back(1, founder);
    };
    // ---- Figures: the people behind the reigns. They draw from their own
    // stream (one per reign), so recording them changes nothing above.
    auto new_person = [&](RngStream& fr, const Polity& p, Tick born, bool female, std::uint32_t dynasty,
                          std::uint16_t roles) {
        Figure f;
        f.name = figure_name(content, language_of(p), fr);
        f.female = female;
        f.born = born;
        f.dynasty = dynasty;
        f.polity = p.id;
        f.culture = p.culture;
        f.language = p.language;
        f.home = p.capital;
        f.roles = roles;
        f.ability = fr.uniform();
        f.martial = fr.uniform();
        f.piety = fr.uniform();
        f.ambition = fr.uniform();
        return f;
    };
    // A spouse for figure `who`, alive at `alive_at`.
    auto marry = [&](std::uint32_t who, Tick alive_at, RngStream& fr) {
        const Figure me = s.figures[who];
        Figure f = new_person(fr, s.polities[me.polity], me.born + static_cast<Tick>((fr.uniform() * 12.0 - 6.0) * kTicksPerYear),
                              !me.female, 0, kRoleConsort);
        f.died = draw_death(f.born, std::max(alive_at, f.born), fr);
        f.spouse = who;
        f.goal = figure_goal(f);
        const std::uint32_t id = add_figure(s, std::move(f));
        s.figures[who].spouse = id;
        return id;
    };
    // The person who reigns as ruler `rid`; parents if known.
    auto person_of_reign = [&](std::uint32_t rid, std::uint16_t roles, std::uint32_t father, std::uint32_t mother) {
        RngStream fr = ctx.rng(Stream::Figures, rid);
        const Ruler& r = s.rulers[rid];
        Figure f = new_person(fr, s.polities[r.polity], r.born, fr.uniform() < 0.1, r.dynasty, roles | kRoleRuler);
        f.name = r.name;
        f.father = father;
        f.mother = mother;
        f.ruler = rid;
        f.ability = r.ability;
        f.martial = r.martial;
        f.goal = figure_goal(f);
        const std::uint32_t id = add_figure(s, std::move(f));
        s.rulers[rid].figure = id;
        // A grown ruler of a state is married. (Chiefs' families are left
        // out: thousands of chiefdoms rise and fall, and their kin would
        // swamp the record.)
        if (ruler_age(r, tick) >= 18 && polity_has_writing(w, s, r.polity) && fr.uniform() < 0.9) marry(id, tick, fr);
        return id;
    };
    // The rest of the family of a ruler who has died: a spouse, if none yet,
    // and the children who outlived infancy besides the heir (born at
    // heir_born; kNever if no child succeeds). Returns them, eldest first.
    auto family_of = [&](std::uint32_t fid, Tick heir_born, RngStream& fr) {
        std::vector<std::uint32_t> kids;
        if (fid == 0) return kids;
        const Tick adult = s.figures[fid].born + 18 * kTicksPerYear;
        if (s.figures[fid].spouse == 0) marry(fid, heir_born != kNever ? std::max(heir_born, adult) : adult, fr);
        const Figure me = s.figures[fid];
        const int others = static_cast<int>(fr.uniform() * 4.0);
        for (int k = 0; k < others; ++k) {
            const Tick born = me.born + static_cast<Tick>((18.0 + fr.uniform() * 22.0) * kTicksPerYear);
            if (born >= tick) continue;  // the parent died first
            if (heir_born != kNever && born > heir_born - kTicksPerYear && born < heir_born + kTicksPerYear) continue;
            const Tick died = draw_death(born, born, fr);
            if (died < born + 5 * kTicksPerYear) continue;  // died an infant: not recorded
            Figure c = new_person(fr, s.polities[me.polity], born, fr.uniform() < 0.5, me.dynasty, kRoleRoyalKin);
            c.died = died;
            c.father = me.female ? me.spouse : fid;
            c.mother = me.female ? fid : me.spouse;
            c.goal = figure_goal(c);
            kids.push_back(add_figure(s, std::move(c)));
        }
        std::sort(kids.begin(), kids.end(), [&](std::uint32_t a, std::uint32_t b) {
            return s.figures[a].born < s.figures[b].born || (s.figures[a].born == s.figures[b].born && a < b);
        });
        return kids;
    };
    // The heir's parents: the late ruler and spouse; for a ruler who died
    // a child, the late ruler's own parents (a younger sibling) if they could
    // have had a child then, else none recorded (a kinsman).
    auto parents_of_heir = [&](std::uint32_t fid, Tick heir_born) -> std::pair<std::uint32_t, std::uint32_t> {
        if (fid == 0) return {0u, 0u};
        const Figure& me = s.figures[fid];
        auto could = [&](std::uint32_t p, bool mother) {
            if (p == 0) return false;
            const Figure& q = s.figures[p];
            return q.born + 16 * kTicksPerYear <= heir_born && (!mother || q.born + 48 * kTicksPerYear >= heir_born) &&
                   (q.died == kNever || q.died > heir_born - (mother ? 0 : kTicksPerYear));
        };
        if (me.born + 16 * kTicksPerYear <= heir_born) {
            // The ruler's child; by the spouse if the spouse was old enough.
            const std::uint32_t other = could(me.spouse, !me.female) ? me.spouse : 0;
            return me.female ? std::pair{other, fid} : std::pair{fid, other};
        }
        if (could(me.father, false) && could(me.mother, true)) return {me.father, me.mother};
        return {0u, 0u};
    };
    // Only a ruler who reached adulthood had a family of his own.
    auto grown = [&](std::uint32_t fid) {
        return fid != 0 && s.figures[fid].born + kAdultAge * kTicksPerYear <= tick;
    };
    // A reign has ended without the ruler dying: the person lives on.
    auto outlive_reign = [&](const Ruler& r) {
        if (r.figure == 0 || r.figure >= s.figures.size()) return;
        RngStream fr = ctx.rng(Stream::Figures, r.id);
        Figure& f = s.figures[r.figure];
        if (f.died == kNever) f.died = draw_death(f.born, tick, fr);
    };
    auto end_house_if_gone = [&](std::uint32_t dynasty) {
        if (dynasty == 0 || dynasty >= s.dynasties.size() || s.dynasties[dynasty].ended != kNever) return;
        for (const Polity& q : s.polities) {
            if (q.ended == kNever && q.ruler != 0 && q.ruler < s.rulers.size() && s.rulers[q.ruler].dynasty == dynasty) return;
        }
        s.dynasties[dynasty].ended = tick;
    };

    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        Polity& pol = s.polities[id];
        RngStream rng = ctx.rng(Stream::Rulers, id);
        if (pol.ended != kNever) {
            // The realm has fallen: so has its ruler.
            if (pol.ruler != 0 && pol.ruler < s.rulers.size() && s.rulers[pol.ruler].end == ReignEnd::Reigning) {
                Ruler& r = s.rulers[pol.ruler];
                r.end = ReignEnd::RealmFell;
                r.reign_end = tick;
                end_house_if_gone(r.dynasty);
                outlive_reign(r);
            }
            continue;
        }
        if (pol.ruler == 0) {
            // A new realm: its first ruler founds a house.
            const std::uint32_t r = crown(id, 0, 25 + static_cast<int>(rng.uniform() * 20.0), rng);
            // A governor who broke away founds the house himself.
            const std::uint32_t gov = pol.founder < s.figures.size() ? pol.founder : 0;
            if (gov != 0 && figure_alive(s.figures[gov], tick)) {
                Ruler& ru = s.rulers[r];
                Figure& f = s.figures[gov];
                ru.name = f.name;
                ru.number = 1;
                ru.born = f.born;
                ru.ability = f.ability;
                ru.martial = f.martial;
                found_house(id, r);
                f.ruler = r;
                f.roles |= kRoleRuler | kRoleFounder;
                f.dynasty = s.rulers[r].dynasty;
                f.polity = id;
                f.died = kNever;  // RulerSystem decides when a ruler dies
                f.goal = figure_goal(f);
                s.rulers[r].figure = gov;
                pol.ruler = r;
                continue;
            }
            found_house(id, r);
            pol.ruler = r;
            person_of_reign(r, kRoleFounder, 0, 0);
            continue;
        }
        // A rebel leader whose people overthrew the dynasty takes the throne.
        if (pol.rebel != 0) {
            const std::uint32_t leader = pol.rebel < s.figures.size() ? pol.rebel : 0;
            pol.rebel = 0;
            if (leader != 0 && figure_alive(s.figures[leader], tick)) {
                Ruler& fallen = s.rulers[pol.ruler];
                fallen.end = ReignEnd::Usurped;
                fallen.reign_end = tick;
                const std::uint32_t fallen_house = fallen.dynasty;
                outlive_reign(fallen);
                const std::uint32_t r = crown(id, 0, 30, rng);
                Ruler& ru = s.rulers[r];
                Figure& f = s.figures[leader];
                ru.name = f.name;
                ru.number = 1;
                ru.born = f.born;
                ru.ability = f.ability;
                ru.martial = f.martial;
                found_house(id, r);
                f.ruler = r;
                f.roles |= kRoleRuler | kRoleFounder | kRoleUsurper;
                f.dynasty = s.rulers[r].dynasty;
                f.died = kNever;
                f.goal = figure_goal(f);
                s.rulers[r].figure = leader;
                pol.ruler = r;
                end_house_if_gone(fallen_house);
                // A general's coup: he leaves the army for the throne.
                if (pol.commander != 0 && pol.commander < s.commanders.size() && s.commanders[pol.commander].figure == leader) {
                    s.commanders[pol.commander].seized = true;
                    s.commanders[pol.commander].to = tick;
                    pol.commander = 0;
                }
                s.events.push_back({tick, EventType::Usurpation, pol.capital, static_cast<std::int64_t>(r)});
                continue;
            }
        }
        Ruler& old = s.rulers[pol.ruler];
        const int age = ruler_age(old, tick);
        const int bin = std::clamp(age / 5, 0, 20);
        if (rng.uniform() >= vital::kForagerDeathProb[bin] * kRulerMortality) continue;
        // The ruler dies; an heir of the house succeeds, a child if the
        // ruler died young.
        old.end = ReignEnd::Died;
        old.reign_end = tick;
        // crown() appends to s.rulers, so `old` must not be used after it.
        const std::uint32_t old_house = old.dynasty;
        const std::uint32_t old_figure = old.figure < s.figures.size() ? old.figure : 0;
        const std::uint32_t old_id = old.id;
        if (old_figure != 0) s.figures[old_figure].died = tick;
        RngStream fr = ctx.rng(Stream::Figures, old_id | 0x80000000u);
        int heir_age = std::max(0, age - 20 - static_cast<int>(rng.uniform() * 15.0));
        // An elected ruler is a grown man of the house.
        if (pol.laws[Laws::Succession] == 1) heir_age = std::max(heir_age, kAdultAge + 4);
        const bool child = heir_age < kAdultAge;
        // Priests lend the heir legitimacy; a council sees the throne passed
        // in good order.
        const double settled = (has_institution(pol.institutions, Institution::Temple) ? Institutions::kTempleDispute : 1.0) *
                               (has_institution(pol.institutions, Institution::Council) ? Institutions::kCouncilDispute : 1.0);
        const double disputed = std::min(1.0, kDispute * (1.5 - pol.stability) * (child ? 2.0 : 1.0) * settled *
                                                  (pol.laws[Laws::Succession] == 1 ? Laws::kElectiveDispute : 1.0));
        // A partible inheritance: the realm may be shared out among the sons.
        if (pol.laws[Laws::Succession] == 2 && rng.uniform() < Laws::kPartition) pol.divide = true;
        if (rng.uniform() < disputed) {
            // Disputed. The claimant: the heir's most ambitious grown sibling,
            // else a kinsman. The more ambitious the claimant, the likelier
            // the realm breaks apart over the claim; else another house wins.
            const Tick heir_born = tick - static_cast<Tick>(heir_age) * kTicksPerYear;
            const auto kids = grown(old_figure) ? family_of(old_figure, heir_born, fr) : std::vector<std::uint32_t>{};
            std::uint32_t claimant = 0;
            int rivals = 0;
            for (std::uint32_t k : kids) {
                const Figure& c = s.figures[k];
                if (!figure_alive(c, tick) || figure_age(c, tick) < kAdultAge) continue;
                ++rivals;
                if (claimant == 0 || c.ambition > s.figures[claimant].ambition) claimant = k;
            }
            Figure kinsman = new_person(fr, pol, tick - static_cast<Tick>((25.0 + fr.uniform() * 25.0) * kTicksPerYear),
                                        false, old_house, 0);
            kinsman.died = draw_death(kinsman.born, tick, fr);
            // How the claimant ranks among all who might have claimed: the
            // most ambitious of k is ambition^k (uniform), so breakups are no
            // more common overall than before, only likelier with the bold.
            double ambition = kinsman.ambition;
            if (claimant != 0) {
                ambition = 1.0;
                for (int k = 0; k < rivals; ++k) ambition *= s.figures[claimant].ambition;
            }
            const double breakup = polity_has_writing(w, s, id) ? kBreakupState : kBreakupChiefdom;
            if (rng.uniform() < breakup * (0.5 + ambition)) {
                pol.contested = true;
                const std::uint32_t heir = crown(id, old_house, heir_age, rng);
                pol.ruler = heir;
                const auto [father, mother] = parents_of_heir(old_figure, s.rulers[heir].born);
                person_of_reign(heir, kRoleRoyalKin, father, mother);
                if (claimant == 0) claimant = add_figure(s, std::move(kinsman));
                s.figures[claimant].roles |= kRoleClaimant;
                s.figures[claimant].goal = figure_goal(s.figures[claimant]);
                s.events.push_back({tick, EventType::SuccessionDisputed, pol.capital, static_cast<std::int64_t>(claimant)});
                continue;
            }
            const std::uint32_t usurper = crown(id, 0, 25 + static_cast<int>(rng.uniform() * 25.0), rng);
            // A famous general is the man who takes it.
            const std::uint32_t mc = pol.commander < s.commanders.size() ? pol.commander : 0;
            const std::uint32_t general = mc != 0 && s.commanders[mc].fame >= PolitySystem::kUsurperFame &&
                                                  figure_alive(s.figures[s.commanders[mc].figure], tick)
                                              ? s.commanders[mc].figure
                                              : 0;
            if (general != 0) {
                Ruler& ru = s.rulers[usurper];
                Figure& f = s.figures[general];
                ru.name = f.name;
                ru.number = 1;
                ru.born = f.born;
                ru.ability = f.ability;
                ru.martial = f.martial;
            }
            found_house(id, usurper);
            pol.ruler = usurper;
            end_house_if_gone(old_house);
            if (general != 0) {
                Figure& f = s.figures[general];
                f.ruler = usurper;
                f.roles |= kRoleRuler | kRoleFounder | kRoleUsurper;
                f.dynasty = s.rulers[usurper].dynasty;
                f.died = kNever;
                f.goal = figure_goal(f);
                s.rulers[usurper].figure = general;
                s.commanders[mc].seized = true;
                s.commanders[mc].to = tick;
                pol.commander = 0;
            } else {
                person_of_reign(usurper, kRoleUsurper | kRoleFounder, 0, 0);
            }
            s.events.push_back({tick, EventType::Usurpation, pol.capital, static_cast<std::int64_t>(usurper)});
            continue;
        }
        const std::uint32_t heir = crown(id, old_house, heir_age, rng);
        pol.ruler = heir;
        if (grown(old_figure) && polity_has_writing(w, s, id)) family_of(old_figure, s.rulers[heir].born, fr);
        const auto [father, mother] = parents_of_heir(old_figure, s.rulers[heir].born);
        person_of_reign(heir, kRoleRoyalKin, father, mother);
        s.events.push_back({tick, EventType::Succession, pol.capital, static_cast<std::int64_t>(heir)});
    }
}

}  // namespace histsim
