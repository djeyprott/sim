#include "histsim/systems/polity.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

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
                if (qualifies && rng.uniform() < Institutions::kFound) {
                    pol.institutions |= bit;
                    s.events.push_back({ctx.tick, EventType::InstitutionFounded, pol.capital, tag});
                }
            }
        }
    }

    // ---- Break-up of fragile polities, and succession disputes.
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
                pol.nomadic = true;
            }
        }
        std::vector<CellId> in_reach;
        for (CellId c = 0; c < n; ++c) {
            if (s.polity_of[c] != 0 || s.cell_population[c] == 0) continue;
            const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
            const double density = static_cast<double>(s.cell_population[c]) / w.grid.area_km2(c);
            const double hierarchy = cell_trait(s, c, i_hierarchy);
            if (herders_at(c)) {
                // Mounted herders band together in confederations, the
                // richer their farming neighbours the likelier (the shadow
                // empire).
                const double warfare = cell_trait(s, c, i_warfare);
                if (warfare >= kNomadWarfare && mounted_country(c)) {
                    double wealth = 0.0;
                    cells_within(w, c, WarSystem::kRaidKm, in_reach);
                    for (CellId nb : in_reach) wealth += raid_wealth(s, nb);
                    const double lure = std::min(kShadowMax, 1.0 + wealth / kShadowWealth);
                    RngStream rng = ctx.rng(Stream::Polity, kFormationEntity | c);
                    const double roaming = mobility_factor(cell_trait(s, c, trait_index(content, "mobility")), 0.5);
                    if (rng.uniform() < w.per_area(kNomadFormation * warfare * lure * roaming)) {
                        s.polity_of[c] = found(c, 0, true);
                    }
                    continue;
                }
                // Herders without mounts form tribal chiefdoms, from the
                // thinner numbers herds can feed.
                const double enough = kHerderDensity * kMinDensity;
                if (hierarchy < kMinHierarchy || density < enough) continue;
                RngStream rng = ctx.rng(Stream::Polity, kFormationEntity | c);
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
            RngStream rng = ctx.rng(Stream::Polity, kFormationEntity | c);
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
    // A new ruler of a house, adult (founders and usurpers) or the given age.
    auto crown = [&](std::uint32_t polity, std::uint32_t dynasty, int age, RngStream& rng) {
        const Polity& p = s.polities[polity];
        Ruler r;
        r.id = static_cast<std::uint32_t>(s.rulers.size());
        r.polity = polity;
        r.dynasty = dynasty;
        // Heirs often bear the names of their forebears.
        std::vector<std::uint32_t> house;
        for (std::size_t i = s.rulers.size(); i-- > 1 && house.size() < 6;) {
            if (s.rulers[i].dynasty == dynasty) house.push_back(static_cast<std::uint32_t>(i));
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
        return s.rulers.back().id;
    };
    auto found_house = [&](std::uint32_t polity, std::uint32_t founder) {
        Dynasty d;
        d.id = static_cast<std::uint32_t>(s.dynasties.size());
        d.name = "House of " + s.rulers[founder].name;
        d.polity = polity;
        d.founder = founder;
        d.founded = tick;
        s.rulers[founder].dynasty = d.id;
        s.dynasties.push_back(std::move(d));
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
            }
            continue;
        }
        if (pol.ruler == 0) {
            // A new realm: its first ruler founds a house.
            const std::uint32_t r = crown(id, 0, 25 + static_cast<int>(rng.uniform() * 20.0), rng);
            found_house(id, r);
            pol.ruler = r;
            continue;
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
        const int heir_age = std::max(0, age - 20 - static_cast<int>(rng.uniform() * 15.0));
        const bool child = heir_age < kAdultAge;
        // Priests lend the heir legitimacy; a council sees the throne passed
        // in good order.
        const double settled = (has_institution(pol.institutions, Institution::Temple) ? Institutions::kTempleDispute : 1.0) *
                               (has_institution(pol.institutions, Institution::Council) ? Institutions::kCouncilDispute : 1.0);
        const double disputed = std::min(1.0, kDispute * (1.5 - pol.stability) * (child ? 2.0 : 1.0) * settled);
        if (rng.uniform() < disputed) {
            // Disputed: the realm may break apart; else another house wins.
            const double breakup = polity_has_writing(w, s, id) ? kBreakupState : kBreakupChiefdom;
            if (rng.uniform() < breakup) {
                pol.contested = true;
                const std::uint32_t heir = crown(id, old_house, heir_age, rng);
                pol.ruler = heir;
                continue;
            }
            const std::uint32_t usurper = crown(id, 0, 25 + static_cast<int>(rng.uniform() * 25.0), rng);
            found_house(id, usurper);
            pol.ruler = usurper;
            end_house_if_gone(old_house);
            s.events.push_back({tick, EventType::Usurpation, pol.capital, static_cast<std::int64_t>(usurper)});
            continue;
        }
        const std::uint32_t heir = crown(id, old_house, heir_age, rng);
        pol.ruler = heir;
        s.events.push_back({tick, EventType::Succession, pol.capital, static_cast<std::int64_t>(heir)});
    }
}

}  // namespace histsim
