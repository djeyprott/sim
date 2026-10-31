#include "histsim/systems/polity.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "histsim/language.hpp"
#include "histsim/systems/technology.hpp"

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
    if (!polity_has_writing(w, s, id)) return "chiefdom";
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
            const double fed = s.env.farm_capacity.empty()
                                   ? 0.0
                                   : s.env.farm_capacity[c] / w.grid.area_km2(c) * s.env.farming[c];
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
    auto found = [&](CellId capital, std::uint32_t parent) {
        const Cohort* lead = lead_cohort(s, capital);
        RngStream rng = ctx.rng(Stream::Polity, kNameEntity | capital);
        Polity p;
        p.id = static_cast<std::uint32_t>(s.polities.size());
        p.capital = capital;
        p.founded = ctx.tick;
        p.culture = lead->culture;
        p.language = lead->language;
        p.parent = parent;
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

    // ---- Reach and strength.
    std::vector<double> reach(np, 0.0), strength(np, 0.0);
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended != kNever) continue;
        reach[id] = polity_reach_km(w, s, s.polities[id].capital);
        for (CellId c : cells[id]) {
            strength[id] += static_cast<double>(s.cell_population[c]) *
                            polity_control(w.grid.distance_km(s.polities[id].capital, c), reach[id]);
        }
    }
    auto control_of = [&](std::size_t id, CellId c) {
        return polity_control(w.grid.distance_km(s.polities[id].capital, c), reach[id]);
    };

    // ---- Expansion and conquest: one attempt a year per polity, at the
    // neighbouring cell it would hold best.
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended != kNever) continue;
        CellId target = kInvalidCell;
        double best = -1.0;
        for (CellId c : cells[id]) {
            for (CellId nb : w.grid.neighbors(c)) {
                if (s.polity_of[nb] == id || s.cell_population[nb] == 0) continue;
                const double ctl = control_of(id, nb);
                if (ctl > best || (ctl == best && nb < target)) best = ctl, target = nb;
            }
        }
        if (target == kInvalidCell || best < kMinControl) continue;
        RngStream rng = ctx.rng(Stream::Polity, static_cast<std::uint32_t>(id));
        const double warfare = cell_trait(s, s.polities[id].capital, i_warfare);
        const double people = static_cast<double>(s.cell_population[target]);
        const std::uint32_t owner = s.polity_of[target];
        double p = 0.0;
        if (owner == 0) {
            p = kAnnex * best * (0.5 + warfare) * std::min(1.0, strength[id] / (4.0 * people));
        } else {
            const double ours = strength[id] * best;
            const double theirs = strength[owner] * control_of(owner, target) + people;
            const double ratio = ours / theirs;
            if (ratio > 1.5) p = std::min(0.2, kConquest * (ratio - 1.5)) * (0.5 + warfare);
        }
        if (p > 0.0 && rng.uniform() < p) {
            s.polity_of[target] = static_cast<std::uint32_t>(id);
            // Rebuild later from polity_of; capitals are rechecked next year.
        }
    }

    // ---- Collapse: the whole polity dissolves into stateless villages.
    for (std::size_t id = 1; id < np; ++id) {
        Polity& pol = s.polities[id];
        if (pol.ended != kNever) continue;
        const double k = s.env.capacity[pol.capital];
        const double crowding = k > 0.0 ? static_cast<double>(s.cell_population[pol.capital]) / k : 2.0;
        const double strain = 1.0 + 3.0 * std::max(0.0, crowding - 1.0);
        const double base = polity_has_writing(w, s, static_cast<std::uint32_t>(id)) ? kStateCollapse : kChiefdomCollapse;
        RngStream rng = ctx.rng(Stream::Polity, kCollapseEntity | static_cast<std::uint32_t>(id));
        if (rng.uniform() < base * strain) {
            for (CellId c : cells[id]) {
                if (s.polity_of[c] == id) s.polity_of[c] = 0;
            }
            pol.ended = ctx.tick;
            s.events.push_back({ctx.tick, EventType::PolityEnded, pol.capital, static_cast<std::int64_t>(id)});
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
        double mean_control = 0.0;
        for (CellId c : mine) {
            const double ctl = control_of(id, c);
            mean_control += ctl;
            if (c != pol.capital && ctl < kSecedeBelow && rng.uniform32() < kSecession) s.polity_of[c] = 0;
        }
        if (mine.size() >= kCivilWarCells) {
            mean_control /= static_cast<double>(mine.size());
            if (rng.uniform() < kCivilWar * (1.0 - mean_control)) {
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
                const std::uint32_t rebel = found(seat, static_cast<std::uint32_t>(id));
                for (std::size_t i = 0; i < half; ++i) s.polity_of[by_distance[i].second] = rebel;
            }
        }
    }

    // ---- New polities, where farming, density, hierarchy and
    // circumscription come together.
    const std::int64_t year = ctx.tick / kTicksPerYear;
    if (year % kFormationYears == 0) {
        for (CellId c = 0; c < n; ++c) {
            if (s.polity_of[c] != 0 || s.cell_population[c] == 0) continue;
            const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            const double density = static_cast<double>(s.cell_population[c]) / w.grid.area_km2(c);
            const double hierarchy = cell_trait(s, c, i_hierarchy);
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
            if (rng.uniform() < p) s.polity_of[c] = found(c, 0);
        }
    }
}

}  // namespace histsim
