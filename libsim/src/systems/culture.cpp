#include "histsim/systems/culture.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "histsim/language.hpp"

namespace histsim {

namespace {

// Random-stream entities beyond any cohort index, for per-language and
// per-culture draws.
constexpr std::uint32_t kNewLanguageEntity = 0x80000000u;
constexpr std::uint32_t kNewCultureEntity = 0xC0000000u;

double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

// Uniform noise with standard deviation sd: arithmetic only, cheaper than a
// normal draw, and over many years the sum is normal anyway.
double noise(RngStream& rng, double sd) {
    constexpr double kSqrt12 = 3.4641016151377544;  // width of a unit-variance uniform
    return (rng.uniform32() - 0.5) * kSqrt12 * sd;
}

bool language_name_taken(const SimState& s, const std::string& name) {
    return std::any_of(s.languages.begin(), s.languages.end(), [&](const Language& l) { return l.name == name; });
}
bool culture_name_taken(const SimState& s, const std::string& name) {
    return std::any_of(s.cultures.begin(), s.cultures.end(), [&](const Culture& c) { return c.name == name; });
}

// A name not used by any language before (with `prefix`, e.g. "Proto-"):
// like real languages, it is the word for "people", or for its land, river or
// mountains, or else a new word in the language.
std::string unique_language_name(const Content& c, const SimState& s, const Language& lang, RngStream& rng,
                                 const std::string& prefix = {}) {
    std::vector<std::size_t> concepts = {c.people_concept, c.land_concept, c.river_concept};
    for (std::size_t k = 0; k < c.concepts.size(); ++k) {
        if (c.concepts[k].id == "mountain" || c.concepts[k].id == "we") concepts.push_back(k);
    }
    for (std::size_t k : concepts) {
        const std::string name = prefix + romanize(c, lang.words[k].sounds, true);
        if (!language_name_taken(s, name)) return name;
    }
    const std::uint64_t inv = inventory(lang);
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::string name = prefix + romanize(c, coin_word(c, inv, lang.syllables, 2, 3, rng), true);
        if (!language_name_taken(s, name)) return name;
    }
    const std::string people = prefix + romanize(c, lang.words[c.people_concept].sounds, true);
    for (int i = 2;; ++i) {
        const std::string name = people + " " + std::to_string(i);
        if (!language_name_taken(s, name)) return name;
    }
}

// A culture name coined in its main language.
std::string coin_culture_name(const Content& c, const SimState& s, const Language& lang, RngStream& rng) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::string name = romanize(c, coin_word(c, inventory(lang), lang.syllables, 2, 3, rng), true);
        if (!culture_name_taken(s, name)) return name;
    }
    for (int i = 2;; ++i) {
        const std::string name = lang.name + " " + std::to_string(i);
        if (!culture_name_taken(s, name)) return name;
    }
}

void ensure_placeholders(SimState& s) {
    if (s.languages.empty()) s.languages.emplace_back();
    if (s.cultures.empty()) s.cultures.emplace_back();
}

double dist2(const double* a, const double* b, std::size_t n) {
    double d = 0.0;
    for (std::size_t i = 0; i < n; ++i) d += (a[i] - b[i]) * (a[i] - b[i]);
    return d;
}

// Split check for one language or culture. `members` are cohort indices,
// `point` gives a cohort's point and its dimension. Returns the cohorts that
// should form the new group (empty if none).
template <typename Point>
std::vector<std::uint32_t> find_split(const SimState& s, const std::vector<std::uint32_t>& members, std::size_t dims,
                                      double threshold, Point&& point) {
    if (members.size() < 2) return {};
    std::array<double, 16> mean{};
    double total = 0.0;
    for (std::uint32_t i : members) {
        const double w = static_cast<double>(s.cohorts[i].total());
        const double* x = point(i);
        for (std::size_t d = 0; d < dims; ++d) mean[d] += w * x[d];
        total += w;
    }
    if (total <= 0.0) return {};
    for (std::size_t d = 0; d < dims; ++d) mean[d] /= total;
    std::uint32_t far = members[0];
    double far_d2 = -1.0;
    for (std::uint32_t i : members) {
        const double d2 = dist2(point(i), mean.data(), dims);
        if (d2 > far_d2) far = i, far_d2 = d2;
    }
    if (far_d2 <= threshold * threshold) return {};
    // The outliers: everyone closer to the farthest group than to the average.
    std::vector<std::uint32_t> group;
    std::int64_t group_pop = 0;
    for (std::uint32_t i : members) {
        if (dist2(point(i), point(far), dims) < dist2(point(i), mean.data(), dims)) {
            group.push_back(i);
            group_pop += s.cohorts[i].total();
        }
    }
    const auto rest_pop = static_cast<std::int64_t>(total) - group_pop;
    if (group.size() == members.size() || group_pop < CultureSystem::kMinSplitPopulation ||
        rest_pop < CultureSystem::kMinSplitPopulation) {
        return {};
    }
    return group;
}

}  // namespace

