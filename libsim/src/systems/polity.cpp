#include "histsim/systems/polity.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "histsim/figures.hpp"
#include "histsim/language.hpp"
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
    for (CellId c = 0; c < n; ++c) {
        w.for_hinterland(c, [&](CellId h) {
            const double pop = static_cast<double>(s.cell_population[h]);
            people[c] += pop;
            if (towns) town[c] += s.urban[h] * pop;
        });
    }
}

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
    }
    // A city is named for the town of a hinterland (the cell itself at level
    // 5), at its centre: the cell with the most townspeople around it.
    std::vector<double> around_people, around_town;
    hinterland_people(w, s, around_people, around_town);
    for (CellId c = 0; c < n; ++c) {
        const double town = around_town[c];
        std::uint32_t& city = s.city_of[c];
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

    // ---- Expansion over stateless land: one attempt a year per polity (per
    // level-5 cell's worth of land on finer grids), at the neighbouring cells
    // it would hold best. (Land changes hands between polities only in war.)
    const std::size_t attempts = std::max<std::size_t>(1, w.cells_for(1));
    const double cell_share = std::min(1.0, w.cell_scale() * w.cell_scale());  // of a level-5 cell
    const double coarse = std::max(1.0, w.cell_scale() * w.cell_scale());       // level-5 cells in one
    std::vector<std::pair<double, CellId>> frontier;
    for (std::size_t id = 1; id < np; ++id) {
        if (s.polities[id].ended != kNever) continue;
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
        for (std::size_t k = 0; k < attempts && k < frontier.size(); ++k) {
            const auto [best, target] = frontier[k];
            if (best < kMinControl) break;
            // Strength against the people of a level-5 cell's worth of land.
            const double people = static_cast<double>(s.cell_population[target]) / cell_share;
            const double p = kAnnex * best * (0.5 + warfare) * std::min(1.0, strength[id] / (4.0 * people)) / coarse;
            if (rng.uniform() < p) {
                s.polity_of[target] = static_cast<std::uint32_t>(id);
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
        // Governors, loyalty and revolts.
        for (std::size_t id = 1; id < np; ++id) {
            if (!provincial[id]) continue;
            for (std::uint32_t v : seats_of[id]) {
                Province& pv = s.provinces[v];
                if (pv.ended != kNever) continue;
                if (pv.governor == 0 || !figure_alive(s.figures[pv.governor], ctx.tick)) {
                    RngStream fr = ctx.rng(Stream::Figures, kGovernorEntity | v);
                    pv.governor = record_person(s, content, pv.seat, ctx.tick, 30, 55, kRoleGovernor, fr);
                    s.figures[pv.governor].polity = static_cast<std::uint32_t>(id);
                }
                const Polity& pol = s.polities[id];
                const Figure& gov = s.figures[pv.governor];
                const Cohort* lead = lead_cohort(s, pv.seat);
                const bool same_people = lead != nullptr && s.cultures[lead->culture].root == s.cultures[pol.culture].root;
                const bool same_faith = !s.faith_of.empty() && s.faith_of[pv.seat] == pol.religion;
                const double target = kLoyaltyBase + kLoyaltyStability * pol.stability + kLoyaltyControl * control_of(id, pv.seat) +
                                      (same_people ? kLoyaltyPeople : 0.0) + (same_faith ? kLoyaltyFaith : 0.0) +
                                      kLoyaltyAmbition * (0.5 - gov.ambition);
                pv.loyalty = std::clamp(pv.loyalty < 0.0 ? target : pv.loyalty + kLoyaltyRate * (target - pv.loyalty), 0.0, 1.0);
            }
        }
        for (std::size_t id = 1; id < np; ++id) {
            if (!provincial[id]) continue;
            for (std::uint32_t v : seats_of[id]) {
                Province& pv = s.provinces[v];
                if (pv.ended != kNever || pv.loyalty >= kRevoltBelow || s.polities[id].ended != kNever) continue;
                RngStream rng = ctx.rng(Stream::Polity, kRevoltEntity | v);
                const double ambition = s.figures[pv.governor].ambition;
                if (rng.uniform() >= kRevolt * (kRevoltBelow - pv.loyalty) / kRevoltBelow * (0.5 + ambition)) continue;
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
                const double target = std::clamp(
                    kUnrestHunger * unfed / people + kUnrestCrowding * crowding +
                        (at_war[id] ? kUnrestAtWar : 0.0) + kUnrestWar * std::min(1.0, pol.shock) +
                        kUnrestPlague * pol.pressure[static_cast<std::size_t>(Pressure::Epidemic)] +
                        (pol.bond == Bond::Tributary ? kUnrestTribute : 0.0) + (foreign ? kUnrestForeign : 0.0) +
                        (other_faith ? kUnrestFaith : 0.0) + kUnrestInept * (0.5 - ability),
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
                if (pv == nullptr || pv->loyalty >= kStayLoyal) {
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
        for (CellId c : mine) (state && (control_of(id, c) >= kCoreControl || c == capital) ? core : rest).push_back(c);
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
            const double score = (town + kSeatRural * people) * hierarchy * (literate ? kSeatWriting : 1.0) *
                                 (scribes ? kSeatBureaucracy : 1.0);
            // A seat in a fallen state held a province under it: it keeps at
            // least a province's reach (its governor's, or a local dynasty's).
            const double own = polity_reach_km(w, s, c);
            candidates.push_back({c, score, (state ? std::max(own, kProvinceReachKm) : own) * (nomadic ? kNomadReach : 1.0)});
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
        if (state) {
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
        if (me.born + 16 * kTicksPerYear <= heir_born) return me.female ? std::pair{me.spouse, fid} : std::pair{fid, me.spouse};
        auto could = [&](std::uint32_t p, bool mother) {
            if (p == 0) return false;
            const Figure& q = s.figures[p];
            return q.born + 16 * kTicksPerYear <= heir_born && (!mother || q.born + 48 * kTicksPerYear >= heir_born) &&
                   (q.died == kNever || q.died > heir_born - (mother ? 0 : kTicksPerYear));
        };
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
        const int heir_age = std::max(0, age - 20 - static_cast<int>(rng.uniform() * 15.0));
        const bool child = heir_age < kAdultAge;
        // Priests lend the heir legitimacy; a council sees the throne passed
        // in good order.
        const double settled = (has_institution(pol.institutions, Institution::Temple) ? Institutions::kTempleDispute : 1.0) *
                               (has_institution(pol.institutions, Institution::Council) ? Institutions::kCouncilDispute : 1.0);
        const double disputed = std::min(1.0, kDispute * (1.5 - pol.stability) * (child ? 2.0 : 1.0) * settled);
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
            found_house(id, usurper);
            pol.ruler = usurper;
            end_house_if_gone(old_house);
            person_of_reign(usurper, kRoleUsurper | kRoleFounder, 0, 0);
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
