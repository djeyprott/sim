#include "histsim/systems/farming.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "histsim/systems/climate.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/systems/technology.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kLearningEntity = 0x40000000u;

double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

// Index of a trait in the content pack, or the pack's size if absent.
std::size_t trait_index(const Content& c, const char* id) {
    for (std::size_t t = 0; t < c.traits.size(); ++t) {
        if (c.traits[t].id == id) return t;
    }
    return c.traits.size();
}

}  // namespace

double farm_density(const World& w, CellId c, std::uint32_t known, std::uint64_t techniques) {
    const FarmParts p = farm_parts(w, c, known, techniques);
    // Herds add to crops (milk, manure, meat), or carry pastoralists alone.
    return p.crops > 0.0 ? p.crops + 0.5 * p.herds : p.herds;
}

FarmParts farm_parts(const World& w, CellId c, std::uint32_t known, std::uint64_t techniques) {
    if (!w.is_land[c] || known == 0) return {};
    const auto& species = w.content->species;
    double crop = 0.0, herd = 0.0;
    for (std::size_t s = 0; s < species.size(); ++s) {
        if (!((known >> s) & 1u)) continue;
        const double v = w.cultivated(c, s) * species[s].food_value;
        if (v < FarmingSystem::kMinSuitability) continue;
        double& best = species[s].kind == SpeciesKind::Plant ? crop : herd;
        best = std::max(best, v);
    }
    const double soil = 0.35 + 0.65 * w.soil_fertility[c];
    // Floodplains: the share of the land along a river (at level 5, river
    // cells), by the river's flow.
    const double along = river_share(w, c);
    const double flow = along > 0.0 ? (c < w.river_flow.size() ? w.river_flow[c] : w.discharge_m3s[c]) : 0.0;
    const double flood = 1.0 + FarmingSystem::kFloodplainBonus * (along * std::min(1.0, flow / 5000.0));
    const double season = std::min(1.0, (w.growing_months[c] + 1.0) / 5.0);
    // A river waters its floodplain; elsewhere crops need reliable rain.
    const double rain = along * 1.0 + (1.0 - along) * std::clamp((w.precipitation_mm[c] - FarmingSystem::kDryFarmingMm) /
                                                                     FarmingSystem::kDryFarmingSpanMm,
                                                                 FarmingSystem::kDryFloor, 1.0);
    const auto biome = static_cast<Biome>(w.biome[c]);
    const double fallow_rate = biome == Biome::TropicalRainforest ? FarmingSystem::kRainforestFallow
                               : biome == Biome::TropicalSavanna  ? FarmingSystem::kSavannaFallow
                                                                  : 1.0;
    const double floodplain = along * std::min(1.0, flow / 5000.0);  // big rivers' floodplains only
    const double fallow = floodplain + (1.0 - floodplain) * fallow_rate;
    const TechEffects tech = technique_effects(w, c, techniques);
    const double crops =
        FarmingSystem::kCropDensity * crop * soil * flood * season * rain * fallow * (1.0 + tech.crop_yield);
    const double herds = FarmingSystem::kHerdDensity * herd * pasture_quality(static_cast<Biome>(w.biome[c])) *
                         (1.0 + tech.herd_yield);
    return {crops, herds};
}

double pasture_quality(Biome b) {
    switch (b) {
        case Biome::TemperateGrassland: return 1.6;
        case Biome::DryShrubland: return 1.3;
        case Biome::TropicalSavanna: return 1.2;
        case Biome::Mediterranean: return 1.0;
        case Biome::Tundra: return 0.8;  // reindeer
        case Biome::Desert: return 0.6;
        case Biome::TemperateForest:
        case Biome::WarmTemperateForest:
        case Biome::BorealForest: return 0.7;
        case Biome::TropicalRainforest: return 0.4;
        case Biome::Ocean:
        case Biome::IceSheet:
        case Biome::Count: return 0.0;
    }
    return 0.0;
}

