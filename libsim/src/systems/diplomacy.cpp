#include "histsim/systems/diplomacy.hpp"

#include <algorithm>
#include <utility>

#include "histsim/systems/polity.hpp"

namespace histsim {

namespace {

std::pair<std::uint32_t, std::uint32_t> ordered(std::uint32_t a, std::uint32_t b) { return a < b ? std::pair{a, b} : std::pair{b, a}; }

bool alive(const SimState& s, std::uint32_t id) { return id != 0 && id < s.polities.size() && s.polities[id].ended == kNever; }

template <typename V>
auto lower(V& v, std::uint32_t a, std::uint32_t b) {
    return std::lower_bound(v.begin(), v.end(), std::pair{a, b},
                            [](const Relation& r, const std::pair<std::uint32_t, std::uint32_t>& k) {
                                return r.a != k.first ? r.a < k.first : r.b < k.second;
                            });
}

// How much a realm's capital lives by trade (0-1).
double trading(const SimState& s, std::uint32_t id) {
    const CellId c = s.polities[id].capital;
    if (c >= s.trade_value.size() || s.cell_population[c] <= 0) return 0.0;
    return std::min(1.0, s.trade_value[c] / (static_cast<double>(s.cell_population[c]) * PolitySystem::kTradePerPerson));
}

// How zealous a realm's high priest is (0 if none or not very pious).
double zeal(const SimState& s, std::uint32_t id) {
    const std::uint32_t pr = s.polities[id].priest;
    if (pr == 0 || pr >= s.figures.size()) return 0.0;
    return std::max(0.0, 2.0 * (s.figures[pr].piety - 0.5));
}

}  // namespace

const Relation* find_relation(const SimState& s, std::uint32_t a, std::uint32_t b) {
    const auto [x, y] = ordered(a, b);
    const auto it = lower(s.relations, x, y);
    return it != s.relations.end() && it->a == x && it->b == y ? &*it : nullptr;
}

Relation& relation_of(SimState& s, std::uint32_t a, std::uint32_t b, Tick now) {
    const auto [x, y] = ordered(a, b);
    auto it = lower(s.relations, x, y);
    if (it != s.relations.end() && it->a == x && it->b == y) return *it;
    Relation r;
    r.a = x;
    r.b = y;
    r.since = now;
    return *s.relations.insert(it, r);
}

double opinion_between(const SimState& s, std::uint32_t a, std::uint32_t b) {
    const Relation* r = find_relation(s, a, b);
    return r != nullptr ? r->opinion : 0.0;
}

double trust_between(const SimState& s, std::uint32_t a, std::uint32_t b) {
    const Relation* r = find_relation(s, a, b);
    return r != nullptr ? r->trust : 0.5;
}

std::uint32_t claims_on(const SimState& s, std::uint32_t by, std::uint32_t of) {
    const Relation* r = find_relation(s, by, of);
    if (r == nullptr) return 0;
    return by == r->a ? r->claims_ab : r->claims_ba;
}

bool tribute_withheld(const SimState& s, std::uint32_t a, std::uint32_t b) {
    const Relation* r = find_relation(s, a, b);
    return r != nullptr && r->withheld_by == b && r->withheld != kNever &&
           static_cast<double>(s.tick - r->withheld) < Diplomacy::kGrievanceYears * kTicksPerYear;
}

bool cause_for_war(const SimState& s, std::uint32_t a, std::uint32_t b) {
    if (claims_on(s, a, b) > 0 || tribute_withheld(s, a, b)) return true;
    // Holy war: a zealous high priest against another faith.
    const std::uint32_t pr = s.polities[a].priest;
    return s.polities[a].religion != s.polities[b].religion && pr != 0 && pr < s.figures.size() &&
           s.figures[pr].piety >= Diplomacy::kHolyPiety;
}

double opinion_target(const World&, const SimState& s, const Relation& r, Tick now) {
    const Polity& p = s.polities[r.a];
    const Polity& q = s.polities[r.b];
    double t = 0.0;
    const std::uint32_t cp = p.culture < s.cultures.size() ? s.cultures[p.culture].root : 0;
    const std::uint32_t cq = q.culture < s.cultures.size() ? s.cultures[q.culture].root : 0;
    if (cp != 0 && cp == cq) t += Diplomacy::kCultureOpinion;
    if (p.religion == q.religion) {
        t += Diplomacy::kFaithOpinion;
    } else {
        t -= Diplomacy::kZealOpinion * std::max(zeal(s, r.a), zeal(s, r.b));
    }
    t += Diplomacy::kTradeOpinion * std::min(trading(s, r.a), trading(s, r.b));
    if (r.last_war != kNever) {
        const double years = static_cast<double>(now - r.last_war) / kTicksPerYear;
        t -= Diplomacy::kWarOpinion * Diplomacy::kMemoryYears / (Diplomacy::kMemoryYears + std::max(0.0, years));
    }
    t -= std::min(Diplomacy::kMaxClaimOpinion, Diplomacy::kClaimOpinion * (r.claims_ab + r.claims_ba));
    for (const Alliance& al : s.alliances) {
        if (al.ended == kNever && al.a == r.a && al.b == r.b) t += Diplomacy::kAllyOpinion;
    }
    if ((p.overlord == r.b && p.bond != Bond::None) || (q.overlord == r.a && q.bond != Bond::None)) t -= Diplomacy::kBondOpinion;
    return std::clamp(t, -1.0, 1.0);
}

void update_relations(const World& w, SimState& s, Tick now, const std::vector<std::vector<std::uint32_t>>& borders) {
    // Claims lapse, die with the claimant, or end when the land is won back.
    s.claims.erase(std::remove_if(s.claims.begin(), s.claims.end(),
                                  [&](const Claim& c) {
                                      return c.until <= now || !alive(s, c.by) || c.cell >= s.polity_of.size() ||
                                             s.polity_of[c.cell] == c.by;
                                  }),
                   s.claims.end());
    // The pairs in touch: neighbours, the bound, the allied, and old enemies
    // within living memory.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (std::uint32_t id = 1; id < borders.size() && id < s.polities.size(); ++id) {
        if (!alive(s, id)) continue;
        for (std::uint32_t b : borders[id]) {
            if (b > id) pairs.emplace_back(id, b);
        }
        const Polity& p = s.polities[id];
        if (p.bond != Bond::None && alive(s, p.overlord)) pairs.push_back(ordered(id, p.overlord));
    }
    for (const Alliance& al : s.alliances) {
        if (al.ended == kNever && alive(s, al.a) && alive(s, al.b)) pairs.emplace_back(al.a, al.b);
    }
    for (const Relation& r : s.relations) {
        if (alive(s, r.a) && alive(s, r.b) && r.last_war != kNever &&
            static_cast<double>(now - r.last_war) / kTicksPerYear < Diplomacy::kForgetYears) {
            pairs.emplace_back(r.a, r.b);
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    std::vector<Relation> next;
    next.reserve(pairs.size());
    for (const auto& [a, b] : pairs) {
        const auto it = lower(s.relations, a, b);
        if (it != s.relations.end() && it->a == a && it->b == b) {
            next.push_back(*it);
        } else {
            Relation r;
            r.a = a;
            r.b = b;
            r.since = now;
            next.push_back(r);
        }
        next.back().claims_ab = next.back().claims_ba = 0;
    }
    s.relations.swap(next);
    // This year's claims between each pair.
    for (const Claim& c : s.claims) {
        const std::uint32_t owner = s.polity_of[c.cell];
        if (owner == 0 || owner == c.by || !alive(s, owner)) continue;
        const auto [x, y] = ordered(c.by, owner);
        const auto it = lower(s.relations, x, y);
        if (it == s.relations.end() || it->a != x || it->b != y) continue;
        Relation& r = s.relations[static_cast<std::size_t>(it - s.relations.begin())];
        (c.by == x ? r.claims_ab : r.claims_ba) += 1;
    }
    // Opinions drift toward their targets; a new acquaintance starts there.
    for (Relation& r : s.relations) {
        const double target = opinion_target(w, s, r, now);
        r.opinion = r.since == now && r.wars == 0 ? target : r.opinion + Diplomacy::kDrift * (target - r.opinion);
        r.trust += std::clamp(0.5 - r.trust, -Diplomacy::kTrustDrift, Diplomacy::kTrustDrift);
    }
}

void note_war_begun(SimState& s, std::uint32_t a, std::uint32_t b, Tick now) {
    Relation& r = relation_of(s, a, b, now);
    r.opinion = std::max(-1.0, r.opinion - Diplomacy::kWarShock);
    ++r.wars;
}

void note_war_ended(SimState& s, std::uint32_t a, std::uint32_t b, Tick now) { relation_of(s, a, b, now).last_war = now; }

bool return_war_gains(SimState& s, Tick since, const std::vector<std::uint32_t>& to,
                      const std::vector<std::uint32_t>& from) {
    auto in = [](const std::vector<std::uint32_t>& v, std::uint32_t p) { return std::find(v.begin(), v.end(), p) != v.end(); };
    bool any = false;
    for (const Claim& c : s.claims) {
        if (c.until - Diplomacy::kClaimYears * kTicksPerYear < since || !alive(s, c.by) || !in(to, c.by)) continue;
        if (c.cell >= s.polity_of.size() || !in(from, s.polity_of[c.cell])) continue;
        s.polity_of[c.cell] = c.by;
        if (c.cell < s.strife.size()) s.strife[c.cell] = std::max<std::uint8_t>(s.strife[c.cell], 1);
        any = true;
    }
    return any;
}

void note_land_lost(SimState& s, CellId cell, std::uint32_t loser, Tick now) {
    if (!alive(s, loser)) return;
    for (Claim& c : s.claims) {
        if (c.cell == cell && c.by == loser) {
            c.until = now + Diplomacy::kClaimYears * kTicksPerYear;
            return;
        }
    }
    s.claims.push_back({cell, loser, now + Diplomacy::kClaimYears * kTicksPerYear});
}

}  // namespace histsim