std::array<double, kCultureVarCount> culture_variables(const World& w, const SimState& s, CellId c) {
    std::array<double, kCultureVarCount> v{};
    const double k = s.env.capacity[c];
    const double pop = static_cast<double>(s.cell_population[c]);
    v[static_cast<int>(CultureVar::Crowding)] = k > 0.0 ? clamp01(pop / k) : (pop > 0.0 ? 1.0 : 0.0);
    v[static_cast<int>(CultureVar::Aquatic)] = clamp01(w.aquatic_bonus[c] / 0.8);
    v[static_cast<int>(CultureVar::Seasonality)] = clamp01(w.temperature_range_c[c] / 40.0);
    v[static_cast<int>(CultureVar::GrowingSeason)] = clamp01(w.growing_months[c] / 12.0);
    v[static_cast<int>(CultureVar::Game)] = clamp01(s.env.game[c]);
    int plants = 0;
    const auto& species = w.content->species;
    for (std::size_t i = 0; i < species.size(); ++i) {
        if (species[i].kind == SpeciesKind::Plant && ((w.species_mask[c] >> i) & 1u)) ++plants;
    }
    v[static_cast<int>(CultureVar::WildPlants)] = clamp01(plants / 4.0);
    return v;
}

double trait_target(const CultureTrait& t, const std::array<double, kCultureVarCount>& vars) {
    double x = t.base;
    for (std::size_t i = 0; i < kCultureVarCount; ++i) x += t.push[i] * vars[i];
    return clamp01(x);
}

namespace {

// Builds a family's prehistory: a tree of languages over the given regions.
struct FamilyBuilder {
    const World& w;
    const Content& c;
    SimState& s;
    RngStream& rng;
    std::uint32_t family = 0;
    std::vector<Vec3> region_center;              // per region
    std::vector<std::uint32_t> region_language;   // result: per region

    // `lang` is spoken (as one language) by `regions` from `years_ago`.
    // Returns its id once it is in the state.
    void grow(Language lang, std::vector<std::size_t> regions, double years_ago, std::uint32_t parent) {
        lang.parent = parent;
        lang.family = family;
        lang.founded = -static_cast<Tick>(years_ago * kTicksPerYear);
        if (regions.size() == 1) {
            // A living language: change until the present.
            const auto years = static_cast<int>(years_ago);
            for (int y = 0; y < years; ++y) evolve_language(c, lang, rng, s.next_cognate);
            lang.id = static_cast<std::uint32_t>(s.languages.size());
            lang.name = unique_language_name(c, s, lang, rng);
            region_language[regions[0]] = lang.id;
            s.languages.push_back(std::move(lang));
            return;
        }
        // An ancestor: it changes for a while, then splits in two along the
        // longest axis of its land.
        const double span = years_ago * (0.25 + 0.4 * rng.uniform());
        const auto years = static_cast<int>(span);
        for (int y = 0; y < years; ++y) evolve_language(c, lang, rng, s.next_cognate);
        const double split_ago = years_ago - static_cast<double>(years);
        lang.extinct = -static_cast<Tick>(split_ago * kTicksPerYear);
        lang.id = static_cast<std::uint32_t>(s.languages.size());
        lang.name = unique_language_name(c, s, lang, rng, "Proto-");
        const std::uint32_t id = lang.id;
        s.languages.push_back(lang);

        // The two regions farthest apart seed the halves.
        std::size_t a = regions[0], b = regions[1];
        double best = -1.0;
        for (std::size_t i = 0; i < regions.size(); ++i) {
            for (std::size_t j = i + 1; j < regions.size(); ++j) {
                const Vec3& p = region_center[regions[i]];
                const Vec3& q = region_center[regions[j]];
                const double d = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
                if (d > best) best = d, a = regions[i], b = regions[j];
            }
        }
        std::vector<std::size_t> left, right;
        for (std::size_t r : regions) {
            const Vec3& p = region_center[r];
            const Vec3& pa = region_center[a];
            const Vec3& pb = region_center[b];
            const double da = (p.x - pa.x) * (p.x - pa.x) + (p.y - pa.y) * (p.y - pa.y) + (p.z - pa.z) * (p.z - pa.z);
            const double db = (p.x - pb.x) * (p.x - pb.x) + (p.y - pb.y) * (p.y - pb.y) + (p.z - pb.z) * (p.z - pb.z);
            (da <= db ? left : right).push_back(r);
        }
        if (right.empty()) {  // only if regions coincide: split the list instead
            right.push_back(left.back());
            left.pop_back();
        }
        Language child = lang;
        child.extinct = kNever;
        child.sound_changes.clear();
        grow(child, std::move(left), split_ago, id);
        grow(child, std::move(right), split_ago, id);
    }
};

}  // namespace