double harvest_factor(const World& w, const SimState& s, CellId c) {
    if (!w.is_land[c]) return 0.0;
    const double rain = std::clamp(s.env.precipitation_mm[c] / std::max(1.0, w.precipitation_mm[c]), 0.4, 1.15);
    const double warmth = std::max(0.3, 1.0 - 0.12 * std::fabs(s.env.temperature_c[c] - w.temperature_c[c]));
    const double f = rain * warmth;
    if (f >= 1.0) return f;
    const double buffer = technique_effects(w, c, techniques_in(s, c)).harvest_buffer;
    return 1.0 - (1.0 - f) * (1.0 - buffer);
}

void FarmingSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    const std::size_t nt = content.traits.size();
    if (s.domesticates.size() != n) s.domesticates.assign(n, 0);
    if (s.domestication.size() != w.wild_stands.size()) s.domestication.assign(w.wild_stands.size(), 0.0);
    if (s.env.farming.size() != n) s.env.farming.assign(n, 0.0);
    if (s.env.farm_capacity.size() != n) s.env.farm_capacity.assign(n, 0.0);

    const std::size_t i_plants = trait_index(content, "plant_reliance");
    const std::size_t i_storage = trait_index(content, "storage");
    const std::size_t i_mobility = trait_index(content, "mobility");
    const std::size_t i_innovation = trait_index(content, "innovation");
    const std::size_t i_openness = trait_index(content, "openness");

    // ---- Per inhabited cell: people's average ways, and who leads it.
    std::vector<std::array<double, kMaxTraits>> ways(n);
    std::vector<std::size_t> main_cohort(n, 0);
    for (CellId c = 0; c < n; ++c) {
        const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
        if (first == last) continue;
        double total = 0.0;
        std::int64_t biggest = -1;
        for (auto i = first; i < last; ++i) {
            const Cohort& co = s.cohorts[i];
            const auto p = static_cast<double>(co.total());
            for (std::size_t t = 0; t < nt; ++t) ways[c][t] += p * co.traits[t];
            total += p;
            if (co.total() > biggest) biggest = co.total(), main_cohort[c] = i;
        }
        if (total > 0.0) {
            for (std::size_t t = 0; t < nt; ++t) ways[c][t] /= total;
        }
    }
    auto trait = [&](CellId c, std::size_t t) { return t < nt ? ways[c][t] : 0.5; };

    // Foraging capacity of each inhabited cell this year (used many times).
    std::vector<double> forage_of(n, 0.0);
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] > 0) forage_of[c] = forage_capacity(w, s, c);
    }

    // ---- Domestication where species grow wild.
    for (std::size_t k = 0; k < w.wild_stands.size(); ++k) {
        const auto [c, sp] = w.wild_stands[k];
        double& progress = s.domestication[k];
        if ((s.domesticates[c] >> sp) & 1u) continue;
        double gain = 0.0;
        if (s.cell_population[c] > 0) {
            const SpeciesDef& def = content.species[sp];
            const double plants = trait(c, i_plants);
            const double lean = def.kind == SpeciesKind::Plant ? plants : 0.6 * (1.0 - 0.5 * plants);
            const double engagement = lean * (0.5 + trait(c, i_storage)) * (1.2 - trait(c, i_mobility)) *
                                      (0.5 + trait(c, i_innovation));
            const double forage = forage_of[c];
            const double strain = forage > 0.0 ? static_cast<double>(s.cell_population[c]) / forage : 1.0;
            const double pressure = std::clamp(0.3 + strain, 0.3, 1.3);
            gain = kDomesticationRate * w.abundance(c, sp) * def.domesticability * engagement * pressure;
        }
        progress = std::max(0.0, progress + gain - kForgetting * progress);
        if (progress >= 1.0) {
            progress = 1.0;
            // A new origin, or the same regional process as one nearby?
            bool independent = true;
            for (CellId o = 0; o < n && independent; ++o) {
                if (((s.domesticates[o] >> sp) & 1u) && w.grid.distance_km(o, c) < kIndependentOriginKm) {
                    independent = false;
                }
            }
            s.domesticates[c] |= 1u << sp;
            if (independent) {
                const Cohort& lead = s.cohorts[main_cohort[c]];
                s.domestications.push_back({ctx.tick, c, sp, lead.culture, lead.language});
                s.events.push_back({ctx.tick, EventType::Domestication, c, static_cast<std::int64_t>(sp)});
            }
        }
    }

    // ---- Learning from neighbours; knowledge lives with people. Foragers
    // often knew of their neighbours' crops long before taking them up.
    const std::vector<std::uint32_t> known = s.domesticates;
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) {
            s.domesticates[c] = 0;
            continue;
        }
        std::uint32_t offered = 0;
        for (CellId nb : w.grid.neighbors(c)) {
            if (s.cell_population[nb] > 0) offered |= known[nb];
        }
        offered &= ~known[c];
        if (offered == 0) continue;
        const double area_scale = kReferenceCellAreaKm2 / w.grid.area_km2(c);
        const double contact = technique_effects(w, c, techniques_in(s, c)).contact;
        const double p =
            std::min(1.0, kLearning * (0.5 + trait(c, i_openness)) * (1.0 + contact) * std::sqrt(area_scale));
        RngStream rng = ctx.rng(Stream::Farming, kLearningEntity | c);
        for (std::size_t sp = 0; sp < content.species.size(); ++sp) {
            if (!((offered >> sp) & 1u)) continue;
            if (w.cultivated(c, sp) < kMinSuitability) continue;  // cannot be raised here
            const double herding = s.env.pastoral.size() == n && s.env.farming.size() == n
                                       ? s.env.pastoral[c] * s.env.farming[c]
                                       : 0.0;
            const double herd = content.species[sp].kind == SpeciesKind::Animal
                                    ? 1.0 + kHerdMobility * trait(c, i_mobility) * herding
                                    : 1.0;
            if (rng.uniform32() < std::min(1.0, p * herd)) s.domesticates[c] |= 1u << sp;
        }
    }

    // ---- What farmland would feed this year.
    if (s.env.pastoral.size() != n) s.env.pastoral.assign(n, 0.0);
    for (CellId c = 0; c < n; ++c) {
        if (s.domesticates[c] == 0) {
            s.env.farm_capacity[c] = 0.0;
            s.env.pastoral[c] = 0.0;
            continue;
        }
        const FarmParts parts = farm_parts(w, c, s.domesticates[c], techniques_in(s, c));
        const double density = parts.crops > 0.0 ? parts.crops + 0.5 * parts.herds : parts.herds;
        s.env.farm_capacity[c] = density * w.grid.area_km2(c) * harvest_factor(w, s, c);
        s.env.pastoral[c] = pastoral_share(parts);
    }

    // ---- Each cohort's share of food from farming.
    for (std::size_t i = 0; i < s.cohorts.size(); ++i) {
        Cohort& co = s.cohorts[i];
        const CellId c = co.cell;
        const double forage = forage_of[c];
        const double farm = s.env.farm_capacity[c];
        if (farm <= 0.0) {
            co.farming = 0.0;  // nothing here to farm: back to foraging at once
            continue;
        }
        const double advantage = forage > 0.0 ? farm / forage : 10.0;
        if (advantage < 1.0) {
            // Farming yields less than foraging would: fields are abandoned.
            co.farming = std::max(0.0, co.farming - kAbandonment * (1.0 - advantage) * co.farming);
            continue;
        }
        // Strain: how far beyond foraging the people are; farming spreads
        // as foraging alone stops being enough.
        const double strain = forage > 0.0 ? static_cast<double>(s.cell_population[c]) / forage : 2.0;
        const double need = std::clamp(strain - 0.6, 0.0, 1.5);
        const double innovation = i_innovation < nt ? co.traits[i_innovation] : 0.5;
        const double mobility = i_mobility < nt ? co.traits[i_mobility] : 0.5;
        const double willing = (0.3 + innovation) * (1.2 - mobility);
        const double gain = kAdoption * willing * (need + 0.05) * (1.0 - co.farming);
        co.farming = clamp01(co.farming + gain);
    }

    // ---- The cell's farming share and capacity.
    for (CellId c = 0; c < n; ++c) {
        const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
        double total = 0.0, farmed = 0.0;
        for (auto i = first; i < last; ++i) {
            const auto p = static_cast<double>(s.cohorts[i].total());
            total += p;
            farmed += p * s.cohorts[i].farming;
        }
        s.env.farming[c] = total > 0.0 ? farmed / total : 0.0;
        s.env.capacity[c] = environment_capacity(w, s, c);
    }
}

}  // namespace histsim
