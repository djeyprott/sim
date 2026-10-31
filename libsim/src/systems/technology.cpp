#include "histsim/systems/technology.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "histsim/systems/culture.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kLearnEntity = 0x40000000u;

bool is_forest(Biome b) {
    return b == Biome::BorealForest || b == Biome::TemperateForest || b == Biome::WarmTemperateForest ||
           b == Biome::TropicalRainforest;
}

std::size_t trait_index(const Content& c, const char* id) {
    for (std::size_t t = 0; t < c.traits.size(); ++t) {
        if (c.traits[t].id == id) return t;
    }
    return c.traits.size();
}

}  // namespace

bool technique_place_ok(const World& w, CellId c, TechPlace place) {
    switch (place) {
        case TechPlace::Anywhere: return true;
        case TechPlace::River: return is_river(w, c);
        case TechPlace::Coast: return w.coast_distance[c] == 1;
        case TechPlace::Water: return is_river(w, c) || w.coast_distance[c] == 1;
        case TechPlace::Dry: return w.precipitation_mm[c] < 900.0;
        case TechPlace::Hills: return w.elevation_m[c] > 600.0;
        case TechPlace::Forest: return is_forest(static_cast<Biome>(w.biome[c]));
    }
    return false;
}

Keeps keeps(const World& w, std::uint32_t known) {
    Keeps k;
    const auto& species = w.content->species;
    for (std::size_t s = 0; s < species.size(); ++s) {
        if (!((known >> s) & 1u) || species[s].food_value <= 0.0) continue;
        (species[s].kind == SpeciesKind::Plant ? k.crops : k.herds) = true;
    }
    return k;
}

bool technique_usable(const World& w, const TechniqueDef& t, CellId c, std::uint64_t techs, std::uint32_t known,
                      Keeps kept) {
    if ((techs & t.prerequisites) != t.prerequisites) return false;
    if (t.requires_crops && !kept.crops) return false;
    if (t.requires_herds && !kept.herds) return false;
    if (t.requires_species != 0 && (known & t.requires_species) == 0) return false;
    return technique_place_ok(w, c, t.where);
}

bool technique_usable(const World& w, const TechniqueDef& t, CellId c, std::uint64_t techs, std::uint32_t known) {
    return technique_usable(w, t, c, techs, known, keeps(w, known));
}

TechEffects technique_effects(const World& w, CellId c, std::uint64_t techs) {
    TechEffects e;
    if (techs == 0) return e;
    const auto& all = w.content->techniques;
    for (std::uint64_t bits = techs; bits != 0; bits &= bits - 1) {
        std::size_t t = 0;
        while (!((bits >> t) & 1u)) ++t;  // lowest set bit
        if (t >= all.size() || !technique_place_ok(w, c, all[t].where)) continue;
        const TechEffects& x = all[t].effects;
        e.crop_yield += x.crop_yield;
        e.herd_yield += x.herd_yield;
        e.forage += x.forage;
        e.aquatic += x.aquatic;
        e.harvest_buffer += x.harvest_buffer;
        e.famine += x.famine;
        e.contact += x.contact;
    }
    e.harvest_buffer = std::min(0.8, e.harvest_buffer);
    e.famine = std::max(-0.8, e.famine);
    return e;
}

void TechnologySystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    const std::size_t nt = content.traits.size();
    const auto& techs = content.techniques;
    if (techs.empty()) return;
    if (s.techniques.size() != n) s.techniques.assign(n, 0);
    const std::size_t i_innovation = trait_index(content, "innovation");
    const std::size_t i_openness = trait_index(content, "openness");

    const std::vector<std::uint64_t> before = s.techniques;
    // Contact (canoes, sails, horses, wheels) of each cell, from last year.
    std::vector<double> contact_of(n, 0.0);
    for (CellId c = 0; c < n; ++c) {
        if (before[c] != 0) contact_of[c] = technique_effects(w, c, before[c]).contact;
    }
    for (CellId c = 0; c < n; ++c) {
        const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
        if (first == last) {
            s.techniques[c] = 0;  // knowledge lives with people
            continue;
        }
        // The people's average ways.
        std::array<double, kMaxTraits> ways{};
        double pop = 0.0;
        std::size_t lead = first;
        for (auto i = first; i < last; ++i) {
            const auto p = static_cast<double>(s.cohorts[i].total());
            for (std::size_t t = 0; t < nt; ++t) ways[t] += p * s.cohorts[i].traits[t];
            pop += p;
            if (s.cohorts[i].total() > s.cohorts[lead].total()) lead = i;
        }
        for (std::size_t t = 0; t < nt; ++t) ways[t] /= pop;
        const double innovation = i_innovation < nt ? ways[i_innovation] : 0.5;
        const double openness = i_openness < nt ? ways[i_openness] : 0.5;
        const std::uint32_t known = s.domesticates.empty() ? 0 : s.domesticates[c];
        const Keeps kept = keeps(w, known);
        std::uint64_t& mine = s.techniques[c];
        RngStream rng = ctx.rng(Stream::Technology, c);

        // Invention.
        const double scale = std::sqrt(pop / 10000.0) * (0.5 + innovation);
        for (std::size_t k = 0; k < techs.size(); ++k) {
            const TechniqueDef& t = techs[k];
            if (((mine >> k) & 1u) || t.rate <= 0.0 || !technique_usable(w, t, c, mine, known, kept)) continue;
            double affinity = 1.0;
            for (std::size_t j = 0; j < nt; ++j) affinity += t.affinity[j] * 2.0 * (ways[j] - 0.5);
            affinity = std::max(0.1, affinity);
            if (rng.uniform() >= t.rate * scale * affinity) continue;
            mine |= std::uint64_t{1} << k;
            bool origin = true;
            for (CellId o = 0; o < n && origin; ++o) {
                if (o != c && ((before[o] >> k) & 1u) && w.grid.distance_km(o, c) < kOriginKm) origin = false;
            }
            for (const Invention& prior : s.inventions) {
                if (prior.technique == k && w.grid.distance_km(prior.cell, c) < kOriginKm) origin = false;
            }
            if (origin) {
                const Cohort& co = s.cohorts[lead];
                s.inventions.push_back({ctx.tick, c, static_cast<std::uint32_t>(k), co.culture});
                s.events.push_back({ctx.tick, EventType::Invention, c, static_cast<std::int64_t>(k)});
            }
        }

        // Borrowing from neighbours.
        std::uint64_t offered = 0;
        double contact = contact_of[c];
        for (CellId nb : w.grid.neighbors(c)) {
            if (s.cell_population[nb] == 0 || before[nb] == 0) continue;
            offered |= before[nb];
            contact = std::max(contact, contact_of[nb]);
        }
        offered &= ~mine;
        if (offered != 0) {
            const double area_scale = kReferenceCellAreaKm2 / w.grid.area_km2(c);
            const double p = std::min(1.0, kLearning * (0.5 + openness) * (1.0 + contact) * std::sqrt(area_scale));
            RngStream learn = ctx.rng(Stream::Technology, kLearnEntity | c);
            for (std::size_t k = 0; k < techs.size(); ++k) {
                if (!((offered >> k) & 1u) || !technique_usable(w, techs[k], c, mine, known, kept)) continue;
                if (learn.uniform32() < p) mine |= std::uint64_t{1} << k;
            }
        }

        // Forgetting.
        for (std::size_t k = 0; k < techs.size(); ++k) {
            if (!((mine >> k) & 1u)) continue;
            double loss = pop < kForgetBelowPeople ? kForgetting : 0.0;
            if (!technique_usable(w, techs[k], c, mine & ~(std::uint64_t{1} << k), known, kept)) loss += kFading;
            if (loss > 0.0 && rng.uniform32() < loss) mine &= ~(std::uint64_t{1} << k);
        }
    }
}

}  // namespace histsim
