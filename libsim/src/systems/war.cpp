#include "histsim/systems/war.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "histsim/figures.hpp"
#include "histsim/systems/bondage.hpp"
#include "histsim/systems/polity.hpp"
#include "histsim/systems/technology.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kWarEntity = 0x10000000u;
constexpr std::uint32_t kDeclareEntity = 0x20000000u;
constexpr std::uint32_t kDiplomacyEntity = 0x30000000u;
constexpr std::uint32_t kRaidEntity = 0x40000000u;
constexpr std::uint32_t kAllianceEntity = 0x50000000u;
constexpr std::uint32_t kArmyEntity = 0x60000000u;

std::size_t trait_index(const Content& c, const char* id) {
    for (std::size_t t = 0; t < c.traits.size(); ++t) {
        if (c.traits[t].id == id) return t;
    }
    return c.traits.size();
}

double cell_trait(const SimState& s, CellId c, std::size_t t) {
    double sum = 0.0, total = 0.0;
    for (const Cohort& co : s.cohorts_in(c)) {
        sum += static_cast<double>(co.total()) * co.traits[t];
        total += static_cast<double>(co.total());
    }
    return total > 0.0 && t < kMaxTraits ? sum / total : 0.5;
}

bool alive(const SimState& s, std::uint32_t id) {
    return id != 0 && id < s.polities.size() && s.polities[id].ended == kNever;
}

std::uint32_t round_people(double x, RngStream& rng) {
    const double f = std::floor(x);
    return static_cast<std::uint32_t>(f) + (rng.uniform() < x - f ? 1u : 0u);
}

// Kill `share` of the people in a cell (only men 15-49 if `soldiers`).
std::int64_t kill(SimState& s, CellId c, double share, bool soldiers, RngStream& rng) {
    share = std::clamp(share, 0.0, 1.0);
    if (share <= 0.0) return 0;
    std::int64_t dead = 0;
    for (auto i = s.cohort_begin[c]; i < s.cohort_begin[c + 1]; ++i) {
        Cohort& co = s.cohorts[i];
        for (int b = 0; b < kAgeBins; ++b) {
            const bool fighting_age = b >= 3 && b < 10;  // 15-49
            if (soldiers && !fighting_age) continue;
            const std::uint32_t m = std::min(co.male[b], round_people(share * co.male[b], rng));
            co.male[b] -= m;
            dead += m;
            if (!soldiers) {
                const std::uint32_t f = std::min(co.female[b], round_people(share * co.female[b], rng));
                co.female[b] -= f;
                dead += f;
            }
        }
    }
    s.ledger.deaths += dead;
    if (s.war_deaths.size() > c) s.war_deaths[c] += static_cast<std::uint32_t>(dead);
    s.war_deaths_total += dead;
    return dead;
}

// Men of fighting age in a cell.
std::int64_t fighting_men(const SimState& s, CellId c) {
    std::int64_t m = 0;
    for (const Cohort& co : s.cohorts_in(c)) {
        for (int b = 3; b < 10; ++b) m += co.male[b];
    }
    return m;
}

// Move `share` of a cell's people to `to` (appended to `out`).
void flee(SimState& s, CellId from, CellId to, double share, RngStream& rng, std::vector<Cohort>& out) {
    for (auto i = s.cohort_begin[from]; i < s.cohort_begin[from + 1]; ++i) {
        Cohort& co = s.cohorts[i];
        Cohort band = co;
        band.cell = to;
        std::int64_t moved = 0;
        for (int b = 0; b < kAgeBins; ++b) {
            band.female[b] = std::min(co.female[b], round_people(share * co.female[b], rng));
            band.male[b] = std::min(co.male[b], round_people(share * co.male[b], rng));
            co.female[b] -= band.female[b];
            co.male[b] -= band.male[b];
            moved += band.female[b] + band.male[b];
        }
        if (moved > 0) {
            s.ledger.migrants += moved;
            out.push_back(band);
        }
    }
}

std::pair<std::uint32_t, std::uint32_t> ordered(std::uint32_t a, std::uint32_t b) {
    return a < b ? std::pair{a, b} : std::pair{b, a};
}

// Active alliances by polity, as alliance indices in ascending order (so
// sums over a polity's allies add up in the same order as a scan of all
// alliances). Rebuilt or extended whenever alliances change.
struct AllianceIndex {
    std::vector<std::vector<std::uint32_t>> of;
    void build(const SimState& s, std::size_t np) {
        of.assign(np, {});
        for (std::size_t i = 0; i < s.alliances.size(); ++i) {
            const Alliance& a = s.alliances[i];
            if (a.ended != kNever) continue;
            if (a.a < np) of[a.a].push_back(static_cast<std::uint32_t>(i));
            if (a.b < np) of[a.b].push_back(static_cast<std::uint32_t>(i));
        }
    }
    void add(const SimState& s, std::size_t i) {  // a new alliance (the highest index)
        const Alliance& a = s.alliances[i];
        if (a.a < of.size()) of[a.a].push_back(static_cast<std::uint32_t>(i));
        if (a.b < of.size()) of[a.b].push_back(static_cast<std::uint32_t>(i));
    }
    [[nodiscard]] bool allied(const SimState& s, std::uint32_t x, std::uint32_t y) const {
        if (x >= of.size()) return false;
        for (std::uint32_t i : of[x]) {
            const Alliance& a = s.alliances[i];
            if ((a.a == x && a.b == y) || (a.a == y && a.b == x)) return true;
        }
        return false;
    }
};

bool on(const std::vector<std::uint32_t>& side, std::uint32_t p) {
    return std::find(side.begin(), side.end(), p) != side.end();
}

void add_to(std::vector<std::uint32_t>& side, const std::vector<std::uint32_t>& other, std::uint32_t p) {
    if (p != 0 && !on(side, p) && !on(other, p)) side.push_back(p);
}

// Bind `subject` to `overlord`, unless that would make a loop.
bool subjugate(SimState& s, std::uint32_t subject, std::uint32_t overlord, Bond bond, Tick tick) {
    if (subject == overlord || top_overlord(s, overlord) == subject) return false;
    Polity& p = s.polities[subject];
    p.overlord = overlord;
    p.bond = bond;
    p.bond_since = tick;
    for (Alliance& a : s.alliances) {  // subjects keep no alliances of their own
        if (a.ended == kNever && (a.a == subject || a.b == subject)) a.ended = tick;
    }
    s.events.push_back({tick, EventType::Subjugated, p.capital, static_cast<std::int64_t>(subject)});
    return true;
}

struct Armies {
    std::vector<double> levy;      // soldiers
    std::vector<double> strength;  // soldiers x quality
};

