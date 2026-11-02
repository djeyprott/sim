#include "histsim/systems/bondage.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "histsim/systems/religion.hpp"
#include "histsim/systems/war.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kSaleEntity = 0x10000000u;

std::size_t trait_index(const Content& c, const char* id) {
    for (std::size_t t = 0; t < c.traits.size(); ++t) {
        if (c.traits[t].id == id) return t;
    }
    return c.traits.size();
}

// People-weighted mean of a trait in a cell (0.5 if the pack has none).
double cell_trait(const SimState& s, CellId c, std::size_t t) {
    if (t >= kMaxTraits) return 0.5;
    double sum = 0.0, total = 0.0;
    for (const Cohort& co : s.cohorts_in(c)) {
        sum += static_cast<double>(co.total()) * co.traits[t];
        total += static_cast<double>(co.total());
    }
    return total > 0.0 ? sum / total : 0.5;
}

// The founding lineage of a cell's largest people (0 if empty).
std::uint32_t main_root(const SimState& s, CellId c) {
    const Cohort* lead = nullptr;
    for (const Cohort& co : s.cohorts_in(c)) {
        if (lead == nullptr || co.total() > lead->total()) lead = &co;
    }
    return lead == nullptr ? 0 : s.cultures[lead->culture].root;
}

std::int64_t round_people(double x, RngStream& rng) {
    const double f = std::floor(x);
    return static_cast<std::int64_t>(f) + (rng.uniform() < x - f ? 1 : 0);
}

// Split `count` enslaved people off a cohort to `to` (all in bondage); the
// cohort keeps the rest, its bonded share lowered to match.
Cohort sell_off(Cohort& co, CellId to, double count, RngStream& rng) {
    Cohort band = co;
    band.cell = to;
    band.bonded = 1.0;
    const double n = static_cast<double>(co.total());
    const double share = n > 0.0 ? std::clamp(count / n, 0.0, 1.0) : 0.0;
    std::int64_t moved = 0;
    for (int b = 0; b < kAgeBins; ++b) {
        band.female[b] = static_cast<std::uint32_t>(std::min<std::int64_t>(co.female[b], round_people(share * co.female[b], rng)));
        band.male[b] = static_cast<std::uint32_t>(std::min<std::int64_t>(co.male[b], round_people(share * co.male[b], rng)));
        co.female[b] -= band.female[b];
        co.male[b] -= band.male[b];
        moved += band.female[b] + band.male[b];
    }
    const double left = n - static_cast<double>(moved);
    co.bonded = left > 0.0 ? std::clamp((co.bonded * n - static_cast<double>(moved)) / left, 0.0, 1.0) : 0.0;
    return band;
}

}  // namespace

double captive_share(const World& w, const SimState& s, std::uint32_t captor, CellId from, double share) {
    if (captor == 0 || captor >= s.polities.size() || from >= w.cell_count()) return 0.0;
    const Polity& p = s.polities[captor];
    // Fewer of their own lineage.
    if (p.culture < s.cultures.size() && s.cultures[p.culture].root == main_root(s, from)) share *= BondageSystem::kKinCaptives;
    // Few of their own (organized) faith.
    if (s.faith_of.size() == w.cell_count() && religion_organized(w, s, p.religion) &&
        religion_organized(w, s, s.faith_of[from]) &&
        religion_family(s, p.religion) == religion_family(s, s.faith_of[from])) {
        share *= BondageSystem::kFaithCaptives;
    }
    return share;
}

std::int64_t take_captives(SimState& s, CellId from, CellId to, double share, RngStream& rng, std::vector<Cohort>& out) {
    if (share <= 0.0 || from == to) return 0;
    std::int64_t taken = 0;
    for (auto i = s.cohort_begin[from]; i < s.cohort_begin[from + 1]; ++i) {
        Cohort& co = s.cohorts[i];
        Cohort band = co;
        band.cell = to;
        band.bonded = 1.0;
        std::int64_t moved = 0;
        for (int b = 0; b < kAgeBins; ++b) {
            band.female[b] = static_cast<std::uint32_t>(std::min<std::int64_t>(co.female[b], round_people(share * co.female[b], rng)));
            band.male[b] = static_cast<std::uint32_t>(std::min<std::int64_t>(co.male[b], round_people(share * co.male[b], rng)));
            co.female[b] -= band.female[b];
            co.male[b] -= band.male[b];
            moved += band.female[b] + band.male[b];
        }
        if (moved > 0) {
            s.ledger.migrants += moved;
            out.push_back(band);
            taken += moved;
        }
    }
    s.captives += taken;
    return taken;
}

std::int64_t bonded_people(const SimState& s, CellId c) {
    double n = 0.0;
    for (const Cohort& co : s.cohorts_in(c)) n += co.bonded * static_cast<double>(co.total());
    return static_cast<std::int64_t>(n + 0.5);
}

