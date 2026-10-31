#include "histsim/systems/disease.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "histsim/portable_math.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kSpreadEntity = 0x40000000u;

}  // namespace

double disease_suitability(const World& w, const DiseaseDef& d, CellId c) {
    const auto& all = w.content->diseases;
    const auto index = static_cast<std::size_t>(&d - all.data());
    if (index < all.size() && w.disease_niche.size() == w.cell_count() * all.size()) {
        return w.disease_niche[c * all.size() + index];
    }
    if (!w.is_land[c]) return 0.0;
    if (!d.has_niche) return 1.0;
    return d.warmest_month_c(w.warmest_month_c(c)) * d.annual_precip_mm(w.precipitation_mm[c]) *
           d.elevation_m(w.elevation_m[c]);
}

double outbreak_size(double r, double s) {
    if (r * s <= 1.0 || s <= 0.0) return 0.0;
    double a = s;
    for (int i = 0; i < 80; ++i) a = s * (1.0 - pmath::exp(-r * a));
    return a;
}

double contact_factor(double density) { return std::clamp(std::sqrt(density), 0.3, 1.5); }

void DiseaseSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    const std::size_t nd = content.diseases.size();
    if (nd == 0) return;
    if (s.disease_present.size() != n * nd) s.disease_present.assign(n * nd, 0);
    if (s.susceptible.size() != n * nd) s.susceptible.assign(n * nd, 1.0);

    std::vector<std::uint8_t> next = s.disease_present;
    std::vector<double> burden(n * nd, 0.0);  // endemic: burden; epidemic: attack rate this year

    for (CellId c = 0; c < n; ++c) {
        const std::size_t base = c * nd;
        if (s.cell_population[c] == 0) {
            for (std::size_t d = 0; d < nd; ++d) {
                next[base + d] = 0;
                s.susceptible[base + d] = 1.0;  // a new population has never met it
            }
            continue;
        }
        const double pop = static_cast<double>(s.cell_population[c]);
        const double density = pop / w.grid.area_km2(c);
        const double farming = s.env.farming.empty() ? 0.0 : s.env.farming[c];
        const std::uint32_t known = s.domesticates.empty() ? 0 : s.domesticates[c];
        RngStream rng = ctx.rng(Stream::Disease, c);

        for (std::size_t d = 0; d < nd; ++d) {
            const DiseaseDef& def = content.diseases[d];
            const std::size_t k = base + d;
            const double suit = disease_suitability(w, def, c);
            if (suit <= 0.0) {
                next[k] = 0;
                continue;
            }
            double& sus = s.susceptible[k];
            if (def.kind == DiseaseKind::Epidemic) {
                sus += kSusceptibleInflow * (1.0 - sus);
                if (def.immunity_years > 0.0) sus += (1.0 - sus) / def.immunity_years;
                sus = std::min(1.0, sus);
            }

            // Emergence: zoonoses where people keep the hosts, others in
            // their niche, both only among enough people.
            if (!s.disease_present[k] && def.emergence_chance > 0.0 && pop >= def.emergence_min_population &&
                (def.hosts == 0 || (known & def.hosts) != 0)) {
                if (rng.uniform() < def.emergence_chance * suit) {
                    s.disease_present[k] = 1;
                    next[k] = 1;
                    // A new origin only if it never arose or circulated near
                    // here before; repeat spillovers are not news.
                    bool origin = true;
                    for (CellId o = 0; o < n && origin; ++o) {
                        if (o != c && s.disease_present[o * nd + d] && w.grid.distance_km(o, c) < kOriginKm) {
                            origin = false;
                        }
                    }
                    for (const DiseaseOrigin& prior : s.disease_origins) {
                        if (prior.disease == d && w.grid.distance_km(prior.cell, c) < kOriginKm) origin = false;
                    }
                    if (origin) {
                        s.disease_origins.push_back({ctx.tick, c, static_cast<std::uint32_t>(d)});
                        s.events.push_back({ctx.tick, EventType::DiseaseEmerged, c, static_cast<std::int64_t>(d)});
                    }
                }
            }
            if (!s.disease_present[k]) continue;

            if (def.kind == DiseaseKind::Endemic) {
                const double b =
                    suit * (def.farming_weight * farming + def.density_weight * std::min(1.0, density / 2.0));
                burden[k] = b;
                // Slowly reaches suitable neighbours.
                for (CellId nb : w.grid.neighbors(c)) {
                    const std::size_t j = nb * nd + d;
                    if (s.cell_population[nb] == 0 || s.disease_present[j] || next[j]) continue;
                    if (disease_suitability(w, def, nb) <= 0.0) continue;
                    if (def.hosts != 0 && (s.domesticates[nb] & def.hosts) == 0) continue;
                    if (rng.uniform32() < kEndemicSpread * b) next[j] = 1;
                }
            } else {
                const double attack = outbreak_size(def.r0 * contact_factor(density), sus);
                burden[k] = attack;
                if (def.immunity_years != 0.0) sus = std::max(0.0, sus - attack);
                // Too few people to keep it going: it burns out here.
                if (pop < def.critical_population) next[k] = 0;
                if (attack > 0.0) {
                    RngStream spread = ctx.rng(Stream::Disease, kSpreadEntity | c);
                    for (CellId nb : w.grid.neighbors(c)) {
                        const std::size_t j = nb * nd + d;
                        if (s.cell_population[nb] == 0 || s.disease_present[j] || next[j]) continue;
                        if (disease_suitability(w, def, nb) <= 0.0) continue;
                        if (spread.uniform32() < std::min(0.9, kEpidemicSpread * attack)) next[j] = 1;
                    }
                }
            }
        }
    }

    // ---- This year's extra chance of death by age, for demography to apply.
    s.env.disease_mortality.assign(n * kDiseaseAgeGroups, 0.0);
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) continue;
        for (std::size_t d = 0; d < nd; ++d) {
            const double b = burden[c * nd + d];
            if (b <= 0.0) continue;
            const DiseaseDef& def = content.diseases[d];
            for (int g = 0; g < kDiseaseAgeGroups; ++g) {
                s.env.disease_mortality[c * kDiseaseAgeGroups + g] +=
                    def.kind == DiseaseKind::Endemic ? def.excess_mortality * def.age_weight[g] * b : b * def.fatality[g];
            }
        }
    }
    s.disease_present = std::move(next);
}

}  // namespace histsim