Armies raise_armies(const World& w, const SimState& s, const RuleField* given = nullptr) {
    const std::size_t np = s.polities.size();
    Armies a{std::vector<double>(np, 0.0), std::vector<double>(np, 0.0)};
    if (s.polity_of.size() != w.cell_count()) return a;
    const std::size_t i_warfare = trait_index(*w.content, "warfare");
    std::vector<double> reach(np, 0.0);
    for (std::uint32_t id = 1; id < np; ++id) {
        if (alive(s, id)) reach[id] = polity_reach(w, s, id);
    }
    RuleField own;
    if (given == nullptr) own.build(w, s, reach);
    const RuleField& rule = given != nullptr ? *given : own;
    for (CellId c = 0; c < w.cell_count(); ++c) {
        const std::uint32_t id = s.polity_of[c];
        if (id == 0 || !alive(s, id) || s.cell_population[c] == 0) continue;
        const Polity& p = s.polities[id];
        const double control = polity_control(rule.km(w, s, id, c), reach[id]);
        a.levy[id] += static_cast<double>(s.cell_population[c]) * control * WarSystem::kLevy *
                      (0.5 + cell_trait(s, c, i_warfare)) * (p.nomadic ? WarSystem::kNomadLevy : 1.0);
    }
    std::vector<double> people(np, 0.0);
    for (CellId c = 0; c < w.cell_count(); ++c) {
        const std::uint32_t id = s.polity_of[c];
        if (id != 0 && id < np) people[id] += static_cast<double>(s.cell_population[c]);
    }
    for (std::uint32_t id = 1; id < np; ++id) {
        if (a.levy[id] <= 0.0) continue;
        const Polity& p = s.polities[id];
        const TechEffects e = technique_effects(w, p.capital, techniques_in(s, p.capital));
        a.strength[id] = a.levy[id] * (1.0 + e.military) *
                         (has_institution(p.institutions, Institution::Army) ? Institutions::kArmyStrength : 1.0);
        // Followers come for the plunder.
        if (p.nomadic) a.strength[id] *= 1.0 + WarSystem::kPlunderStrength * plunder_plenty(p, people[id]);
    }
    return a;
}

}  // namespace

std::vector<double> polity_strengths(const World& w, const SimState& s) { return raise_armies(w, s).strength; }

std::uint32_t army_at(const SimState& s, CellId c) {
    std::uint32_t best = 0;
    for (std::size_t i = 1; i < s.armies.size(); ++i) {
        const Army& a = s.armies[i];
        if (a.to == kNever && a.cell == c && (best == 0 || a.men > s.armies[best].men)) best = static_cast<std::uint32_t>(i);
    }
    return best;
}

double army_strength(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.armies.size()) return 0.0;
    const Army& a = s.armies[id];
    if (a.polity == 0 || a.polity >= s.polities.size()) return 0.0;
    const Polity& p = s.polities[a.polity];
    const TechEffects e = technique_effects(w, p.capital, techniques_in(s, p.capital));
    return a.men * (1.0 + e.military) * (has_institution(p.institutions, Institution::Army) ? Institutions::kArmyStrength : 1.0) *
           general_factor(s, a.polity);
}

bool herder_tribe(const World& w, const SimState& s, std::uint32_t id) {
    if (id == 0 || id >= s.polities.size()) return false;
    const CellId seat = s.polities[id].capital;
    if (polity_has_writing(w, s, id) || (s.city_of.size() > seat && s.city_of[seat] != 0)) return false;
    return s.env.pastoral.size() > seat && s.env.pastoral[seat] >= WarSystem::kHerderTribe;
}

void cells_within(const World& w, CellId start, double km, std::vector<CellId>& out) {
    // Scratch marks (1 = taken, 2 = too far), cleared before returning; one
    // set per thread. The cells come out in the same order as a plain
    // breadth-first search with a membership test.
    thread_local std::vector<std::uint8_t> mark;
    thread_local std::vector<CellId> seen;
    if (mark.size() != w.cell_count()) mark.assign(w.cell_count(), 0);
    seen.clear();
    out.clear();
    out.push_back(start);
    mark[start] = 1;
    seen.push_back(start);
    for (std::size_t i = 0; i < out.size(); ++i) {
        for (CellId nb : w.grid.neighbors(out[i])) {
            if (mark[nb] != 0) continue;
            mark[nb] = w.grid.distance_km(start, nb) > km ? 2 : 1;
            seen.push_back(nb);
            if (mark[nb] == 1) out.push_back(nb);
        }
    }
    for (CellId c : seen) mark[c] = 0;
}

double raid_wealth(const SimState& s, CellId c) {
    if (c >= s.cell_population.size() || s.cell_population[c] == 0) return 0.0;
    const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
    const double pastoral = s.env.pastoral.empty() ? 0.0 : s.env.pastoral[c];
    if (farming < 0.5 || pastoral >= 0.5) return 0.0;
    const double people = static_cast<double>(s.cell_population[c]);
    const double town = s.urban.empty() ? 0.0 : s.urban[c];
    return people * (town + 0.05 * (1.0 - town));
}

std::uint32_t top_overlord(const SimState& s, std::uint32_t id) {
    for (std::size_t guard = 0; guard < s.polities.size() && id != 0 && id < s.polities.size(); ++guard) {
        const std::uint32_t up = s.polities[id].overlord;
        if (up == 0) return id;
        id = up;
    }
    return id;
}

bool allied(const SimState& s, std::uint32_t a, std::uint32_t b) {
    const auto [x, y] = ordered(a, b);
    return std::any_of(s.alliances.begin(), s.alliances.end(),
                       [&](const Alliance& al) { return al.ended == kNever && al.a == x && al.b == y; });
}