FoundedPeople found_people(const World& w, SimState& s, std::uint64_t seed, std::uint32_t entity,
                           std::span<const CellId> cells, double history_years) {
    const Content& c = *w.content;
    ensure_placeholders(s);
    FoundedPeople out;
    out.cell_language.assign(cells.size(), 0);

    // ---- The culture: what the land suggests, averaged over the people's
    // cells, plus a character of its own.
    RngStream crng(seed, s.tick, Stream::Culture, kNewCultureEntity | entity);
    for (std::size_t t = 0; t < c.traits.size(); ++t) {
        double sum = 0.0;
        for (CellId cell : cells) sum += trait_target(c.traits[t], culture_variables(w, s, cell));
        const double mean = cells.empty() ? c.traits[t].base : sum / static_cast<double>(cells.size());
        out.traits[t] = clamp01(mean + (crng.uniform() - 0.5) * 0.3);
    }

    // ---- The language family.
    RngStream lrng(seed, s.tick, Stream::Language, kNewLanguageEntity | entity);
    Language proto = make_proto_language(c, lrng, s.next_cognate);
    double area = 0.0;
    for (CellId cell : cells) area += w.grid.area_km2(cell);
    const auto regions = history_years > 0.0 && !cells.empty()
                             ? std::max<std::size_t>(1, static_cast<std::size_t>(area / kInitialAreaPerLanguageKm2 + 0.5))
                             : 1;
    out.family = static_cast<std::uint32_t>(s.languages.size());
    if (regions == 1) {
        proto.id = out.family;
        proto.family = out.family;
        proto.founded = -static_cast<Tick>(history_years * kTicksPerYear);
        for (int y = 0; y < static_cast<int>(history_years); ++y) evolve_language(c, proto, lrng, s.next_cognate);
        proto.sound_changes.clear();
        proto.name = unique_language_name(c, s, proto, lrng);
        s.languages.push_back(std::move(proto));
        out.cell_language.assign(cells.size(), out.family);
    } else {
        // Regions: grown at once from seed cells through the people's land
        // (it is contiguous: starting groups are grown the same way).
        const std::size_t n = w.cell_count();
        std::vector<std::int32_t> region_of(n, -1);
        std::vector<std::uint8_t> mine(n, 0);
        for (CellId cell : cells) mine[cell] = 1;
        std::deque<CellId> queue;
        std::size_t seeded = 0;
        for (std::size_t attempt = 0; seeded < regions && attempt < 20 * regions; ++attempt) {
            const CellId cell = cells[lrng.below(static_cast<std::uint32_t>(cells.size()))];
            if (region_of[cell] >= 0) continue;
            region_of[cell] = static_cast<std::int32_t>(seeded++);
            queue.push_back(cell);
        }
        while (!queue.empty()) {
            const CellId cell = queue.front();
            queue.pop_front();
            for (CellId nb : w.grid.neighbors(cell)) {
                if (mine[nb] && region_of[nb] < 0) {
                    region_of[nb] = region_of[cell];
                    queue.push_back(nb);
                }
            }
        }
        FamilyBuilder fb{w, c, s, lrng, out.family, std::vector<Vec3>(seeded, Vec3{0, 0, 0}),
                         std::vector<std::uint32_t>(seeded, 0)};
        for (CellId cell : cells) {
            if (region_of[cell] < 0) continue;
            Vec3& v = fb.region_center[static_cast<std::size_t>(region_of[cell])];
            const Vec3& p = w.grid.center(cell);
            v.x += p.x, v.y += p.y, v.z += p.z;
        }
        for (Vec3& v : fb.region_center) {
            const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len > 0.0) v.x /= len, v.y /= len, v.z /= len;
        }
        std::vector<std::size_t> all(seeded);
        for (std::size_t r = 0; r < seeded; ++r) all[r] = r;
        fb.grow(std::move(proto), std::move(all), history_years, 0);
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const std::int32_t r = region_of[cells[i]];
            out.cell_language[i] = r >= 0 ? fb.region_language[static_cast<std::size_t>(r)] : out.family;
        }
    }

    // ---- The culture record, named after the family.
    Culture culture;
    culture.id = static_cast<std::uint32_t>(s.cultures.size());
    culture.root = culture.id;
    culture.language = out.cell_language.empty() ? out.family : out.cell_language[0];
    culture.founded = s.tick;
    std::string name = s.languages[out.family].name;
    if (name.rfind("Proto-", 0) == 0) name = name.substr(6);
    culture.name = culture_name_taken(s, name) ? coin_culture_name(c, s, s.languages[culture.language], crng) : name;
    out.culture = culture.id;
    s.cultures.push_back(std::move(culture));
    return out;
}

