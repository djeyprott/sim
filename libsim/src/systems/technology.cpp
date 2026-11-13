#include "histsim/systems/technology.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

#include "histsim/figures.hpp"
#include "histsim/parallel.hpp"
#include "histsim/systems/culture.hpp"
#include "histsim/systems/polity.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kLearnEntity = 0x40000000u;
constexpr std::uint32_t kInventorEntity = 0x40000000u;  // Stream::Figures

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

double use_of(const TechniqueDef& t, const std::array<double, kPracticeCount>& doing) {
    double use = 0.0;
    for (std::size_t a = 0; a < kPracticeCount; ++a) use += t.practice[a] * doing[a];
    return use;
}

double affinity_of(const TechniqueDef& t, const std::array<double, kMaxTraits>& ways) {
    double affinity = 1.0;
    for (std::size_t j = 0; j < kMaxTraits; ++j) affinity += t.affinity[j] * 2.0 * (ways[j] - 0.5);
    return std::max(0.1, affinity);
}

}  // namespace

bool technique_place_ok(const World& w, CellId c, TechPlace place) {
    switch (place) {
        case TechPlace::Anywhere: return true;
        case TechPlace::River: return river_share(w, c) >= 0.5;
        case TechPlace::Coast: return coast_share(w, c) >= 0.5;
        case TechPlace::Water: return river_share(w, c) >= 0.5 || coast_share(w, c) >= 0.5;
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
    if ((kept.goods & t.requires_goods) != t.requires_goods) return false;
    if (!w.technique_places.empty()) {
        const auto k = static_cast<std::size_t>(&t - w.content->techniques.data());
        if (k < 64) return (w.technique_places[c] >> k) & 1u;
    }
    return technique_place_ok(w, c, t.where);
}

bool technique_usable(const World& w, const TechniqueDef& t, CellId c, std::uint64_t techs, std::uint32_t known) {
    return technique_usable(w, t, c, techs, known, keeps(w, known));
}

TechEffects technique_effects(const World& w, CellId c, std::uint64_t techs) {
    TechEffects e;
    if (techs == 0) return e;
    const auto& all = w.content->techniques;
    const bool placed = !w.technique_places.empty();
    if (placed) techs &= w.technique_places[c];
    for (std::uint64_t bits = techs; bits != 0; bits &= bits - 1) {
        const auto t = static_cast<std::size_t>(std::countr_zero(bits));  // lowest set bit
        if (t >= all.size() || (!placed && !technique_place_ok(w, c, all[t].where))) continue;
        const TechEffects& x = all[t].effects;
        e.crop_yield += x.crop_yield;
        e.herd_yield += x.herd_yield;
        e.forage += x.forage;
        e.aquatic += x.aquatic;
        e.harvest_buffer += x.harvest_buffer;
        e.famine += x.famine;
        e.contact += x.contact;
        e.administration += x.administration;
        e.land_transport += x.land_transport;
        e.water_transport += x.water_transport;
        e.military += x.military;
    }
    e.harvest_buffer = std::min(0.8, e.harvest_buffer);
    e.famine = std::max(-0.8, e.famine);
    return e;
}

std::array<double, kPracticeCount> practices(const World& w, const SimState& s, CellId c, const PracticeInputs& in) {
    std::array<double, kPracticeCount> d{};
    const double people = s.cell_population[c];
    if (people <= 0.0) return d;
    const auto at = [](const std::vector<double>& v, CellId i) { return i < v.size() ? v[i] : 0.0; };
    const double farming = at(s.env.farming, c), herds = at(s.env.pastoral, c);
    const auto set = [&](TechPractice p, double x) { d[static_cast<std::size_t>(p)] = x; };
    set(TechPractice::Foraging, 1.0 - farming);
    set(TechPractice::Hunting, (1.0 - farming) * (s.env.game.size() > c ? s.env.game[c] : 1.0));
    set(TechPractice::Fishing, technique_place_ok(w, c, TechPlace::Water) ? 1.0 - 0.5 * farming : 0.0);
    set(TechPractice::Farming, farming * (1.0 - herds));
    set(TechPractice::Herding, farming * herds);
    const double capacity = at(s.env.capacity, c);
    set(TechPractice::Crowding, capacity > 0.0 ? std::min(TechnologySystem::kMaxCrowding, people / capacity) : 0.0);
    set(TechPractice::Storing, std::min(TechnologySystem::kMaxPractice, at(s.env.food_store, c) / people));
    const double around = std::max(1.0, in.community);
    set(TechPractice::Towns, std::min(TechnologySystem::kMaxPractice, in.town / TechnologySystem::kTownPeople));
    set(TechPractice::Trade, std::min(TechnologySystem::kMaxPractice, in.trade / (around * TechnologySystem::kTradePerPerson)));
    // Surplus passing through the hands of chiefs, priests and kings: the
    // share of people fed in towns and the food brought in, kept account of
    // where someone rules, most at a realm's seat (and its temple).
    const double handled = in.town / around / TechnologySystem::kTownShare + in.imported / (around * TechnologySystem::kImportPerPerson);
    const double ruled = in.seat == 2 ? TechnologySystem::kSeatTemple
                         : in.seat == 1 ? 1.0
                         : in.ruled ? TechnologySystem::kRuled
                                    : TechnologySystem::kUnruled;
    set(TechPractice::Redistribution, std::min(TechnologySystem::kMaxPractice, handled * ruled));
    // Ruling a realm, measured in level-5 cells' worth of land, most at its seat.
    set(TechPractice::Rule, in.ruled ? std::min(TechnologySystem::kMaxPractice, in.realm_cells / TechnologySystem::kRealmCells) *
                                           (in.seat != 0 ? 1.0 : TechnologySystem::kRuled)
                                     : 0.0);
    return d;
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
    const std::size_t nk = techs.size();
    if (s.know_how.size() != n * nk) s.know_how.assign(n * nk, 0.0f);
    // Contact (canoes, sails, horses, wheels) of each cell, from last year.
    std::vector<double> contact_of(n, 0.0);
    for (CellId c = 0; c < n; ++c) {
        if (before[c] != 0) contact_of[c] = technique_effects(w, c, before[c]).contact;
    }
    // Towns, populations, trade and food brought in are counted over each
    // cell's hinterland (the cell itself at level 5), so practice is the same
    // at any grid level.
    std::vector<double> around_people, around_town;
    hinterland_people(w, s, around_people, around_town);
    std::vector<double> around_trade(n, 0.0), around_import(n, 0.0);
    if (s.trade_value.size() == n && s.food_import.size() == n) {
        parallel_for(n, [&](std::size_t c_begin, std::size_t c_end) {
            for (std::size_t c = c_begin; c < c_end; ++c) {
                if (s.cell_population[c] == 0) continue;
                double trade = 0.0, imported = 0.0;
                w.for_hinterland(static_cast<CellId>(c), [&](CellId h) {
                    trade += s.trade_value[h];
                    imported += std::max(0.0, s.food_import[h]);
                });
                around_trade[c] = trade * w.hinterland_weight();
                around_import[c] = imported * w.hinterland_weight();
            }
        }, 1024);
    }
    // Realms: their size in level-5 cells' worth of land, and their seats
    // (the capital's hinterland as far as the realm holds it), where the
    // ruler's household and its temple keep accounts.
    const std::size_t np = s.polities.size();
    std::vector<double> realm(np, 0.0);
    std::vector<std::uint8_t> seat(n, 0);  // 1 seat, 2 seat with a temple priesthood
    if (s.polity_of.size() == n) {
        const double cell5 = w.per_area(1.0);
        for (CellId c = 0; c < n; ++c) {
            if (s.polity_of[c] != 0) realm[s.polity_of[c]] += cell5;
        }
        for (std::size_t p = 1; p < np; ++p) {
            const Polity& pol = s.polities[p];
            if (pol.ended != kNever || pol.capital >= n || s.polity_of[pol.capital] != p) continue;
            const std::uint8_t mark = has_institution(pol.institutions, Institution::Temple) ? 2 : 1;
            w.for_hinterland(pol.capital, [&](CellId h) {
                if (s.polity_of[h] == p) seat[h] = mark;
            });
        }
    }
    // Each cell in parallel; new inventions are gathered and recorded in cell
    // order afterwards.
    const std::size_t inventions_before = s.inventions.size();
    const auto found = parallel_gather<std::vector<Invention>>(n, [&](std::size_t c_begin, std::size_t c_end, std::vector<Invention>& out) {
    for (CellId c = static_cast<CellId>(c_begin); c < c_end; ++c) {
        const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
        float* const know = s.know_how.data() + static_cast<std::size_t>(c) * nk;
        if (first == last) {
            s.techniques[c] = 0;  // knowledge lives with people
            std::fill(know, know + nk, 0.0f);
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
        Keeps kept = keeps(w, known);
        const double community = around_people[c];
        kept.goods = goods_in(s, c);
        std::uint64_t& mine = s.techniques[c];
        RngStream rng = ctx.rng(Stream::Technology, c);

        // What the people do (see technology.hpp).
        const std::array<double, kPracticeCount> doing = practices(w, s, c, PracticeInputs{
            community, around_town[c], around_trade[c], around_import[c],
            s.polity_of.size() == n && s.polity_of[c] != 0 ? realm[s.polity_of[c]] : 0.0,
            s.polity_of.size() == n && s.polity_of[c] != 0, seat[c]});

        // Neighbours who know techniques, and the best contact among them.
        std::uint64_t offered = 0;
        double contact = contact_of[c];
        for (CellId nb : w.grid.neighbors(c)) {
            if (s.cell_population[nb] == 0 || before[nb] == 0) continue;
            offered |= before[nb];
            contact = std::max(contact, contact_of[nb]);
        }
        const double learning = kLearning * (0.5 + openness) * (1.0 + contact) * std::sqrt(kReferenceCellAreaKm2 / w.grid.area_km2(c));
        const double people = std::min(kMaxPeopleFactor, std::sqrt(community / 10000.0)) * (0.5 + innovation);
        const double memory = community < kForgetBelowPeople ? kMemoryYearsSmall : kMemoryYears;

        // Know-how: built by practice (more in larger, more inventive
        // communities whose ways suit it) and by seeing neighbours use it;
        // fading when neither goes on. At 1 the people know the technique:
        // invented if no neighbour taught it.
        for (std::size_t k = 0; k < nk; ++k) {
            if ((mine >> k) & 1u) {
                know[k] = 0.0f;
                continue;
            }
            const TechniqueDef& t = techs[k];
            if (!technique_usable(w, t, c, mine, known, kept)) {
                know[k] = static_cast<float>(know[k] * (1.0 - 1.0 / kMemoryYearsSmall));
                continue;
            }
            const double use = use_of(t, doing);
            double gain = use * people * affinity_of(t, ways) / std::max(1.0, t.practice_years);
            const bool taught = ((offered >> k) & 1u) != 0;
            if (taught) gain += learning * std::min(1.0, use);
            const double k_now = know[k] + gain - know[k] / memory;
            if (k_now < 1.0) {
                know[k] = static_cast<float>(std::max(0.0, k_now));
                continue;
            }
            know[k] = 0.0f;
            mine |= std::uint64_t{1} << k;
            if (taught) continue;
            bool origin = true;
            for (CellId o = 0; o < n && origin; ++o) {
                if (o != c && ((before[o] >> k) & 1u) && w.grid.distance_km(o, c) < kOriginKm) origin = false;
            }
            for (std::size_t i = 0; i < inventions_before && origin; ++i) {
                const Invention& prior = s.inventions[i];
                if (prior.technique == k && w.grid.distance_km(prior.cell, c) < kOriginKm) origin = false;
            }
            if (origin) out.push_back({ctx.tick, c, static_cast<std::uint32_t>(k), s.cohorts[lead].culture});
        }

        // Forgetting.
        for (std::size_t k = 0; k < techs.size(); ++k) {
            if (!((mine >> k) & 1u)) continue;
            double loss = community < kForgetBelowPeople ? kForgetting : 0.0;
            if (!technique_usable(w, techs[k], c, mine & ~(std::uint64_t{1} << k), known, kept)) loss += kFading;
            // Out of use: what they still do would keep less than kKeep of
            // the know-how it takes (scribes with no accounts to keep).
            const TechniqueDef& t = techs[k];
            if (t.practice_years > 0.0 &&
                use_of(t, doing) * people * affinity_of(t, ways) * memory / t.practice_years < kKeep) {
                loss += kDisuse;
            }
            if (loss > 0.0 && rng.uniform32() < loss) mine &= ~(std::uint64_t{1} << k);
        }
    }
    }, 32);
    // An origin unless one found nearer earlier this year.
    for (const auto& part : found) {
        for (const Invention& inv : part) {
            bool origin = true;
            for (std::size_t i = inventions_before; i < s.inventions.size() && origin; ++i) {
                const Invention& prior = s.inventions[i];
                if (prior.technique == inv.technique && w.grid.distance_km(prior.cell, inv.cell) < kOriginKm) origin = false;
            }
            if (!origin) continue;
            // The inventor, recorded by name (own random stream).
            RngStream fr = ctx.rng(Stream::Figures, kInventorEntity | static_cast<std::uint32_t>(s.inventions.size()));
            Invention named = inv;
            named.figure = record_person(s, content, inv.cell, ctx.tick, 20, 55, kRoleInventor, fr);
            s.inventions.push_back(named);
            s.events.push_back({ctx.tick, EventType::Invention, inv.cell, static_cast<std::int64_t>(inv.technique)});
        }
    }
}

}  // namespace histsim
