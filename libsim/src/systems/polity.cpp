#include "histsim/systems/polity.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "histsim/language.hpp"
#include "histsim/systems/farming.hpp"
#include "histsim/systems/technology.hpp"
#include "histsim/systems/war.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kCivilWarEntity = 0x40000000u;
constexpr std::uint32_t kFormationEntity = 0x20000000u;
constexpr std::uint32_t kNameEntity = 0x60000000u;
constexpr std::uint32_t kCollapseEntity = 0x10000000u;

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

double polity_reach_km(const World& w, const SimState& s, CellId capital) {
    const TechEffects e = technique_effects(w, capital, techniques_in(s, capital));
    return PolitySystem::kBaseReachKm * (1.0 + e.administration) * (1.0 + 0.3 * e.contact);
}

double polity_reach(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.polities.size()) return 0.0;
    const Polity& p = s.polities[id];
    return polity_reach_km(w, s, p.capital) * (p.nomadic ? PolitySystem::kNomadReach : 1.0);
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
        const double town = s.urban[c] * static_cast<double>(s.cell_population[c]);
        std::uint32_t& city = s.city_of[c];
        if (city == 0 && town >= kCityPeople) {
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
        if (s.polities[id].ended != kNever) continue;
        reach[id] = polity_reach(w, s, static_cast<std::uint32_t>(id));
        for (CellId c : cells[id]) {
            strength[id] += static_cast<double>(s.cell_population[c]) *
                            polity_control(w.grid.distance_km(s.polities[id].capital, c), reach[id]);
        }
    }
    auto control_of = [&](std::size_t id, CellId c) {
        return polity_control(w.grid.distance_km(s.polities[id].capital, c), reach[id]);
    };

    // ---- Expansion over stateless land: one attempt a year per polity, at
    // the neighbouring cell it would hold best. (Land changes hands between
    // polities only in war.)
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended != kNever) continue;
        CellId target = kInvalidCell;
        double best = -1.0;
        for (CellId c : cells[id]) {
            for (CellId nb : w.grid.neighbors(c)) {
                if (s.polity_of[nb] != 0 || s.cell_population[nb] == 0) continue;
                const double ctl = control_of(id, nb);
                if (ctl > best || (ctl == best && nb < target)) best = ctl, target = nb;
            }
        }
        if (target == kInvalidCell || best < kMinControl) continue;
        RngStream rng = ctx.rng(Stream::Polity, static_cast<std::uint32_t>(id));
        const double warfare = cell_trait(s, s.polities[id].capital, i_warfare);
        const double people = static_cast<double>(s.cell_population[target]);
        const double p = kAnnex * best * (0.5 + warfare) * std::min(1.0, strength[id] / (4.0 * people));
        if (rng.uniform() < p) {
            s.polity_of[target] = static_cast<std::uint32_t>(id);
            // Rebuild later from polity_of; capitals are rechecked next year.
        }
    }

    // ---- Stability: resilience against this year's pressures.
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
        for (CellId c : cells[id]) {
            const double pop = static_cast<double>(s.cell_population[c]);
            people += pop;
            unfed += std::max(0.0, pop - s.env.capacity[c]);
            held += pop * control_of(id, c);
            if (!s.trade_value.empty()) trade += s.trade_value[c];
            if (!s.disease_deaths.empty()) sick += s.disease_deaths[c];
            for (const Cohort& co : s.cohorts_in(c)) {
                if (s.cultures[co.culture].root != root) others += static_cast<double>(co.total());
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
            pr[static_cast<std::size_t>(Pressure::Division)] = others / people;
        }
        pr[static_cast<std::size_t>(Pressure::War)] = std::min(1.0, pol.shock);
        const std::uint64_t known = techniques_in(s, pol.capital);
        double target = kStabilityBase + kHierarchyWeight * cell_trait(s, pol.capital, i_hierarchy) +
                        kRitualWeight * cell_trait(s, pol.capital, i_ritual) +
                        (writing < 64 && ((known >> writing) & 1u) ? kWritingWeight : 0.0) +
                        (bureaucracy < 64 && ((known >> bureaucracy) & 1u) ? kBureaucracyWeight : 0.0);
        for (std::size_t k = 0; k < kPressureCount; ++k) target -= kPressureWeight[k] * pr[k];
        target = std::clamp(target, 0.0, 1.0);
        pol.stability += (target < pol.stability ? kStabilityFall : kStabilityRise) * (target - pol.stability);
    }

    // ---- Break-up of fragile polities, and succession disputes.
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended != kNever) continue;
        const bool state = polity_has_writing(w, s, static_cast<std::uint32_t>(id));
        const double stability = s.polities[id].stability;
        const double fragile = stability < kFragile ? (kFragile - stability) / kFragile : 0.0;
        const double crisis = kCollapse * fragile * fragile;
        const double succession = state ? kStateSuccession : kChiefdomSuccession;
        RngStream rng = ctx.rng(Stream::Polity, kCollapseEntity | static_cast<std::uint32_t>(id));
        const double roll = rng.uniform();
        if (roll >= crisis + succession) continue;
        // The cause: the heaviest pressure, unless the polity was sound.
        std::uint8_t cause = kSuccessionCrisis;
        if (roll < crisis) {
            double worst = 0.0;
            for (std::size_t k = 0; k < kPressureCount; ++k) {
                const double weight = kPressureWeight[k] * s.polities[id].pressure[k];
                if (weight > worst) worst = weight, cause = static_cast<std::uint8_t>(k);
            }
        }
        const CellId capital = s.polities[id].capital;
        const bool nomadic = s.polities[id].nomadic;
        std::vector<CellId> mine;
        for (CellId c : cells[id]) {
            if (s.polity_of[c] == id) mine.push_back(c);
        }
        Polity& pol = s.polities[id];
        pol.ended = ctx.tick;
        pol.fate = PolityFate::Collapsed;
        pol.fate_cause = cause;
        s.events.push_back({ctx.tick, EventType::PolityEnded, capital, static_cast<std::int64_t>(id)});
        for (CellId c : mine) s.polity_of[c] = 0;
        if (!state) continue;  // a chiefdom dissolves into its villages
        // A state leaves a core under its old capital and successors where
        // towns can hold a following.
        std::vector<CellId> core, rest;
        for (CellId c : mine) (control_of(id, c) >= kCoreControl || c == capital ? core : rest).push_back(c);
        std::vector<CellId> seats;
        std::stable_sort(rest.begin(), rest.end(),
                         [&](CellId a, CellId b) { return s.cell_population[a] > s.cell_population[b]; });
        for (CellId c : rest) {
            const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            const double density = static_cast<double>(s.cell_population[c]) / w.grid.area_km2(c);
            if (farming < kMinFarming || density < kMinDensity || cell_trait(s, c, i_hierarchy) < kMinHierarchy) continue;
            bool near = w.grid.distance_km(c, capital) < kSuccessorKm;
            for (CellId seat : seats) near = near || w.grid.distance_km(c, seat) < kSuccessorKm;
            if (!near) seats.push_back(c);
        }
        const std::uint32_t heir = found(capital, static_cast<std::uint32_t>(id), nomadic);
        s.polities[heir].stability = kSuccessorStability;
        for (CellId c : core) s.polity_of[c] = heir;
        std::vector<std::uint32_t> successor(seats.size());
        for (std::size_t i = 0; i < seats.size(); ++i) {
            successor[i] = found(seats[i], static_cast<std::uint32_t>(id), nomadic);
            s.polities[successor[i]].stability = kSuccessorStability;
        }
        for (CellId c : rest) {
            double best = kSuccessorKm;
            std::uint32_t to = 0;
            for (std::size_t i = 0; i < seats.size(); ++i) {
                const double d = w.grid.distance_km(c, seats[i]);
                if (d < best || (d == best && successor[i] < to)) best = d, to = successor[i];
            }
            if (to != 0) s.polity_of[c] = to;
        }
    }

    // ---- Secession of provinces held too loosely, and civil war.
    for (std::size_t id = 1; id < np; ++id) {
        Polity& pol = s.polities[id];
        if (pol.ended != kNever) continue;
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
        if (mine.size() >= kCivilWarCells) {
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

    // ---- New polities, where farming, density, hierarchy and
    // circumscription come together.
    const std::int64_t year = ctx.tick / kTicksPerYear;
    if (year % kFormationYears == 0) {
        std::size_t horse_riding = content.techniques.size();
        for (std::size_t k = 0; k < content.techniques.size(); ++k) {
            if (content.techniques[k].id == "horse_riding") horse_riding = k;
        }
        std::vector<CellId> in_reach;
        for (CellId c = 0; c < n; ++c) {
            if (s.polity_of[c] != 0 || s.cell_population[c] == 0) continue;
            const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            const double density = static_cast<double>(s.cell_population[c]) / w.grid.area_km2(c);
            const double hierarchy = cell_trait(s, c, i_hierarchy);
            // Mounted herders band together in confederations.
            const double pastoral = s.env.pastoral.empty() ? 0.0 : s.env.pastoral[c];
            if (pastoral >= kNomadPastoral && farming >= kMinFarming) {
                // Mounted herders on open pasture, the richer their farming
                // neighbours the likelier (the shadow empire).
                const double warfare = cell_trait(s, c, i_warfare);
                if (pasture_quality(static_cast<Biome>(w.biome[c])) >= kNomadPasture && warfare >= kNomadWarfare &&
                    horse_riding < content.techniques.size() && ((techniques_in(s, c) >> horse_riding) & 1u)) {
                    double wealth = 0.0;
                    cells_within(w, c, WarSystem::kRaidKm, in_reach);
                    for (CellId nb : in_reach) wealth += raid_wealth(s, nb);
                    const double lure = std::min(kShadowMax, 1.0 + wealth / kShadowWealth);
                    RngStream rng = ctx.rng(Stream::Polity, kFormationEntity | c);
                    if (rng.uniform() < kNomadFormation * warfare * lure) s.polity_of[c] = found(c, 0, true);
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
            const double p = kFormation * hierarchy * (0.5 + circumscription) * std::min(2.0, density / kMinDensity);
            RngStream rng = ctx.rng(Stream::Polity, kFormationEntity | c);
            if (rng.uniform() < p) s.polity_of[c] = found(c, 0, false);
        }
    }
}

}  // namespace histsim