PeopleCounts count_peoples(const SimState& s) {
    PeopleCounts p;
    std::vector<std::uint8_t> family(s.languages.size(), 0);
    for (std::size_t i = 1; i < s.languages.size(); ++i) {
        const Language& l = s.languages[i];
        if (l.extinct != kNever) continue;
        ++p.languages;
        if (!family[l.family]) {
            family[l.family] = 1;
            ++p.families;
        }
    }
    for (std::size_t i = 1; i < s.cultures.size(); ++i) p.cultures += s.cultures[i].extinct == kNever ? 1 : 0;
    return p;
}

void CultureSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    const Content& content = *w.content;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    const std::size_t nt = content.traits.size();
    const std::size_t count = s.cohorts.size();
    ensure_placeholders(s);

    std::size_t openness = nt;  // index of the openness trait, if the pack has one
    for (std::size_t t = 0; t < nt; ++t) {
        if (content.traits[t].id == "openness") openness = t;
    }

    // Per inhabited cell: circumstances and the trait targets they set.
    std::vector<std::array<double, kMaxTraits>> target(n);
    for (CellId c = 0; c < n; ++c) {
        if (s.cell_population[c] == 0) continue;
        const auto vars = culture_variables(w, s, c);
        for (std::size_t t = 0; t < nt; ++t) target[c][t] = trait_target(content.traits[t], vars);
    }

    // ---- Drift, contact and adaptation, computed from last year's values.
    std::vector<std::array<double, kMaxTraits>> new_traits(count);
    std::vector<std::array<double, kDialectDims>> new_dialect(count);
    std::vector<Cohort> arrivals;
    std::vector<std::int64_t> pop(count);
    for (std::size_t i = 0; i < count; ++i) pop[i] = s.cohorts[i].total();

    // Per cell: the largest language and culture (ties: the lower id) and
    // their largest cohort, which newcomers to them learn from.
    struct Top {
        std::uint32_t language = 0, culture = 0;
        std::int64_t language_pop = 0, culture_pop = 0;
        std::size_t language_cohort = 0, culture_cohort = 0;
    };
    std::vector<Top> cell_top(n);
    {
        std::vector<std::pair<std::uint32_t, std::int64_t>> langs, cults;
        auto add = [](std::vector<std::pair<std::uint32_t, std::int64_t>>& v, std::uint32_t id, std::int64_t p) {
            for (auto& e : v) {
                if (e.first == id) {
                    e.second += p;
                    return;
                }
            }
            v.emplace_back(id, p);
        };
        for (CellId c = 0; c < n; ++c) {
            const auto first = s.cohort_begin[c], last = s.cohort_begin[c + 1];
            if (first == last) continue;
            langs.clear();
            cults.clear();
            for (auto j = first; j < last; ++j) {
                add(langs, s.cohorts[j].language, pop[j]);
                add(cults, s.cohorts[j].culture, pop[j]);
            }
            Top& t = cell_top[c];
            t.language = langs[0].first;
            t.language_pop = -1;
            for (const auto& [id, p] : langs) {
                if (p > t.language_pop || (p == t.language_pop && id < t.language)) t.language = id, t.language_pop = p;
            }
            t.culture = cults[0].first;
            t.culture_pop = -1;
            for (const auto& [id, p] : cults) {
                if (p > t.culture_pop || (p == t.culture_pop && id < t.culture)) t.culture = id, t.culture_pop = p;
            }
            std::int64_t best_l = -1, best_c = -1;
            for (auto j = first; j < last; ++j) {
                if (s.cohorts[j].language == t.language && pop[j] > best_l) best_l = pop[j], t.language_cohort = j;
                if (s.cohorts[j].culture == t.culture && pop[j] > best_c) best_c = pop[j], t.culture_cohort = j;
            }
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        const Cohort& co = s.cohorts[i];
        RngStream rng = ctx.rng(Stream::Culture, static_cast<std::uint32_t>(i));

        std::array<double, kMaxTraits> same{}, other{};
        std::array<double, kDialectDims> speech{};
        double w_same = 0.0, w_other = 0.0, w_speech = 0.0;
        // Who lives here, for language shift and assimilation.
        std::int64_t cell_pop = 0, own_lang = 0, own_culture = 0;

        auto visit = [&](CellId cell, bool home) {
            const auto first = s.cohort_begin[cell], last = s.cohort_begin[cell + 1];
            for (auto j = first; j < last; ++j) {
                const Cohort& o = s.cohorts[j];
                const auto wj = static_cast<double>(pop[j]);
                if (home) {
                    cell_pop += pop[j];
                    if (o.language == co.language) own_lang += pop[j];
                    if (o.culture == co.culture) own_culture += pop[j];
                }
                if (j == i) continue;
                if (o.culture == co.culture) {
                    for (std::size_t t = 0; t < nt; ++t) same[t] += wj * o.traits[t];
                    w_same += wj;
                } else {
                    for (std::size_t t = 0; t < nt; ++t) other[t] += wj * o.traits[t];
                    w_other += wj;
                }
                if (o.language == co.language) {
                    for (int d = 0; d < kDialectDims; ++d) speech[d] += wj * o.dialect[d];
                    w_speech += wj;
                }
            }
        };
        visit(co.cell, true);
        for (CellId nb : w.grid.neighbors(co.cell)) visit(nb, false);

        const std::uint32_t top_lang = cell_top[co.cell].language, top_culture = cell_top[co.cell].culture;
        const std::int64_t top_lang_pop = cell_top[co.cell].language_pop;
        const std::int64_t top_culture_pop = cell_top[co.cell].culture_pop;
        const std::size_t top_lang_cohort = cell_top[co.cell].language_cohort;
        const std::size_t top_culture_cohort = cell_top[co.cell].culture_cohort;

        // Rates for this cell's size (see kReferenceCellAreaKm2). The pull
        // between neighbours scales with 1 / area, as diffusion would. Drift
        // scales only with area^(-1/8): on a 2-D grid the spread of dialects
        // also grows with the log of distance counted in cells, so finer
        // grids diverge faster on their own. Tuned so grid levels 5 and 6
        // settle at similar numbers of languages.
        const double area_scale = kReferenceCellAreaKm2 / w.grid.area_km2(co.cell);
        const double drift_scale = std::sqrt(std::sqrt(std::sqrt(area_scale)));
        const double culture_pull = std::min(0.5, kCultureCohesion * area_scale);
        const double dialect_pull = std::min(0.5, kDialectCohesion * area_scale);

        // Traits.
        const double open = openness < nt ? co.traits[openness] : 0.5;
        for (std::size_t t = 0; t < nt; ++t) {
            const CultureTrait& def = content.traits[t];
            double x = co.traits[t];
            double dx = 0.0;
            if (w_same > 0.0) dx += culture_pull * (same[t] / w_same - x);
            if (w_other > 0.0) dx += kCultureContact * (0.5 + open) * (other[t] / w_other - x);
            dx += def.adapt / 100.0 * (target[co.cell][t] - x);
            dx += noise(rng, drift_scale * def.drift / 10.0);  // per century = per year x sqrt(100)
            new_traits[i][t] = clamp01(x + dx);
        }
        // Dialect.
        for (int d = 0; d < kDialectDims; ++d) {
            double x = co.dialect[d];
            if (w_speech > 0.0) x += dialect_pull * (speech[d] / w_speech - x);
            new_dialect[i][d] = x + noise(rng, drift_scale * kDialectDrift);
        }

        // Language shift: some of a minority's people take up the cell's
        // main language, keeping their customs.
        if (cell_pop > 0 && top_lang != co.language) {
            const double gap = static_cast<double>(top_lang_pop - own_lang) / static_cast<double>(cell_pop);
            const double rate = std::min(1.0, kLanguageShiftRate * gap);
            Cohort moved;
            moved.cell = co.cell;
            moved.culture = co.culture;
            moved.language = top_lang;
            moved.traits = new_traits[i];
            moved.dialect = s.cohorts[top_lang_cohort].dialect;
            std::int64_t moved_n = 0;
            Cohort& src = s.cohorts[i];
            for (int b = 0; b < kAgeBins; ++b) {
                const auto f = std::min<std::int64_t>(src.female[b], stochastic_round(rate * src.female[b], rng));
                const auto m = std::min<std::int64_t>(src.male[b], stochastic_round(rate * src.male[b], rng));
                src.female[b] -= static_cast<std::uint32_t>(f);
                src.male[b] -= static_cast<std::uint32_t>(m);
                moved.female[b] = static_cast<std::uint32_t>(f);
                moved.male[b] = static_cast<std::uint32_t>(m);
                moved_n += f + m;
            }
            if (moved_n > 0) arrivals.push_back(moved);
        }
        // Assimilation: some take up the cell's main culture, keeping their
        // language.
        if (cell_pop > 0 && top_culture != co.culture) {
            const double gap = static_cast<double>(top_culture_pop - own_culture) / static_cast<double>(cell_pop);
            const double rate = std::min(1.0, kAssimilationRate * gap);
            Cohort moved;
            moved.cell = co.cell;
            moved.culture = top_culture;
            moved.language = co.language;
            moved.traits = s.cohorts[top_culture_cohort].traits;
            moved.dialect = new_dialect[i];
            std::int64_t moved_n = 0;
            Cohort& src = s.cohorts[i];
            for (int b = 0; b < kAgeBins; ++b) {
                const auto f = std::min<std::int64_t>(src.female[b], stochastic_round(rate * src.female[b], rng));
                const auto m = std::min<std::int64_t>(src.male[b], stochastic_round(rate * src.male[b], rng));
                src.female[b] -= static_cast<std::uint32_t>(f);
                src.male[b] -= static_cast<std::uint32_t>(m);
                moved.female[b] = static_cast<std::uint32_t>(f);
                moved.male[b] = static_cast<std::uint32_t>(m);
                moved_n += f + m;
            }
            if (moved_n > 0) arrivals.push_back(moved);
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        s.cohorts[i].traits = new_traits[i];
        s.cohorts[i].dialect = new_dialect[i];
    }
    s.cohorts.insert(s.cohorts.end(), arrivals.begin(), arrivals.end());
    s.normalize_cohorts(n);

    // ---- Every language changes a little.
    for (std::size_t id = 1; id < s.languages.size(); ++id) {
        Language& lang = s.languages[id];
        if (lang.extinct != kNever) continue;
        RngStream rng = ctx.rng(Stream::Language, static_cast<std::uint32_t>(id));
        evolve_language(content, lang, rng, s.next_cognate);
    }

    // ---- Splits.
    const std::int64_t year = ctx.tick / kTicksPerYear;
    if (year % kSplitCheckYears == kSplitCheckYears - 1) {
        bool split_any = false;
        // Languages.
        {
            std::vector<std::vector<std::uint32_t>> members(s.languages.size());
            for (std::size_t i = 0; i < s.cohorts.size(); ++i) {
                members[s.cohorts[i].language].push_back(static_cast<std::uint32_t>(i));
            }
            const std::size_t existing = s.languages.size();
            for (std::size_t id = 1; id < existing; ++id) {
                const auto group = find_split(s, members[id], kDialectDims, kLanguageSplitDistance,
                                              [&](std::uint32_t i) { return s.cohorts[i].dialect.data(); });
                if (group.empty()) continue;
                Language d = s.languages[id];  // a copy: push_back may move the parent
                d.id = static_cast<std::uint32_t>(s.languages.size());
                d.parent = static_cast<std::uint32_t>(id);
                d.founded = ctx.tick;
                d.extinct = kNever;
                d.sound_changes.clear();
                RngStream rng = ctx.rng(Stream::Language, kNewLanguageEntity | d.id);
                for (int y = 0; y < kSplitCatchUpYears; ++y) evolve_language(content, d, rng, s.next_cognate);
                d.name = unique_language_name(content, s, d, rng);
                for (std::uint32_t i : group) s.cohorts[i].language = d.id;
                s.events.push_back({ctx.tick, EventType::LanguageSplit, s.cohorts[group[0]].cell, d.id});
                s.languages.push_back(std::move(d));
                split_any = true;
            }
        }
        // Cultures.
        {
            std::vector<std::vector<std::uint32_t>> members(s.cultures.size());
            for (std::size_t i = 0; i < s.cohorts.size(); ++i) {
                members[s.cohorts[i].culture].push_back(static_cast<std::uint32_t>(i));
            }
            const std::size_t existing = s.cultures.size();
            for (std::size_t id = 1; id < existing; ++id) {
                const auto group = find_split(s, members[id], nt, kCultureSplitDistance,
                                              [&](std::uint32_t i) { return s.cohorts[i].traits.data(); });
                if (group.empty()) continue;
                Culture d;
                d.id = static_cast<std::uint32_t>(s.cultures.size());
                d.parent = static_cast<std::uint32_t>(id);
                d.root = s.cultures[id].root;
                d.founded = ctx.tick;
                // Its main language: that of the largest cohort that splits off.
                std::uint32_t biggest = group[0];
                for (std::uint32_t i : group) {
                    if (s.cohorts[i].total() > s.cohorts[biggest].total()) biggest = i;
                }
                d.language = s.cohorts[biggest].language;
                RngStream rng = ctx.rng(Stream::Culture, kNewCultureEntity | d.id);
                d.name = coin_culture_name(content, s, s.languages[d.language], rng);
                for (std::uint32_t i : group) s.cohorts[i].culture = d.id;
                s.events.push_back({ctx.tick, EventType::CultureSplit, s.cohorts[biggest].cell, d.id});
                s.cultures.push_back(std::move(d));
                split_any = true;
            }
        }
        if (split_any) s.normalize_cohorts(n);
    }

    // ---- Extinctions.
    std::vector<std::uint8_t> lang_alive(s.languages.size(), 0), culture_alive(s.cultures.size(), 0);
    for (const Cohort& co : s.cohorts) {
        lang_alive[co.language] = 1;
        culture_alive[co.culture] = 1;
    }
    for (std::size_t id = 1; id < s.languages.size(); ++id) {
        if (!lang_alive[id] && s.languages[id].extinct == kNever) {
            s.languages[id].extinct = ctx.tick;
            s.events.push_back({ctx.tick, EventType::LanguageExtinct, kInvalidCell, static_cast<std::int64_t>(id)});
        }
    }
    for (std::size_t id = 1; id < s.cultures.size(); ++id) {
        if (!culture_alive[id] && s.cultures[id].extinct == kNever) {
            s.cultures[id].extinct = ctx.tick;
            s.events.push_back({ctx.tick, EventType::CultureExtinct, kInvalidCell, static_cast<std::int64_t>(id)});
        }
    }
}

}  // namespace histsim
