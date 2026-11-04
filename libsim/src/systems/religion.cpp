#include "histsim/systems/religion.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "histsim/figures.hpp"
#include "histsim/language.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/systems/technology.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kFoundEntity = 0x10000000u;
constexpr std::uint32_t kNameEntity = 0x20000000u;
constexpr std::uint32_t kSchismEntity = 0x30000000u;
constexpr std::uint32_t kProphetFigureEntity = 0x20000000u;  // Stream::Figures
constexpr std::uint32_t kFollowEntity = 0x40000000u;

std::size_t trait_index(const Content& c, const char* id) {
    for (std::size_t t = 0; t < c.traits.size(); ++t) {
        if (c.traits[t].id == id) return t;
    }
    return c.traits.size();
}

// The largest cohort of a cell.
const Cohort* lead_cohort(const SimState& s, CellId c) {
    const Cohort* best = nullptr;
    for (const Cohort& co : s.cohorts_in(c)) {
        if (best == nullptr || co.total() > best->total()) best = &co;
    }
    return best;
}

double cell_trait(const SimState& s, CellId c, std::size_t t) {
    double sum = 0.0, total = 0.0;
    for (const Cohort& co : s.cohorts_in(c)) {
        sum += static_cast<double>(co.total()) * (t < kMaxTraits ? co.traits[t] : 0.5);
        total += static_cast<double>(co.total());
    }
    return total > 0.0 ? sum / total : 0.5;
}

// A name word in the language of a cell's main people, not used before.
std::string coin_religion_word(const Content& content, const SimState& s, CellId c, RngStream& rng) {
    const Cohort* lead = lead_cohort(s, c);
    if (lead == nullptr) return "Nameless";
    const Language& lang = s.languages[lead->language];
    const std::uint64_t inv = inventory(lang);
    std::string word;
    for (int attempt = 0; attempt < 12; ++attempt) {
        word = romanize(content, coin_word(content, inv, lang.syllables, 2, 3, rng), true);
        const bool taken = std::any_of(s.religions.begin(), s.religions.end(),
                                       [&](const Religion& r) { return r.name.find(word) != std::string::npos; });
        if (!taken) break;
    }
    return word;
}

}  // namespace

std::uint32_t religion_family(const SimState& s, std::uint32_t id) {
    for (std::size_t guard = 0; guard < s.religions.size() && id != 0 && id < s.religions.size(); ++guard) {
        if (s.religions[id].parent == 0) return id;
        id = s.religions[id].parent;
    }
    return id;
}

const std::string& religion_kind_name(const World& w, const SimState& s, std::uint32_t id) {
    static const std::string none;
    if (id == 0 || id >= s.religions.size() || s.religions[id].kind >= w.content->religion_kinds.size()) return none;
    return w.content->religion_kinds[s.religions[id].kind].name;
}

bool religion_organized(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.religions.size()) return false;
    const auto& kinds = w.content->religion_kinds;
    return s.religions[id].kind < kinds.size() && !kinds[s.religions[id].kind].ethnic;
}

void ReligionSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    const auto& kinds = content.religion_kinds;
    if (kinds.empty() || content.folk_religion >= kinds.size()) return;
    if (s.religions.empty()) s.religions.emplace_back();
    if (s.faith_of.size() != n) s.faith_of.assign(n, 0);
    const Tick tick = ctx.tick;
    const std::int64_t year = tick / kTicksPerYear;
    const std::size_t i_ritual = trait_index(content, "ritual");

    auto make = [&](std::uint8_t kind, std::uint32_t parent, std::uint32_t root, std::string name, CellId origin,
                    std::uint32_t polity) {
        Religion r;
        r.id = static_cast<std::uint32_t>(s.religions.size());
        r.parent = parent;
        r.kind = kind;
        r.culture_root = root;
        r.name = std::move(name);
        r.founded = tick;
        r.origin = origin;
        r.polity = polity;
        s.religions.push_back(std::move(r));
        return s.religions.back().id;
    };

    // ---- Each people's folk religion, made the first time it is needed.
    std::vector<std::uint32_t> folk_of(s.cultures.size(), 0);
    for (const Religion& r : s.religions) {
        if (r.id != 0 && r.kind == content.folk_religion && r.culture_root < folk_of.size()) folk_of[r.culture_root] = r.id;
    }
    std::vector<std::uint32_t> root_of(n, 0);
    for (CellId c = 0; c < n; ++c) {
        const Cohort* lead = lead_cohort(s, c);
        if (lead == nullptr) {
            s.faith_of[c] = 0;
            continue;
        }
        const std::uint32_t root = s.cultures[lead->culture].root;
        root_of[c] = root;
        if (folk_of[root] == 0) {
            folk_of[root] = make(static_cast<std::uint8_t>(content.folk_religion), 0, root,
                                 s.cultures[root].name + " beliefs", c, 0);
        }
        if (s.faith_of[c] == 0) s.faith_of[c] = folk_of[root];
    }

    // ---- Folk beliefs follow their people: where newcomers' culture has
    // taken over, the old folk faith gives way to theirs.
    for (CellId c = 0; c < n; ++c) {
        const std::uint32_t f = s.faith_of[c];
        if (f == 0 || s.religions[f].kind != content.folk_religion || s.religions[f].culture_root == root_of[c]) continue;
        RngStream rng = ctx.rng(Stream::Religion, kFollowEntity | c);
        if (rng.uniform() < kFolkFollow) s.faith_of[c] = folk_of[root_of[c]];
    }

    // ---- New organized religions, in towns counted over their hinterland.
    std::vector<double> around_people, around_town;
    if (year % kFoundingYears == 0) hinterland_people(w, s, around_people, around_town);
    if (year % kFoundingYears == 0) {
        for (std::size_t k = 0; k < kinds.size(); ++k) {
            const ReligionKind& kind = kinds[k];
            if (kind.found_chance <= 0.0) continue;
            auto try_found = [&](CellId c, std::uint32_t polity) {
                const double people = static_cast<double>(s.cell_population[c]);
                if (people <= 0.0) return;
                const std::uint32_t f = s.faith_of[c];
                if (f == 0 || s.religions[f].kind == k) return;  // already of this kind
                // Cults of the city's gods grow out of its people's own beliefs.
                if (kind.found_polity && !kinds[s.religions[f].kind].ethnic) return;
                if (around_town[c] < kind.found_min_town) return;
                if (kind.found_min_ritual > 0.0 && cell_trait(s, c, i_ritual) < kind.found_min_ritual) return;
                if (kind.found_technique >= 0 && !((techniques_in(s, c) >> kind.found_technique) & 1u)) return;
                // A universal faith is not founded where one already holds.
                if (!kind.ethnic && kind.missionary > 0.0 && kinds[s.religions[f].kind].missionary >= kind.missionary) {
                    return;
                }
                RngStream rng = ctx.rng(Stream::Religion, kFoundEntity | (static_cast<std::uint32_t>(k) << 24) | c);
                // Per capital for temple cults; per area for the others.
                const double chance = kind.found_polity ? kind.found_chance : w.per_area(kind.found_chance);
                if (rng.uniform() >= chance) return;
                RngStream names = ctx.rng(Stream::Religion, kNameEntity | c);
                const std::string word = coin_religion_word(content, s, c, names);
                const std::string name = kind.missionary > 0.0 ? "Teaching of " + word : "Cult of " + word;
                const std::uint32_t id = make(static_cast<std::uint8_t>(k), f, 0, name, c, polity);
                s.faith_of[c] = id;
                // A missionary faith has a teacher: the "Teaching of" names him.
                if (kind.missionary > 0.0) {
                    RngStream fr = ctx.rng(Stream::Figures, kProphetFigureEntity | id);
                    s.religions[id].founder = record_person(s, content, c, tick, 25, 50, kRoleProphet, fr, word);
                }
                s.events.push_back({tick, EventType::ReligionFounded, c, static_cast<std::int64_t>(id)});
            };
            if (kind.found_polity) {
                for (std::uint32_t p = 1; p < s.polities.size(); ++p) {
                    if (s.polities[p].ended == kNever) try_found(s.polities[p].capital, p);
                }
            } else {
                for (CellId c = 0; c < n; ++c) {
                    try_found(c, s.polity_of.size() == n ? s.polity_of[c] : 0);
                }
            }
        }
    }

    // ---- Rulers hold the faith of their capital.
    std::vector<double> reach(s.polities.size(), 0.0);
    for (std::uint32_t p = 1; p < s.polities.size(); ++p) {
        Polity& pol = s.polities[p];
        if (pol.ended != kNever) continue;
        reach[p] = polity_reach(w, s, p);
        const std::uint32_t faith = s.faith_of[pol.capital];
        if (faith != 0 && faith != pol.religion) {
            if (pol.religion != 0 && religion_organized(w, s, faith) && s.religions[faith].founded != tick) {
                s.events.push_back({tick, EventType::StateConversion, pol.capital, static_cast<std::int64_t>(p)});
            }
            pol.religion = faith;
        }
    }

    // ---- Conversion, every land from last year's faiths at once.
    std::vector<std::uint32_t> next = s.faith_of;
    std::vector<std::pair<std::uint32_t, double>> pulls;
    auto add = [&](std::uint32_t g, double pull) {
        for (auto& e : pulls) {
            if (e.first == g) {
                e.second += pull;
                return;
            }
        }
        pulls.emplace_back(g, pull);
    };
    for (CellId c = 0; c < n; ++c) {
        const std::uint32_t f = s.faith_of[c];
        if (f == 0 || s.cell_population[c] == 0) continue;
        const double people = static_cast<double>(s.cell_population[c]);
        // Preachers and traders win over farmers and townspeople; foragers
        // mostly keep their own beliefs.
        const double tilled = (s.env.farming.size() == n ? s.env.farming[c] : 0.0) + (s.urban.size() == n ? s.urban[c] : 0.0);
        const double open = kForagerOpenness + (1.0 - kForagerOpenness) * std::min(1.0, tilled);
        pulls.clear();
        for (CellId nb : w.grid.neighbors(c)) {
            const std::uint32_t g = s.faith_of[nb];
            if (g == 0 || g == f || kinds[s.religions[g].kind].ethnic) continue;
            const ReligionKind& kind = kinds[s.religions[g].kind];
            add(g, open * static_cast<double>(s.cell_population[nb]) * kind.appeal * (kNeighbourInfluence + kind.missionary));
        }
        if (!w.trade_edge_begin.empty() && s.route_value.size() == w.trade_edges.size()) {
            for (std::uint32_t e : w.trade_links(c)) {
                const std::uint32_t g = s.faith_of[w.trade_other(e, c)];
                if (g == 0 || g == f || s.route_value[e] <= 0.0) continue;
                const ReligionKind& kind = kinds[s.religions[g].kind];
                if (kind.ethnic || kind.missionary <= 0.0) continue;
                add(g, open * s.route_value[e] * kTradeInfluence * kind.appeal * kind.missionary);
            }
        }
        const std::uint32_t ruler = s.polity_of.size() == n ? s.polity_of[c] : 0;
        if (ruler != 0 && s.polities[ruler].ended == kNever && reach[ruler] > 0.0) {
            const std::uint32_t g = s.polities[ruler].religion;
            if (g != 0 && g != f && !kinds[s.religions[g].kind].ethnic) {
                const double control = polity_control(w.grid.distance_km(s.polities[ruler].capital, c), reach[ruler]);
                // A pious ruler presses the faith harder.
                add(g, kRulerPull * control * people * kinds[s.religions[g].kind].appeal *
                           trait_factor(reigning_person(s, ruler), &Figure::piety));
            }
        }
        if (pulls.empty()) continue;
        std::pair<std::uint32_t, double> best = pulls.front();
        for (const auto& e : pulls) {
            if (e.second > best.second || (e.second == best.second && e.first < best.first)) best = e;
        }
        const double own = people * kinds[s.religions[f].kind].appeal * kAttachment;
        if (best.second <= own) continue;
        RngStream rng = ctx.rng(Stream::Religion, c);
        // Preaching passes from land to land at the same pace in km on any
        // grid; the rulers' pull does not depend on the grid.
        const bool rulers = ruler != 0 && best.first == s.polities[ruler].religion;
        const double rate = rulers ? kConversion : w.per_step(kConversion);
        if (rng.uniform() < rate * (best.second - own) / (best.second + own)) next[c] = best.first;
    }
    s.faith_of = std::move(next);

    // ---- Schisms: a faith ruled far from where it began gets its own sect.
    if (year % kSchismYears == 0) {
        for (std::uint32_t p = 1; p < s.polities.size(); ++p) {
            Polity& pol = s.polities[p];
            if (pol.ended != kNever || !religion_organized(w, s, pol.religion)) continue;
            const std::uint32_t r = pol.religion;
            if (w.grid.distance_km(pol.capital, s.religions[r].origin) <= kSchismKm) continue;
            std::size_t held = 0;
            for (CellId c = 0; c < n; ++c) held += s.polity_of.size() == n && s.polity_of[c] == p && s.faith_of[c] == r ? 1 : 0;
            if (held < w.cells_for(kSchismCells)) continue;
            RngStream rng = ctx.rng(Stream::Religion, kSchismEntity | p);
            if (rng.uniform() >= kSchism) continue;
            const std::uint32_t sect =
                make(s.religions[r].kind, r, 0, s.religions[r].name + " (" + pol.name + " rite)", pol.capital, p);
            for (CellId c = 0; c < n; ++c) {
                if (s.polity_of[c] == p && s.faith_of[c] == r) s.faith_of[c] = sect;
            }
            s.polities[p].religion = sect;
            s.events.push_back({tick, EventType::Schism, s.polities[p].capital, static_cast<std::int64_t>(sect)});
        }
    }

    // ---- Followers drift toward their faith's doctrine.
    for (Cohort& co : s.cohorts) {
        const std::uint32_t f = s.faith_of[co.cell];
        if (f == 0) continue;
        const ReligionKind& kind = kinds[s.religions[f].kind];
        if (kind.pulls == 0) continue;
        for (std::size_t t = 0; t < kMaxTraits; ++t) {
            if ((kind.pulls >> t) & 1u) co.traits[t] += kDoctrinePull * (kind.doctrine[t] - co.traits[t]);
        }
    }

    // ---- Faiths with no land left are gone.
    std::vector<std::uint8_t> held(s.religions.size(), 0);
    for (std::uint32_t f : s.faith_of) held[f] = 1;
    for (Religion& r : s.religions) {
        if (r.id != 0 && r.extinct == kNever && !held[r.id] && r.founded < tick) r.extinct = tick;
        if (r.id != 0 && r.extinct != kNever && held[r.id]) r.extinct = kNever;  // came back (folk beliefs)
    }
}

}  // namespace histsim