std::uint32_t war_between(const SimState& s, std::uint32_t a, std::uint32_t b) {
    for (std::size_t i = 1; i < s.wars.size(); ++i) {
        const War& war = s.wars[i];
        if (war.ended != kNever) continue;
        if ((on(war.attackers, a) && on(war.defenders, b)) || (on(war.attackers, b) && on(war.defenders, a))) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return 0;
}

const char* war_outcome_name(WarOutcome o) {
    switch (o) {
        case WarOutcome::Ongoing: return "under way";
        case WarOutcome::AttackersWon: return "won by the attackers";
        case WarOutcome::DefendersWon: return "won by the defenders";
        case WarOutcome::Stalemate: return "stalemate";
    }
    return "";
}

const char* war_goal_name(WarGoal g) {
    switch (g) {
        case WarGoal::Conquest: return "conquest";
        case WarGoal::Tribute: return "tribute";
        case WarGoal::Independence: return "independence";
    }
    return "";
}

std::uint32_t start_war(const World&, SimState& s, Tick tick, std::uint32_t attacker, std::uint32_t defender,
                        WarGoal goal) {
    if (s.wars.empty()) s.wars.emplace_back();
    War war;
    war.id = static_cast<std::uint32_t>(s.wars.size());
    war.goal = goal;
    war.started = tick;
    war.attackers.push_back(attacker);
    war.defenders.push_back(defender);
    const Polity& d = s.polities[defender];
    // The defender's overlord, allies and vassals stand with it; the
    // attacker's vassals with the attacker.
    if (alive(s, d.overlord) && d.overlord != attacker) add_to(war.defenders, war.attackers, d.overlord);
    for (const Alliance& al : s.alliances) {
        if (al.ended != kNever) continue;
        if (al.a == defender && alive(s, al.b)) add_to(war.defenders, war.attackers, al.b);
        if (al.b == defender && alive(s, al.a)) add_to(war.defenders, war.attackers, al.a);
    }
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        const Polity& p = s.polities[id];
        if (!alive(s, id) || p.bond != Bond::Vassal) continue;
        if (p.overlord == attacker) add_to(war.attackers, war.defenders, id);
        if (p.overlord == defender) add_to(war.defenders, war.attackers, id);
    }
    const std::string& a = s.polities[attacker].name;
    war.name = goal == WarGoal::Independence ? a + " War of Independence" : a + "-" + d.name + " War";
    s.events.push_back({tick, EventType::WarBegan, s.polities[attacker].capital, static_cast<std::int64_t>(war.id)});
    s.wars.push_back(std::move(war));
    return s.wars.back().id;
}

void WarSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    // Grid scale (see World::cell_scale): pushes per level-5 cell of land on
    // finer grids; on coarser ones each cell is several level-5 cells.
    const int pushes_per_cell = static_cast<int>(std::max<std::size_t>(1, w.cells_for(1)));
    const double coarse = std::max(1.0, w.cell_scale() * w.cell_scale());
    if (s.polity_of.size() != n) return;
    if (s.wars.empty()) s.wars.emplace_back();
    s.war_deaths.assign(n, 0);
    s.strife.assign(n, 0);
    for (Polity& p : s.polities) p.plunder *= 1.0 - kPlunderFade;
    s.captives = 0;
    s.raids = 0;
    if (s.last_raid.size() != n) s.last_raid.assign(n, kNever);
    const Tick tick = ctx.tick;
    const std::size_t np = s.polities.size();
    const std::size_t i_warfare = trait_index(content, "warfare");
    const std::size_t ng = content.goods.size();
    const bool have_stocks = s.stocks.size() == n * ng;
    std::vector<Cohort> arrivals;

    // ---- Who rules where, how far, with what armies.
    std::vector<std::vector<CellId>> cells(np);
    for (CellId c = 0; c < n; ++c) {
        if (s.polity_of[c] != 0 && alive(s, s.polity_of[c])) cells[s.polity_of[c]].push_back(c);
    }
    std::vector<double> reach(np, 0.0);
    for (std::uint32_t id = 1; id < np; ++id) {
        if (alive(s, id)) reach[id] = polity_reach(w, s, id);
    }
    RuleField rule;
    rule.build(w, s, reach);
    auto rule_km_of = [&](std::uint32_t id, CellId c) { return rule.km(w, s, id, c); };
    auto control_of = [&](std::uint32_t id, CellId c) {
        return reach[id] > 0.0 ? polity_control(rule_km_of(id, c), reach[id]) : 0.0;
    };
    const double slack = std::max(0.0, kLevel5CellKm * w.cell_scale() - kConquestNearKm);
    const Armies armies = raise_armies(w, s, &rule);
    // People of each polity at the start of the year, to weigh its losses.
    std::vector<double> people_of(np, 0.0);
    for (std::uint32_t id = 1; id < np; ++id) {
        for (CellId c : cells[id]) people_of[id] += static_cast<double>(s.cell_population[c]);
    }
    // The toll of war on a polity's stability (PolitySystem): losses as a
    // share of its people.
    auto blow = [&](std::uint32_t id, double amount) {
        if (id != 0 && id < np && people_of[id] > 0.0) s.polities[id].shock += amount / people_of[id];
    };
    std::vector<double> power = armies.strength;
    for (std::uint32_t id = 1; id < np; ++id) {
        const Polity& p = s.polities[id];
        if (alive(s, id) && p.bond == Bond::Vassal && alive(s, p.overlord)) {
            power[p.overlord] += kVassalShare * armies.strength[id];
        }
    }
    // Neighbouring polities.
    std::vector<std::vector<std::uint32_t>> borders(np);
    for (CellId c = 0; c < n; ++c) {
        const std::uint32_t a = s.polity_of[c];
        if (a == 0 || !alive(s, a)) continue;
        for (CellId nb : w.grid.neighbors(c)) {
            const std::uint32_t b = s.polity_of[nb];
            if (b != 0 && b != a && alive(s, b)) borders[a].push_back(b);
        }
    }
    for (auto& b : borders) {
        std::sort(b.begin(), b.end());
        b.erase(std::unique(b.begin(), b.end()), b.end());
    }

    // ---- The dead free their subjects and leave their alliances; truces run out.
    for (std::uint32_t id = 1; id < np; ++id) {
        Polity& p = s.polities[id];
        if (p.overlord != 0 && (!alive(s, id) || !alive(s, p.overlord))) {
            p.overlord = 0;
            p.bond = Bond::None;
        }
    }
    for (Alliance& a : s.alliances) {
        if (a.ended == kNever && (!alive(s, a.a) || !alive(s, a.b))) a.ended = tick;
    }
    s.truces.erase(std::remove_if(s.truces.begin(), s.truces.end(),
                                  [&](const Truce& t) { return t.until <= tick || !alive(s, t.a) || !alive(s, t.b); }),
                   s.truces.end());

    // ---- Wars under way.
    std::vector<std::int8_t> side(np, 0);
    if (s.armies.empty()) s.armies.emplace_back();
    auto living = [&](std::uint32_t id) { return id != 0 && id < s.armies.size() && s.armies[id].to == kNever; };
    // Armies of wars ended elsewhere (a people's flight) go home.
    for (Army& a : s.armies) {
        if (a.to == kNever && a.war != 0 && a.war < s.wars.size() && s.wars[a.war].ended != kNever) a.to = s.wars[a.war].ended;
    }
    const double spacing = kLevel5CellKm * w.cell_scale();
    std::vector<int> dist(n, -1);
    std::vector<CellId> queue;
    queue.reserve(n);
    for (std::size_t wi = 1; wi < s.wars.size(); ++wi) {
        War& war = s.wars[wi];
        if (war.ended != kNever) continue;
        const std::uint32_t lead_a = war.attackers.front(), lead_d = war.defenders.front();
        auto finish = [&](WarOutcome outcome) {
            war.ended = tick;
            war.outcome = outcome;
            // The armies go home.
            for (std::uint32_t id : {war.army_a, war.army_d}) {
                if (living(id)) s.armies[id].to = tick;
            }
            if (alive(s, lead_a) && alive(s, lead_d)) {
                const auto [x, y] = ordered(lead_a, lead_d);
                s.truces.push_back({x, y, tick + kTruceYears * kTicksPerYear});
            }
            s.events.push_back({tick, EventType::WarEnded, s.polities[lead_a].capital, static_cast<std::int64_t>(war.id)});
        };
        if (!alive(s, lead_a) || !alive(s, lead_d)) {
            finish(!alive(s, lead_d) && alive(s, lead_a)   ? WarOutcome::AttackersWon
                   : !alive(s, lead_a) && alive(s, lead_d) ? WarOutcome::DefendersWon
                                                           : WarOutcome::Stalemate);
            continue;
        }
        auto prune = [&](std::vector<std::uint32_t>& v) {
            v.erase(std::remove_if(v.begin() + 1, v.end(), [&](std::uint32_t p) { return !alive(s, p); }), v.end());
        };
        prune(war.attackers);
        prune(war.defenders);
        for (std::uint32_t p : war.attackers) side[p] = 1;
        for (std::uint32_t p : war.defenders) side[p] = 2;
        auto side_power = [&](const std::vector<std::uint32_t>& v, bool levy) {
            double total = 0.0;
            for (std::size_t i = 0; i < v.size(); ++i) {
                // The leader's general leads the side.
                total += (i == 0 ? general_factor(s, v[i]) : kAllyShare) * (levy ? armies.levy[v[i]] : power[v[i]]);
            }
            return total;
        };
        double pa = side_power(war.attackers, false), pd = side_power(war.defenders, false);
        RngStream rng = ctx.rng(Stream::War, kWarEntity | war.id);

        // ---- Field armies: raised, marched, fought, worn and fed.
        RngStream arng = ctx.rng(Stream::War, kArmyEntity | war.id);
        auto kill_soldiers = [&](std::uint32_t id, double men) -> std::int64_t {
            if (men <= 0.0 || id == 0 || id >= np) return 0;
            std::int64_t total = 0;
            for (CellId c : cells[id]) {
                if (s.polity_of[c] == id) total += fighting_men(s, c);
            }
            if (total <= 0) return 0;
            const double share = std::min(0.5, men / static_cast<double>(total));
            std::int64_t dead = 0;
            for (CellId c : cells[id]) {
                if (s.polity_of[c] == id) dead += kill(s, c, share, true, arng);
            }
            blow(id, kShockDead * static_cast<double>(dead));
            return dead;
        };
        auto raise = [&](std::uint32_t leader, std::uint32_t last) -> std::uint32_t {
            if (last != 0 && last < s.armies.size() && tick - s.armies[last].to < kReraiseYears * kTicksPerYear) return last;
            const double men = kFieldShare * armies.levy[leader];
            if (men < 1.0) return last;
            const Polity& p = s.polities[leader];
            Army a;
            a.id = static_cast<std::uint32_t>(s.armies.size());
            a.polity = leader;
            a.war = war.id;
            a.general = p.commander != 0 && p.commander < s.commanders.size() ? s.commanders[p.commander].figure : 0;
            a.cell = a.goal = p.capital;
            a.men = a.raised = men;
            a.from = tick;
            s.armies.push_back(a);
            return a.id;
        };
        if (!living(war.army_a)) war.army_a = raise(lead_a, war.army_a);
        if (!living(war.army_d)) war.army_d = raise(lead_d, war.army_d);
        auto in_side = [&](CellId c, std::int8_t mine) {
            const std::uint32_t o = s.polity_of[c];
            return o != 0 && o < np && side[o] == mine;
        };
        // Up to `steps` cells by land toward `goal` (breadth-first from the goal).
        auto step_toward = [&](std::uint32_t id, CellId goal, int steps) {
            Army& a = s.armies[id];
            if (steps <= 0 || goal == a.cell || goal == kInvalidCell || !w.is_land[goal]) return;
            for (CellId c : queue) dist[c] = -1;
            queue.clear();
            dist[goal] = 0;
            queue.push_back(goal);
            for (std::size_t i = 0; i < queue.size() && dist[a.cell] < 0; ++i) {
                for (CellId nb : w.grid.neighbors(queue[i])) {
                    if (dist[nb] >= 0 || !w.is_land[nb]) continue;
                    dist[nb] = dist[queue[i]] + 1;
                    queue.push_back(nb);
                }
            }
            for (int k = 0; k < steps && dist[a.cell] > 0; ++k) {
                CellId next = kInvalidCell;
                for (CellId nb : w.grid.neighbors(a.cell)) {
                    if (dist[nb] >= 0 && dist[nb] < dist[a.cell] && (next == kInvalidCell || nb < next)) next = nb;
                }
                if (next == kInvalidCell) break;
                a.cell = next;
            }
        };
        auto march = [&](std::uint32_t me, std::uint32_t foe, std::int8_t mine, std::uint32_t foe_leader) {
            if (!living(me)) return;
            Army& a = s.armies[me];
            CellId goal = s.polities[foe_leader].capital;
            if (living(foe)) {
                const CellId fc = s.armies[foe].cell;
                if (in_side(fc, mine) || w.grid.distance_km(a.cell, fc) <= kEngageKm) goal = fc;
            }
            a.goal = goal;
            const Polity& p = s.polities[a.polity];
            const double transport = technique_effects(w, p.capital, techniques_in(s, p.capital)).land_transport;
            const double reach_cells = kMarchKm * (p.nomadic ? kNomadMarch : 1.0) * (1.0 + transport) / spacing;
            const double whole = std::floor(reach_cells);
            const int steps = static_cast<int>(whole) + (arng.uniform() < reach_cells - whole ? 1 : 0);
            step_toward(me, goal, steps);
        };
        auto close = [&](CellId x, CellId y) {
            if (x == y) return true;
            for (CellId nb : w.grid.neighbors(x)) {
                if (nb == y) return true;
            }
            return false;
        };
        auto battle = [&]() {
            const std::uint32_t ia = war.army_a, id = war.army_d;
            const double sa = army_strength(w, s, ia), sd = army_strength(w, s, id);
            if (sa + sd <= 0.0) return;
            const bool a_wins = arng.uniform() < sa * sa / (sa * sa + sd * sd);
            const std::uint32_t iw = a_wins ? ia : id, il = a_wins ? id : ia;
            const double sw = a_wins ? sa : sd, sl = a_wins ? sd : sa;
            Army& win = s.armies[iw];
            Army& lose = s.armies[il];
            const double lost_l = kRoutLoss * lose.men;
            const double lost_w = kWinLoss * win.men * std::min(1.0, sw > 0.0 ? sl / sw : 1.0);
            lose.men -= lost_l;
            win.men -= lost_w;
            ++win.victories;
            ++lose.defeats;
            Battle b;
            b.tick = tick;
            b.cell = lose.cell;
            b.war = war.id;
            b.winner = iw;
            b.loser = il;
            const std::uint32_t wp = win.polity, lp = lose.polity;
            // One after the other: both draw on the army stream, and the order
            // of the operands of + is not fixed (MSVC differs).
            const std::int64_t dead_l = kill_soldiers(lp, lost_l);
            const std::int64_t dead_w = kill_soldiers(wp, lost_w);
            b.dead = dead_l + dead_w;
            war.deaths += b.dead;
            const std::uint32_t mc = s.polities[wp].commander;
            if (mc != 0 && mc < s.commanders.size() && s.commanders[mc].figure == s.armies[iw].general) {
                s.commanders[mc].fame += kFameBattle;
            }
            s.events.push_back({tick, EventType::BattleFought, b.cell, static_cast<std::int64_t>(s.battles.size())});
            s.battles.push_back(b);
            // The beaten army falls back toward its capital.
            step_toward(il, s.polities[lp].capital, 1);
        };
        march(war.army_a, war.army_d, 1, lead_d);
        if (living(war.army_a) && living(war.army_d) && close(s.armies[war.army_a].cell, s.armies[war.army_d].cell)) {
            battle();
        } else {
            march(war.army_d, war.army_a, 2, lead_a);
            if (living(war.army_a) && living(war.army_d) && close(s.armies[war.army_a].cell, s.armies[war.army_d].cell)) battle();
        }
        auto wear = [&](std::uint32_t me, std::int8_t mine) {
            if (!living(me)) return;
            Army& a = s.armies[me];
            const CellId c = a.cell;
            const bool home = in_side(c, mine);
            const auto biome = static_cast<Biome>(w.biome[c]);
            const bool harsh = biome == Biome::Desert || biome == Biome::Tundra || biome == Biome::TropicalRainforest ||
                               biome == Biome::IceSheet;
            const double lost = kAttrition * (home ? kHomeAttrition : 1.0) * (harsh ? kHarshAttrition : 1.0) * a.men;
            a.men -= lost;
            const std::uint32_t mp = a.polity;
            war.deaths += kill_soldiers(mp, lost);
            Army& b = s.armies[me];
            if (home) b.men = std::min(b.raised, b.men + kRefill * b.raised);
            // It lives off the land.
            const double people = static_cast<double>(s.cell_population[c]);
            if (s.env.food_store.size() == n && people > 0.0) {
                const double need = kForage * b.men;
                const double take = std::min(s.env.food_store[c], need);
                s.env.food_store[c] -= take;
                if (need > take) war.deaths += kill(s, c, kForageDeaths * std::min(1.0, (need - take) / people), false, arng);
            }
            const std::uint32_t owner = s.polity_of[c];
            const std::uint32_t v = s.province_of.size() == n ? s.province_of[c] : 0;
            if (v != 0 && v < s.provinces.size()) {
                s.provinces[v].unrest = std::min(1.0, s.provinces[v].unrest + kArmyUnrest);
            } else if (owner != 0 && owner < np) {
                s.polities[owner].unrest = std::min(1.0, s.polities[owner].unrest + kArmyUnrest);
            }
            if (b.men < kBroken * b.raised) b.to = tick;  // it breaks up
        };
        wear(war.army_a, 1);
        wear(war.army_d, 2);
        // A side without a field army fights on with garrisons and levies.
        if (!living(war.army_a)) pa *= kNoArmy;
        if (!living(war.army_d)) pd *= kNoArmy;
        const bool broken = (war.army_a != 0 && !living(war.army_a)) || (war.army_d != 0 && !living(war.army_d));

        // Cells the two sides held at the start of the year (taken cells
        // stay listed; their owner is read as it is now).
        std::vector<CellId> member_cells;
        for (const auto* v : {&war.attackers, &war.defenders}) {
            for (std::uint32_t q : *v) member_cells.insert(member_cells.end(), cells[q].begin(), cells[q].end());
        }
        std::sort(member_cells.begin(), member_cells.end());
        member_cells.erase(std::unique(member_cells.begin(), member_cells.end()), member_cells.end());

        // One push a year by each side: the stronger takes the enemy cell
        // along the front it controls best.
        std::vector<CellId> front_a, front_d;  // cells of each side on the front
        auto push = [&](std::int8_t mine, double ratio) -> bool {
            CellId target = kInvalidCell, from = kInvalidCell;
            double best = -1.0e18;
            // Land is taken where the side's field army is.
            const std::uint32_t mine_army = mine == 1 ? war.army_a : war.army_d;
            const CellId at = living(mine_army) ? s.armies[mine_army].cell : kInvalidCell;
            front_a.clear();
            front_d.clear();
            for (CellId c : member_cells) {
                const std::uint32_t owner = s.polity_of[c];
                if (owner == 0 || side[owner] == 0) continue;
                for (CellId nb : w.grid.neighbors(c)) {
                    const std::uint32_t other = s.polity_of[nb];
                    if (other == 0 || side[other] == 0 || side[other] == side[owner]) continue;
                    (side[owner] == 1 ? front_a : front_d).push_back(c);
                    if (side[owner] == mine && s.cell_population[nb] > 0) {
                        const double ctl = control_of(owner, nb);
                        if (reach[owner] <= 0.0 ||
                            polity_control(std::max(0.0, rule_km_of(owner, nb) - slack), reach[owner]) < kConquestControl) {
                            continue;
                        }
                        const double score = at != kInvalidCell ? -w.grid.distance_km(at, nb) : ctl;
                        if (score > best || (score == best && nb < target)) best = score, target = nb, from = c;
                    }
                }
            }
            if (target == kInvalidCell || ratio <= 1.0) return false;
            if (rng.uniform() >= kSeize * std::min(2.0, ratio - 1.0) / coarse) return false;
            const std::uint32_t loser = s.polity_of[target];
            // Losing land shakes the loser in proportion to the people lost.
            blow(loser, kShockLand * static_cast<double>(s.cell_population[target]));
            // The sack, and refugees to the nearest cell their side still holds.
            war.deaths += kill(s, target, kSackDeaths, false, rng);
            // Captives carried off to the conquerors' capital.
            {
                const std::uint32_t captor = s.polity_of[from];
                take_captives(s, target, s.polities[captor].capital,
                              captive_share(w, s, captor, target, BondageSystem::kWarCaptives), rng, arrivals);
            }
            CellId refuge = kInvalidCell;
            for (CellId nb : w.grid.neighbors(target)) {
                const std::uint32_t o = s.polity_of[nb];
                if (o != 0 && side[o] == side[loser] && s.cell_population[nb] > 0 &&
                    (refuge == kInvalidCell || s.cell_population[nb] > s.cell_population[refuge])) {
                    refuge = nb;
                }
            }
            if (refuge != kInvalidCell) flee(s, target, refuge, kRefugees, rng, arrivals);
            // Settlers and garrisons from the conquering side move in.
            flee(s, from, target, kConquestSettlers, rng, arrivals);
            s.polity_of[target] = s.polity_of[from];
            s.strife[target] = 2;
            // The army moves into what it took.
            if (at != kInvalidCell && close(at, target)) s.armies[mine_army].cell = target;
            return true;
        };
        // On finer grids, as many pushes as cells make up a level-5 cell, so
        // land changes hands at the same pace in km^2.
        for (int k = 0; k < std::clamp(static_cast<int>(pa / pd), 1, kMaxPushes) * pushes_per_cell; ++k) {
            if (push(1, pa / pd)) ++war.cells_taken;
        }
        for (int k = 0; k < std::clamp(static_cast<int>(pd / pa), 1, kMaxPushes) * pushes_per_cell; ++k) {
            if (push(2, pd / pa)) ++war.cells_lost;
        }

        // Battles along the front.
        std::sort(front_a.begin(), front_a.end());
        front_a.erase(std::unique(front_a.begin(), front_a.end()), front_a.end());
        std::sort(front_d.begin(), front_d.end());
        front_d.erase(std::unique(front_d.begin(), front_d.end()), front_d.end());
        if (!front_a.empty() && !front_d.empty()) {
            const double engaged = kEngaged * std::min(side_power(war.attackers, true), side_power(war.defenders, true));
            auto losses = [&](const std::vector<CellId>& front, double dead) {
                std::int64_t men = 0;
                for (CellId c : front) men += fighting_men(s, c);
                if (men <= 0) return;
                const double share = std::min(0.5, dead / static_cast<double>(men));
                for (CellId c : front) {
                    const std::int64_t fallen = kill(s, c, share, true, rng);
                    war.deaths += fallen;
                    blow(s.polity_of[c], kShockDead * static_cast<double>(fallen));
                    if (s.strife[c] == 0) s.strife[c] = 1;
                }
            };
            losses(front_a, kBattleDeaths * engaged * 2.0 * pd / (pa + pd));
            losses(front_d, kBattleDeaths * engaged * 2.0 * pa / (pa + pd));
        }

        // Does it end?
        const CellId cap_a = s.polities[lead_a].capital, cap_d = s.polities[lead_d].capital;
        const double years = static_cast<double>(tick - war.started) / kTicksPerYear;
        WarOutcome outcome = WarOutcome::Ongoing;
        bool capital_fell = false;
        if (side[s.polity_of[cap_d]] == 1) {
            outcome = WarOutcome::AttackersWon;
            capital_fell = true;
        } else if (side[s.polity_of[cap_a]] == 2) {
            outcome = WarOutcome::DefendersWon;
            capital_fell = true;
        } else if (rng.uniform() < kPeace * (1.0 + years / 10.0) * (broken ? kNoArmyPeace : 1.0)) {
            outcome = war.cells_taken > war.cells_lost   ? WarOutcome::AttackersWon
                      : war.cells_lost > war.cells_taken ? WarOutcome::DefendersWon
                                                         : WarOutcome::Stalemate;
        }
        for (std::uint32_t p : war.attackers) side[p] = 0;
        for (std::uint32_t p : war.defenders) side[p] = 0;
        if (outcome == WarOutcome::Ongoing) continue;

        // Terms.
        auto own_cells = [&](std::uint32_t id) {
            std::size_t k = 0;
            for (CellId c = 0; c < n; ++c) k += s.polity_of[c] == id;
            return k;
        };
        auto annex = [&](std::uint32_t loser, std::uint32_t winner) {
            for (CellId c = 0; c < n; ++c) {
                if (s.polity_of[c] == loser) s.polity_of[c] = winner;
            }
            s.polities[loser].fate = PolityFate::Conquered;
        };
        const std::uint32_t winner = outcome == WarOutcome::AttackersWon ? lead_a : lead_d;
        const std::uint32_t loser = outcome == WarOutcome::AttackersWon ? lead_d : lead_a;
        if (outcome != WarOutcome::Stalemate) {
            // The generals: fame for the victor, blame for the vanquished.
            auto general_of = [&](std::uint32_t p) -> Commander* {
                const std::uint32_t c = s.polities[p].commander;
                return c != 0 && c < s.commanders.size() ? &s.commanders[c] : nullptr;
            };
            if (Commander* m = general_of(winner)) {
                ++m->victories;
                m->fame += PolitySystem::kFameVictory + (capital_fell ? PolitySystem::kFameCapital : 0.0);
            }
            if (Commander* m = general_of(loser)) {
                ++m->defeats;
                m->fame = std::max(0.0, m->fame - PolitySystem::kFameDefeat);
                RngStream blame = ctx.rng(Stream::War, kWarEntity | 0x04000000u | war.id);
                if (blame.uniform() < PolitySystem::kDismiss) {
                    m->to = tick;
                    s.polities[loser].commander = 0;
                }
            }
            s.polities[loser].shock += kShockDefeat;
            s.polities[winner].shock = std::max(0.0, s.polities[winner].shock - kVictoryRelief);
            if (war.goal == WarGoal::Independence) {
                if (winner == lead_a) {
                    Polity& rebel = s.polities[lead_a];
                    if (rebel.overlord == lead_d) rebel.overlord = 0, rebel.bond = Bond::None;
                } else if (own_cells(lead_a) <= w.cells_for(kAnnexBelowCells)) {
                    annex(lead_a, lead_d);
                } else {
                    subjugate(s, lead_a, lead_d, Bond::Vassal, tick);
                }
            } else if (winner == lead_a && war.goal == WarGoal::Tribute) {
                Polity& nomads = s.polities[lead_a];
                const CellId seat = s.polities[lead_d].capital;
                const bool civilised = polity_has_writing(w, s, lead_d) || s.city_of[seat] != 0;
                if (capital_fell && nomads.nomadic && civilised &&
                    power[lead_a] >= kConquestDynastyRatio * power[lead_d]) {
                    // A conquest dynasty: the nomads rule the state from its capital.
                    annex(lead_d, lead_a);
                    // The horde comes with them.
                    RngStream horde = ctx.rng(Stream::War, kWarEntity | 0x08000000u | war.id);
                    flee(s, nomads.capital, seat, kDynastyHerders, horde, arrivals);
                    nomads.capital = seat;
                    nomads.settling_since = tick;
                    s.events.push_back({tick, EventType::ConquestDynasty, seat, static_cast<std::int64_t>(lead_a)});
                } else {
                    // Herder tribes join the confederation as clans; others pay.
                    subjugate(s, lead_d, lead_a,
                              nomads.nomadic && herder_tribe(w, s, lead_d) ? Bond::Vassal : Bond::Tributary, tick);
                }
            } else if (winner == lead_a && capital_fell) {
                if (own_cells(loser) <= w.cells_for(kAnnexBelowCells)) {
                    annex(loser, winner);
                } else {
                    subjugate(s, loser, winner, Bond::Vassal, tick);
                }
            }
        }
        finish(outcome);
    }

    // ---- Subjects: tribute, rebellion, absorption.
    for (std::uint32_t id = 1; id < np; ++id) {
        Polity& p = s.polities[id];
        if (!alive(s, id) || p.bond == Bond::None || !alive(s, p.overlord)) continue;
        const std::uint32_t lord = p.overlord;
        if (have_stocks) {
            const CellId from = p.capital, to = s.polities[lord].capital;
            for (std::size_t g = 0; g < ng; ++g) {
                if (g == content.food_good) continue;
                const double q = kTribute * s.stocks[from * ng + g];
                s.stocks[from * ng + g] -= q;
                s.stocks[to * ng + g] += q;
                s.polities[lord].plunder += q * content.goods[g].base_price;
            }
        }
        // Grain too: a share of the subject's stores goes to the overlord's
        // capital (the state's granaries).
        if (s.env.food_store.size() == n) {
            const double q = kTribute * s.env.food_store[p.capital];
            s.env.food_store[p.capital] -= q;
            s.env.food_store[s.polities[lord].capital] += q;
        }
        RngStream rng = ctx.rng(Stream::War, kDiplomacyEntity | id);
        const double rival = power[lord] - (p.bond == Bond::Vassal ? kVassalShare * armies.strength[id] : 0.0);
        if (armies.strength[id] >= kRebelRatio * rival && war_between(s, id, lord) == 0) {
            if (rng.uniform() < kRebellion) start_war(w, s, tick, id, lord, WarGoal::Independence);
        } else if (p.bond == Bond::Vassal && war_between(s, id, lord) == 0 &&
                   // Distance counted as for conquest (less the cell spacing beyond
                   // kConquestNearKm), so a bordering vassal can be held at any grid level.
                   ((polity_control(std::max(0.0, rule_km_of(lord, p.capital) - slack), reach[lord]) >= kAbsorbControl &&
                     rng.uniform() < kAbsorb) ||
                    // A herder clan bordering its confederation merges into it.
                    (s.polities[lord].nomadic && herder_tribe(w, s, id) &&
                     std::binary_search(borders[id].begin(), borders[id].end(), lord) && rng.uniform() < kClanAbsorb))) {
            for (CellId c : cells[id]) {
                if (s.polity_of[c] == id) s.polity_of[c] = lord;
            }
            for (std::uint32_t q = 1; q < np; ++q) {  // its own subjects pass to the overlord
                if (s.polities[q].overlord == id) s.polities[q].overlord = lord;
            }
            p.overlord = 0;
            p.bond = Bond::None;
            p.fate = PolityFate::Absorbed;
            s.events.push_back({tick, EventType::Absorbed, p.capital, static_cast<std::int64_t>(id)});
        }
    }

    // ---- Alliances lapse, and threatened polities seek allies.
    for (std::size_t i = 0; i < s.alliances.size(); ++i) {
        Alliance& a = s.alliances[i];
        if (a.ended != kNever) continue;
        RngStream rng = ctx.rng(Stream::War, kAllianceEntity | static_cast<std::uint32_t>(i));
        if (rng.uniform() < kAllianceEnd || war_between(s, a.a, a.b) != 0) a.ended = tick;
    }
    std::vector<std::size_t> ally_count(np, 0);
    for (const Alliance& a : s.alliances) {
        if (a.ended == kNever) ++ally_count[a.a], ++ally_count[a.b];
    }
    AllianceIndex allies;
    allies.build(s, np);
    for (std::uint32_t id = 1; id < np; ++id) {
        if (!alive(s, id) || s.polities[id].bond != Bond::None || ally_count[id] >= kMaxAllies) continue;
        std::uint32_t threat = 0;
        for (std::uint32_t b : borders[id]) {
            if (power[b] > kThreat * power[id] && !allies.allied(s, id, b) && (threat == 0 || power[b] > power[threat])) {
                threat = b;
            }
        }
        if (threat == 0) continue;
        // The best partner: a neighbour of either, strongest, related culture first.
        const std::uint32_t root = s.cultures[s.polities[id].culture].root;
        std::uint32_t partner = 0;
        double best = 0.0;
        for (const auto* list : {&borders[id], &borders[threat]}) {
            for (std::uint32_t b : *list) {
                if (b == id || b == threat || s.polities[b].bond != Bond::None || ally_count[b] >= kMaxAllies) continue;
                if (allies.allied(s, id, b) || allies.allied(s, threat, b) || war_between(s, id, b) != 0) continue;
                const double score = power[b] * (s.cultures[s.polities[b].culture].root == root ? 2.0 : 1.0);
                if (score > best || (score == best && b < partner)) best = score, partner = b;
            }
        }
        if (partner == 0) continue;
        RngStream rng = ctx.rng(Stream::War, kAllianceEntity | 0x08000000u | id);
        const bool related = s.cultures[s.polities[partner].culture].root == root;
        if (rng.uniform() < kAlliance * (related ? 2.0 : 1.0)) {
            const auto [x, y] = ordered(id, partner);
            s.alliances.push_back({x, y, tick, kNever});
            allies.add(s, s.alliances.size() - 1);
            ++ally_count[x];
            ++ally_count[y];
            s.events.push_back({tick, EventType::AllianceFormed, s.polities[id].capital,
                                static_cast<std::int64_t>(s.alliances.size() - 1)});
        }
    }

    // ---- New wars: a free polity may attack a neighbour it far outmatches.
    std::vector<std::uint8_t> busy(np, 0);
    for (std::size_t wi = 1; wi < s.wars.size(); ++wi) {
        const War& war = s.wars[wi];
        if (war.ended != kNever) continue;
        for (std::uint32_t p : war.attackers) busy[p] = 1;
        for (std::uint32_t p : war.defenders) busy[p] = 1;
    }
    // Truces in force, as sorted pairs.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> truced;
    for (const Truce& t : s.truces) truced.emplace_back(t.a, t.b);
    std::sort(truced.begin(), truced.end());
    for (std::uint32_t id = 1; id < np; ++id) {
        const Polity& p = s.polities[id];
        if (!alive(s, id) || p.bond != Bond::None || busy[id] || borders[id].empty()) continue;
        RngStream rng = ctx.rng(Stream::War, kDeclareEntity | id);
        const double warfare = cell_trait(s, p.capital, i_warfare);
        for (std::uint32_t b : borders[id]) {
            if (busy[b] || top_overlord(s, b) == id || allies.allied(s, id, b) ||
                std::binary_search(truced.begin(), truced.end(), std::pair{std::min(id, b), std::max(id, b)})) {
                continue;
            }
            const Polity& q = s.polities[b];
            double defence = power[b];
            if (alive(s, q.overlord)) defence += kAllyShare * power[q.overlord];
            for (std::uint32_t i : allies.of[b]) {
                const Alliance& al = s.alliances[i];
                defence += kAllyShare * power[al.a == b ? al.b : al.a];
            }
            const double ratio = defence > 0.0 ? power[id] / defence : 0.0;
            if (ratio < kDeclareRatio) continue;
            // A martial ruler is readier for war, a peaceable one less so; an
            // ambitious one more again.
            const std::uint32_t king = s.polities[id].ruler;
            const double martial = king != 0 && king < s.rulers.size() ? s.rulers[king].martial : 0.5;
            const double ambition = trait_factor(reigning_person(s, id), &Figure::ambition);
            if (rng.uniform() < kDeclare * (0.5 + warfare) * (0.5 + martial) * ambition * std::min(2.0, ratio - 1.0)) {
                start_war(w, s, tick, id, b, p.nomadic && !q.nomadic ? WarGoal::Tribute : WarGoal::Conquest);
                busy[id] = busy[b] = 1;
                break;
            }
        }
    }

    // ---- Nomad raids on the richest settled farmers within reach.
    std::vector<CellId> around;
    for (std::uint32_t id = 1; id < np; ++id) {
        const Polity& p = s.polities[id];
        if (!alive(s, id) || !p.nomadic || p.bond != Bond::None) continue;
        RngStream rng = ctx.rng(Stream::War, kRaidEntity | id);
        if (rng.uniform() >= kRaid) continue;
        CellId target = kInvalidCell;
        double richest = 0.0;
        for (CellId c : cells[id]) {
            cells_within(w, c, kRaidKm, around);
            for (CellId nb : around) {
                const std::uint32_t owner = s.polity_of[nb];
                if (owner == id) continue;
                if (owner != 0 && (s.polities[owner].nomadic || top_overlord(s, owner) == id || allies.allied(s, id, owner))) {
                    continue;
                }
                const double wealth = raid_wealth(s, nb);
                if (wealth > richest || (wealth == richest && wealth > 0.0 && nb < target)) {
                    richest = wealth;
                    target = nb;
                }
            }
        }
        if (target == kInvalidCell) continue;
        const std::uint32_t owner = s.polity_of[target];
        // A raid strikes the target's hinterland (the cell itself at level 5).
        std::vector<CellId> struck;
        double struck_people = 0.0;
        w.for_hinterland(target, [&](CellId h) {
            if (h != target && (s.polity_of[h] != owner || s.cell_population[h] == 0)) return;
            struck.push_back(h);
            struck_people += static_cast<double>(s.cell_population[h]);
        });
        const double defence = owner != 0 ? power[owner] * control_of(owner, target) : struck_people * kLevy;
        const double attack = power[id] * control_of(id, target);
        if (attack <= 0.0 || rng.uniform() >= attack / (attack + defence)) continue;
        ++s.raids;
        s.events.push_back({tick, EventType::Raid, target, static_cast<std::int64_t>(id)});
        for (CellId h : struck) {
            s.strife[h] = 3;
            s.last_raid[h] = tick;
            kill(s, h, kRaidDeaths, false, rng);
            // Captives carried off to the raiders' camp.
            take_captives(s, h, p.capital, captive_share(w, s, id, h, BondageSystem::kRaidCaptives), rng, arrivals);
        }
        if (owner != 0) s.polities[owner].shock += kShockRaid;
        // The raiders drive off herds and carry off grain.
        if (s.env.food_store.size() == n) {
            for (CellId h : struck) {
                const double q = kRaidLoot * s.env.food_store[h];
                s.env.food_store[h] -= q;
                s.env.food_store[p.capital] += q;
            }
        }
        if (have_stocks) {
            for (CellId h : struck) {
                for (std::size_t g = 0; g < ng; ++g) {
                    if (g == content.food_good) continue;
                    const double q = kRaidLoot * s.stocks[h * ng + g];
                    s.stocks[h * ng + g] -= q;
                    s.stocks[p.capital * ng + g] += q;
                    s.polities[id].plunder += q * content.goods[g].base_price;
                }
            }
        }
        // The victim's ruler buys peace with tribute.
        if (owner != 0 && s.polities[owner].bond == Bond::None && power[id] > kRaidTributeRatio * power[owner] &&
            war_between(s, id, owner) == 0 && rng.uniform() < kRaidTribute) {
            subjugate(s, owner, id, herder_tribe(w, s, owner) ? Bond::Vassal : Bond::Tributary, tick);
            allies.build(s, np);  // the new subject's alliances ended
        }
    }

    // ---- Refugees arrive; the dead are gone.
    s.cohorts.insert(s.cohorts.end(), arrivals.begin(), arrivals.end());
    s.normalize_cohorts(n);

    // ---- Polities overrun this year, left with no land, end now.
    std::vector<std::uint8_t> landed(s.polities.size(), 0);
    for (CellId c = 0; c < n; ++c) landed[s.polity_of[c]] = 1;
    for (std::uint32_t id = 1; id < s.polities.size(); ++id) {
        Polity& p = s.polities[id];
        if (p.ended != kNever || landed[id]) continue;
        p.ended = tick;
        if (p.fate == PolityFate::Living) p.fate = PolityFate::Conquered;
        s.events.push_back({tick, EventType::PolityEnded, p.capital, static_cast<std::int64_t>(id)});
        // Its subjects go free and its alliances end with it.
        for (Polity& q : s.polities) {
            if (q.overlord == id) {
                q.overlord = 0;
                q.bond = Bond::None;
            }
        }
        for (Alliance& a : s.alliances) {
            if (a.ended == kNever && (a.a == id || a.b == id)) a.ended = tick;
        }
    }
}

std::int64_t kill_in_strife(SimState& s, CellId c, double share, RngStream& rng) { return kill(s, c, share, false, rng); }

}  // namespace histsim