double bondage_demand(const World& w, const SimState& s, CellId c) {
    if (s.cell_population[c] == 0) return 0.0;
    const Content& content = *w.content;
    const double urban = s.urban.size() == w.cell_count() ? s.urban[c] : 0.0;
    const double farming = s.env.farming.size() == w.cell_count() ? s.env.farming[c] : 0.0;
    // Mines and salt pans worked by settled people.
    double mine = 0.0;
    if (farming >= 0.5 && !w.deposits.empty()) {
        for (std::size_t g = 0; g < content.goods.size(); ++g) {
            if (content.goods[g].source == GoodSource::Deposit && content.goods[g].id != "stone" && w.deposit(c, g) >= 0.3) {
                mine = 1.0;
            }
        }
    }
    const double hierarchy = cell_trait(s, c, trait_index(content, "hierarchy"));
    const double sharing = cell_trait(s, c, trait_index(content, "sharing"));
    const double want = BondageSystem::kTownDemand * urban + BondageSystem::kMineDemand * mine +
                        BondageSystem::kEstateDemand * farming * hierarchy;
    return std::clamp(want * (1.5 - sharing), 0.0, BondageSystem::kMaxDemand);
}

void BondageSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    s.sold = 0;

    // ---- Freedom: some of the enslaved go free each year, more of those who
    // have taken up the ways of the land.
    std::vector<std::uint32_t> root(n, 0);
    for (CellId c = 0; c < n; ++c) root[c] = main_root(s, c);
    double held_total = 0.0;
    for (Cohort& co : s.cohorts) {
        if (co.bonded <= 0.0) continue;
        const bool local = s.cultures[co.culture].root == root[co.cell];
        co.bonded *= 1.0 - (kManumission + (local ? kManumissionLocal : 0.0));
        if (co.bonded < 1e-4) co.bonded = 0.0;
        held_total += co.bonded * static_cast<double>(co.total());
    }
    if (held_total <= 0.0) return;

    // ---- Trade: surplus captives are sold to where they are wanted most.
    std::vector<double> held(n, 0.0), want(n, 0.0);
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) continue;
        held[c] = static_cast<double>(bonded_people(s, c));
        want[c] = bondage_demand(w, s, c) * static_cast<double>(s.cell_population[c]);
    }
    const bool markets = s.trade_value.size() == n;
    std::vector<CellId> around;
    std::vector<Cohort> sold;
    for (CellId c = 0; c < n; ++c) {
        const double surplus = held[c] - want[c];
        if (surplus * kSale < static_cast<double>(kMinSale)) continue;
        // The buyer: most wanting, nearest, on the market network.
        cells_within(w, c, kTradeKm, around);
        CellId buyer = kInvalidCell;
        double best = 0.0;
        for (CellId d : around) {
            if (d == c || (markets && s.trade_value[d] <= 0.0)) continue;
            const double gap = want[d] - held[d];
            if (gap < static_cast<double>(kMinSale)) continue;
            const double appeal = gap / (1.0 + w.grid.distance_km(c, d) / kTradeDistanceKm);
            if (appeal > best || (appeal == best && d < buyer)) best = appeal, buyer = d;
        }
        if (buyer == kInvalidCell) continue;
        const double lot = std::min(kSale * surplus, want[buyer] - held[buyer]);
        // Taken from each group in proportion to its enslaved.
        RngStream rng = ctx.rng(Stream::Bondage, kSaleEntity | c);
        std::int64_t moved = 0;
        std::uint32_t language = 0;
        std::int64_t largest = 0;
        for (auto i = s.cohort_begin[c]; i < s.cohort_begin[c + 1]; ++i) {
            Cohort& co = s.cohorts[i];
            const double mine = co.bonded * static_cast<double>(co.total());
            if (mine <= 0.0) continue;
            Cohort band = sell_off(co, buyer, lot * mine / held[c], rng);
            const std::int64_t k = band.total();
            if (k <= 0) continue;
            moved += k;
            if (k > largest) largest = k, language = band.language;
            sold.push_back(band);
        }
        if (moved <= 0) continue;
        held[c] -= static_cast<double>(moved);
        held[buyer] += static_cast<double>(moved);
        s.sold += moved;
        s.ledger.migrants += moved;
        // The route, kept sorted by (from, to).
        auto it = std::lower_bound(s.slave_routes.begin(), s.slave_routes.end(), std::pair{c, buyer},
                                   [](const SlaveRoute& r, const std::pair<CellId, CellId>& k) {
                                       return r.from < k.first || (r.from == k.first && r.to < k.second);
                                   });
        if (it == s.slave_routes.end() || it->from != c || it->to != buyer) {
            SlaveRoute r;
            r.from = c;
            r.to = buyer;
            r.first = ctx.tick;
            it = s.slave_routes.insert(it, r);
        }
        it->people += moved;
        it->last = ctx.tick;
        it->language = language;
    }
    if (!sold.empty()) {
        s.cohorts.insert(s.cohorts.end(), sold.begin(), sold.end());
        s.normalize_cohorts(n);
    }
}

}  // namespace histsim
