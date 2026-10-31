#include "histsim/content.hpp"

#include <stdexcept>
#include <string>

#include "histsim/hash.hpp"
#include "histsim/json.hpp"

namespace histsim {

// Generated at configure time from data/*.json (see libsim/CMakeLists.txt).
extern const unsigned char kSpeciesJson[];
extern const std::size_t kSpeciesJsonSize;
extern const unsigned char kPhonologyJson[];
extern const std::size_t kPhonologyJsonSize;
extern const unsigned char kCultureJson[];
extern const std::size_t kCultureJsonSize;
extern const unsigned char kDiseasesJson[];
extern const std::size_t kDiseasesJsonSize;
extern const unsigned char kTechniquesJson[];
extern const std::size_t kTechniquesJsonSize;

namespace {

Trapezoid trapezoid(const json::Value& v) {
    const auto& a = v.as_array();
    if (a.size() != 4) {
        throw std::runtime_error("content: " + v.path() + " should be [min, best_from, best_to, max]");
    }
    Trapezoid t{a[0].as_number(), a[1].as_number(), a[2].as_number(), a[3].as_number()};
    if (!(t.lo <= t.best_lo && t.best_lo <= t.best_hi && t.best_hi <= t.hi) || t.lo == t.hi) {
        throw std::runtime_error("content: " + v.path() + " must be in increasing order with min < max");
    }
    // A zero-width ramp would divide by zero: nudge it to a step.
    if (t.best_lo == t.lo) t.lo -= 1e-9;
    if (t.best_hi == t.hi) t.hi += 1e-9;
    return t;
}

double unit(const json::Value& obj, std::string_view key, double fallback) {
    const double v = obj.number_or(key, fallback);
    if (v < 0.0 || v > 1.0) {
        throw std::runtime_error("content: " + obj.get(key).path() + " must be between 0 and 1");
    }
    return v;
}

[[noreturn]] void fail(const json::Value& v, const std::string& what) {
    throw std::runtime_error("content: " + v.path() + " " + what);
}

// ---- species

void load_species(Content& c, std::string_view text) {
    const json::Value doc = json::parse(text, "data/species.json");
    if (doc.at("version").as_int() != 1) fail(doc.at("version"), "is not a supported species pack version");
    for (const json::Value& s : doc.at("species").as_array()) {
        SpeciesDef d;
        d.id = s.at("id").as_string();
        d.name = s.at("name").as_string();
        d.domesticated = s.string_or("domesticated", d.name);
        d.domesticated_before_start = s.has("domesticated_before_start") && s.at("domesticated_before_start").as_bool();
        const std::string& kind = s.at("kind").as_string();
        if (kind == "plant") d.kind = SpeciesKind::Plant;
        else if (kind == "animal") d.kind = SpeciesKind::Animal;
        else fail(s.at("kind"), "must be \"plant\" or \"animal\"");
        d.food = s.string_or("food", "");
        d.coldest_month_c = trapezoid(s.at("coldest_month_c"));
        d.warmest_month_c = trapezoid(s.at("warmest_month_c"));
        d.annual_precip_mm = trapezoid(s.at("annual_precip_mm"));
        d.rain_season = trapezoid(s.at("rain_season"));
        d.elevation_m = trapezoid(s.at("elevation_m"));
        d.river_affinity = unit(s, "river_affinity", 0.0);
        d.native_regions = static_cast<int>(s.at("native_regions").as_int());
        if (d.native_regions < 1 || d.native_regions > 8) fail(s.at("native_regions"), "must be 1-8");
        d.range_km = s.at("range_km").as_number();
        if (d.range_km <= 0.0 || d.range_km > 20000.0) fail(s.at("range_km"), "must be 0-20000");
        d.domesticability = unit(s, "domesticability", 0.5);
        d.food_value = s.number_or("food_value", 0.5);
        for (const SpeciesDef& other : c.species) {
            if (other.id == d.id) fail(s.at("id"), "duplicates species \"" + d.id + "\"");
        }
        c.species.push_back(std::move(d));
    }
    if (c.species.size() > kMaxSpecies) {
        throw std::runtime_error("content: at most " + std::to_string(kMaxSpecies) + " species are supported");
    }
}

// ---- phonology

constexpr const char* kPlaces[] = {"labial", "dental", "alveolar", "palatal", "velar", "uvular", "glottal"};
constexpr const char* kManners[] = {"stop", "affricate", "fricative", "nasal", "lateral", "rhotic", "glide"};
constexpr const char* kHeights[] = {"high", "mid", "low"};
constexpr const char* kBacknesses[] = {"front", "central", "back"};
constexpr const char* kKinds[] = {"consonant", "vowel"};
constexpr const char* kEnvs[] = {"anywhere", "word_initial", "word_final", "between_vowels", "before_front_vowel",
                                 "before_high_front_vowel", "before_consonant", "after_vowel", "before_velar"};

template <std::size_t N>
std::uint8_t pick(const json::Value& v, const char* const (&names)[N]) {
    const std::string& s = v.as_string();
    for (std::size_t i = 0; i < N; ++i) {
        if (s == names[i]) return static_cast<std::uint8_t>(i);
    }
    std::string list;
    for (std::size_t i = 0; i < N; ++i) list += (i ? ", " : "") + std::string(names[i]);
    fail(v, "must be one of: " + list);
}

// A match field: absent = any; a string or a list of strings = a set.
template <std::size_t N>
std::uint32_t mask(const json::Value& obj, std::string_view key, const char* const (&names)[N]) {
    const json::Value& v = obj.get(key);
    if (v.is_null()) return ~0u;
    std::uint32_t m = 0;
    if (v.kind() == json::Value::Kind::Array) {
        for (const json::Value& e : v.as_array()) m |= 1u << pick(e, names);
    } else {
        m = 1u << pick(v, names);
    }
    return m;
}

WeightedOptions weighted(const json::Value& obj) {
    WeightedOptions w;
    for (const auto& [k, v] : obj.members()) {
        const double x = v.as_number();
        if (x < 0.0) fail(v, "must not be negative");
        w.options.emplace_back(k, x);
    }
    if (w.options.empty()) fail(obj, "needs at least one option");
    return w;
}

void load_phonology(Content& c, std::string_view text) {
    const json::Value doc = json::parse(text, "data/phonology.json");
    if (doc.at("version").as_int() != 1) fail(doc.at("version"), "is not a supported phonology pack version");

    for (const json::Value& v : doc.at("phonemes").as_array()) {
        Phoneme p;
        p.text = v.at("text").as_string();
        if (p.text.empty()) fail(v.at("text"), "must not be empty");
        p.kind = static_cast<PhonemeKind>(pick(v.at("kind"), kKinds));
        if (p.kind == PhonemeKind::Consonant) {
            p.place = static_cast<Place>(pick(v.at("place"), kPlaces));
            p.manner = static_cast<Manner>(pick(v.at("manner"), kManners));
            p.voiced = v.at("voiced").as_bool();
        } else {
            p.height = static_cast<Height>(pick(v.at("height"), kHeights));
            p.backness = static_cast<Backness>(pick(v.at("backness"), kBacknesses));
        }
        p.frequency = unit(v, "frequency", 0.5);
        for (const Phoneme& o : c.phonemes) {
            if (o.text == p.text) fail(v.at("text"), "duplicates phoneme \"" + p.text + "\"");
            const bool same = o.kind == p.kind &&
                              (p.kind == PhonemeKind::Consonant
                                   ? o.place == p.place && o.manner == p.manner && o.voiced == p.voiced
                                   : o.height == p.height && o.backness == p.backness);
            if (same) fail(v, "has the same features as \"" + o.text + "\"");
        }
        c.phonemes.push_back(std::move(p));
    }
    if (c.phonemes.size() > kMaxPhonemes) fail(doc.at("phonemes"), "has more than 64 phonemes");
    int consonants = 0, vowels = 0;
    for (const Phoneme& p : c.phonemes) (p.kind == PhonemeKind::Vowel ? vowels : consonants) += 1;
    if (consonants < 6 || vowels < 3) fail(doc.at("phonemes"), "needs at least 6 consonants and 3 vowels");

    for (const json::Value& v : doc.at("syllables").as_array()) {
        SyllableShape s;
        s.shape = v.at("shape").as_string();
        bool has_vowel = false;
        for (char ch : s.shape) {
            if (ch != 'C' && ch != 'V') fail(v.at("shape"), "may only contain C and V");
            has_vowel = has_vowel || ch == 'V';
        }
        if (!has_vowel || s.shape.size() > 6) fail(v.at("shape"), "needs one vowel and at most 6 sounds");
        s.probability = unit(v, "probability", 0.5);
        c.syllables.push_back(std::move(s));
    }
    if (c.syllables.empty() || c.syllables[0].shape != "CV") {
        fail(doc.at("syllables"), "must start with \"CV\" (every language has it)");
    }

    const json::Value& typ = doc.at("typology");
    c.word_order = weighted(typ.at("word_order"));
    c.morphology = weighted(typ.at("morphology"));
    c.tone = weighted(typ.at("tone"));

    const std::size_t np = c.phonemes.size();
    for (const json::Value& v : doc.at("sound_changes").as_array()) {
        SoundChange r;
        r.name = v.at("name").as_string();
        r.where = static_cast<SoundEnv>(pick(v.at("where"), kEnvs));
        r.weight = v.number_or("weight", 1.0);
        if (r.weight <= 0.0) fail(v.at("weight"), "must be positive");
        const json::Value& m = v.at("match");
        const std::uint32_t kinds = mask(m, "kind", kKinds), places = mask(m, "place", kPlaces),
                            manners = mask(m, "manner", kManners), heights = mask(m, "height", kHeights),
                            backs = mask(m, "backness", kBacknesses);
        const json::Value& voiced = m.get("voiced");
        for (const auto& [key, val] : m.members()) {
            if (key != "kind" && key != "place" && key != "manner" && key != "voiced" && key != "height" &&
                key != "backness") {
                fail(val, "is not a sound feature");
            }
        }
        const int actions = (v.has("to") ? 1 : 0) + (v.has("delete") ? 1 : 0) + (v.has("insert_after") ? 1 : 0);
        if (actions != 1) fail(v, "needs exactly one of \"to\", \"delete\" or \"insert_after\"");
        r.shift_to.resize(np);
        for (std::size_t i = 0; i < np; ++i) {
            r.shift_to[i] = static_cast<PhonemeId>(i);
            const Phoneme& p = c.phonemes[i];
            const bool consonant = p.kind == PhonemeKind::Consonant;
            bool match = (kinds >> static_cast<int>(p.kind)) & 1u;
            if (consonant) {
                // Vowel-only fields exclude consonants, and vice versa.
                match = match && heights == ~0u && backs == ~0u;
                match = match && ((places >> static_cast<int>(p.place)) & 1u) &&
                        ((manners >> static_cast<int>(p.manner)) & 1u);
                if (!voiced.is_null()) match = match && voiced.as_bool() == p.voiced;
            } else {
                match = match && places == ~0u && manners == ~0u && voiced.is_null();
                match = match && ((heights >> static_cast<int>(p.height)) & 1u) &&
                        ((backs >> static_cast<int>(p.backness)) & 1u);
            }
            if (match) r.applies |= std::uint64_t{1} << i;
        }
        if (r.applies == 0) fail(m, "matches no phoneme");
        if (v.has("delete")) {
            if (!v.at("delete").as_bool()) fail(v.at("delete"), "must be true");
            r.action = SoundChange::Action::Delete;
        } else if (v.has("insert_after")) {
            r.action = SoundChange::Action::InsertAfter;
            const std::string& t = v.at("insert_after").as_string();
            bool found = false;
            for (std::size_t i = 0; i < np; ++i) {
                if (c.phonemes[i].text == t) {
                    r.insert = static_cast<PhonemeId>(i);
                    found = true;
                }
            }
            if (!found) fail(v.at("insert_after"), "is not a phoneme in the pack");
        } else {
            r.action = SoundChange::Action::Shift;
            const json::Value& to = v.at("to");
            std::uint64_t changes = 0;
            for (std::size_t i = 0; i < np; ++i) {
                if (!((r.applies >> i) & 1u)) continue;
                Phoneme target = c.phonemes[i];
                for (const auto& [key, val] : to.members()) {
                    if (key == "place") target.place = static_cast<Place>(pick(val, kPlaces));
                    else if (key == "manner") target.manner = static_cast<Manner>(pick(val, kManners));
                    else if (key == "voiced") target.voiced = val.as_bool();
                    else if (key == "height") target.height = static_cast<Height>(pick(val, kHeights));
                    else if (key == "backness") target.backness = static_cast<Backness>(pick(val, kBacknesses));
                    else fail(val, "is not a sound feature");
                }
                for (std::size_t j = 0; j < np; ++j) {
                    const Phoneme& q = c.phonemes[j];
                    const bool same = q.kind == target.kind &&
                                      (q.kind == PhonemeKind::Consonant
                                           ? q.place == target.place && q.manner == target.manner &&
                                                 q.voiced == target.voiced
                                           : q.height == target.height && q.backness == target.backness);
                    if (same && j != i) {
                        r.shift_to[i] = static_cast<PhonemeId>(j);
                        changes |= std::uint64_t{1} << i;
                    }
                }
            }
            // Sounds whose shifted form is not in the pack are left alone.
            r.applies = changes;
            if (r.applies == 0) fail(to, "changes no phoneme into another phoneme of the pack");
        }
        c.sound_changes.push_back(std::move(r));
    }
    if (c.sound_changes.empty()) fail(doc.at("sound_changes"), "needs at least one rule");

    bool people = false, land = false, river = false;
    for (const json::Value& v : doc.at("concepts").as_array()) {
        Concept k;
        k.id = v.at("id").as_string();
        k.stability = unit(v, "stability", 0.5);
        const auto& syl = v.at("syllables").as_array();
        if (syl.size() != 2) fail(v.at("syllables"), "should be [min, max]");
        k.min_syllables = static_cast<int>(syl[0].as_int());
        k.max_syllables = static_cast<int>(syl[1].as_int());
        if (k.min_syllables < 1 || k.max_syllables < k.min_syllables || k.max_syllables > 4) {
            fail(v.at("syllables"), "must be 1 <= min <= max <= 4");
        }
        for (const Concept& o : c.concepts) {
            if (o.id == k.id) fail(v.at("id"), "duplicates concept \"" + k.id + "\"");
        }
        if (k.id == "people") c.people_concept = c.concepts.size(), people = true;
        if (k.id == "land") c.land_concept = c.concepts.size(), land = true;
        if (k.id == "river") c.river_concept = c.concepts.size(), river = true;
        c.concepts.push_back(std::move(k));
    }
    if (!people || !land || !river) fail(doc.at("concepts"), "must include \"people\", \"land\" and \"river\"");
    if (c.concepts.size() > kMaxConcepts) fail(doc.at("concepts"), "has more than 128 concepts");
}

// ---- culture

constexpr const char* kVars[] = {"crowding", "aquatic",     "seasonality", "growing_season",
                                 "game",     "wild_plants", "farming",     "ruled"};

void load_culture(Content& c, std::string_view text) {
    const json::Value doc = json::parse(text, "data/culture.json");
    if (doc.at("version").as_int() != 1) fail(doc.at("version"), "is not a supported culture pack version");
    for (const json::Value& v : doc.at("traits").as_array()) {
        CultureTrait t;
        t.id = v.at("id").as_string();
        t.name = v.at("name").as_string();
        t.low = v.at("low").as_string();
        t.high = v.at("high").as_string();
        t.drift = unit(v, "drift", 0.05);
        t.adapt = unit(v, "adapt", 0.1);
        t.base = unit(v, "base", 0.5);
        for (const auto& [key, val] : v.at("push").members()) {
            std::size_t i = 0;
            while (i < kCultureVarCount && key != kVars[i]) ++i;
            if (i == kCultureVarCount) {
                fail(val, "is not a culture variable (crowding, aquatic, seasonality, growing_season, game, "
                          "wild_plants, farming, ruled)");
            }
            t.push[i] = val.as_number();
            if (t.push[i] < -1.0 || t.push[i] > 1.0) fail(val, "must be between -1 and 1");
        }
        for (const CultureTrait& o : c.traits) {
            if (o.id == t.id) fail(v.at("id"), "duplicates trait \"" + t.id + "\"");
        }
        c.traits.push_back(std::move(t));
    }
    if (c.traits.empty() || c.traits.size() > kMaxTraits) fail(doc.at("traits"), "must have 1-12 traits");
}

// ---- diseases

std::array<double, kDiseaseAgeGroups> age_array(const json::Value& v) {
    const auto& a = v.as_array();
    if (a.size() != kDiseaseAgeGroups) fail(v, "needs 4 values: infants, children, adults, elders");
    std::array<double, kDiseaseAgeGroups> out{};
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = a[i].as_number();
        if (out[i] < 0.0) fail(a[i], "must not be negative");
    }
    return out;
}

void load_diseases(Content& c, std::string_view text) {
    const json::Value doc = json::parse(text, "data/diseases.json");
    if (doc.at("version").as_int() != 1) fail(doc.at("version"), "is not a supported disease pack version");
    for (const json::Value& v : doc.at("diseases").as_array()) {
        DiseaseDef d;
        d.id = v.at("id").as_string();
        d.name = v.at("name").as_string();
        const std::string& kind = v.at("kind").as_string();
        if (kind == "endemic") {
            d.kind = DiseaseKind::Endemic;
            d.excess_mortality = unit(v, "excess_mortality", 0.0);
            if (v.has("age_weight")) d.age_weight = age_array(v.at("age_weight"));
            d.farming_weight = unit(v, "farming_weight", 0.0);
            d.density_weight = unit(v, "density_weight", 0.0);
            if (d.farming_weight + d.density_weight > 1.0) fail(v, "farming_weight + density_weight must be at most 1");
        } else if (kind == "epidemic") {
            d.kind = DiseaseKind::Epidemic;
            d.r0 = v.at("r0").as_number();
            if (d.r0 <= 0.0 || d.r0 > 50.0) fail(v.at("r0"), "must be between 0 and 50");
            d.fatality = age_array(v.at("fatality"));
            for (double f : d.fatality) {
                if (f > 1.0) fail(v.at("fatality"), "values must be at most 1");
            }
            d.immunity_years = v.number_or("immunity_years", -1.0);
            if (d.immunity_years < 0.0 && d.immunity_years != -1.0) fail(v.at("immunity_years"), "must be -1, 0 or more");
            d.critical_population = v.number_or("critical_population", 0.0);
        } else {
            fail(v.at("kind"), "must be \"endemic\" or \"epidemic\"");
        }
        if (v.has("warmest_month_c")) d.warmest_month_c = trapezoid(v.at("warmest_month_c")), d.has_niche = true;
        if (v.has("annual_precip_mm")) d.annual_precip_mm = trapezoid(v.at("annual_precip_mm")), d.has_niche = true;
        if (v.has("elevation_m")) d.elevation_m = trapezoid(v.at("elevation_m")), d.has_niche = true;
        if (v.has("hosts")) {
            for (const json::Value& h : v.at("hosts").as_array()) {
                std::size_t i = 0;
                while (i < c.species.size() && c.species[i].id != h.as_string()) ++i;
                if (i == c.species.size()) fail(h, "is not a species id");
                d.hosts |= 1u << i;
            }
        }
        d.emergence_min_population = v.number_or("emergence_min_population", 0.0);
        d.emergence_min_town = v.number_or("emergence_min_town", 0.0);
        d.emergence_chance = unit(v, "emergence_chance", 0.0);
        d.present_at_start = v.has("present_at_start") && v.at("present_at_start").as_bool();
        for (const DiseaseDef& o : c.diseases) {
            if (o.id == d.id) fail(v.at("id"), "duplicates disease \"" + d.id + "\"");
        }
        c.diseases.push_back(std::move(d));
    }
    if (c.diseases.size() > kMaxDiseases) fail(doc.at("diseases"), "has more than 16 diseases");
}

// ---- techniques (after species and culture: they refer to both)

constexpr const char* kPlacesWhere[] = {"anywhere", "river", "coast", "water", "dry", "hills", "forest"};

void load_techniques(Content& c, std::string_view text) {
    const json::Value doc = json::parse(text, "data/techniques.json");
    if (doc.at("version").as_int() != 1) fail(doc.at("version"), "is not a supported technique pack version");
    for (const json::Value& v : doc.at("techniques").as_array()) {
        TechniqueDef t;
        t.id = v.at("id").as_string();
        t.name = v.at("name").as_string();
        if (v.has("requires")) {
            for (const json::Value& r : v.at("requires").as_array()) {
                std::size_t i = 0;
                while (i < c.techniques.size() && c.techniques[i].id != r.as_string()) ++i;
                if (i == c.techniques.size()) fail(r, "is not a technique listed earlier");
                t.prerequisites |= std::uint64_t{1} << i;
            }
        }
        t.requires_crops = v.has("requires_crops") && v.at("requires_crops").as_bool();
        t.requires_herds = v.has("requires_herds") && v.at("requires_herds").as_bool();
        if (v.has("requires_species")) {
            for (const json::Value& sp : v.at("requires_species").as_array()) {
                std::size_t i = 0;
                while (i < c.species.size() && c.species[i].id != sp.as_string()) ++i;
                if (i == c.species.size()) fail(sp, "is not a species id");
                t.requires_species |= 1u << i;
            }
        }
        if (v.has("where")) t.where = static_cast<TechPlace>(pick(v.at("where"), kPlacesWhere));
        t.rate = unit(v, "rate", 0.0);
        t.start_chance = unit(v, "start_chance", 0.0);
        t.min_town = v.number_or("min_town", 0.0);
        if (v.has("affinity")) {
            for (const auto& [key, val] : v.at("affinity").members()) {
                std::size_t i = 0;
                while (i < c.traits.size() && c.traits[i].id != key) ++i;
                if (i == c.traits.size()) fail(val, "is not a culture trait id");
                t.affinity[i] = val.as_number();
            }
        }
        for (const auto& [key, val] : v.at("effects").members()) {
            const double x = val.as_number();
            if (key == "crop_yield") t.effects.crop_yield = x;
            else if (key == "herd_yield") t.effects.herd_yield = x;
            else if (key == "forage") t.effects.forage = x;
            else if (key == "aquatic") t.effects.aquatic = x;
            else if (key == "harvest_buffer") t.effects.harvest_buffer = x;
            else if (key == "famine") t.effects.famine = x;
            else if (key == "contact") t.effects.contact = x;
            else if (key == "administration") t.effects.administration = x;
            else fail(val, "is not an effect (crop_yield, herd_yield, forage, aquatic, harvest_buffer, famine, "
                           "contact, administration)");
        }
        for (const TechniqueDef& o : c.techniques) {
            if (o.id == t.id) fail(v.at("id"), "duplicates technique \"" + t.id + "\"");
        }
        c.techniques.push_back(std::move(t));
    }
    if (c.techniques.size() > kMaxTechniques) fail(doc.at("techniques"), "has more than 64 techniques");
}

}  // namespace

ContentSources default_sources() {
    auto view = [](const unsigned char* p, std::size_t n) {
        return std::string_view(reinterpret_cast<const char*>(p), n);
    };
    return {view(kSpeciesJson, kSpeciesJsonSize), view(kPhonologyJson, kPhonologyJsonSize),
            view(kCultureJson, kCultureJsonSize), view(kDiseasesJson, kDiseasesJsonSize),
            view(kTechniquesJson, kTechniquesJsonSize)};
}

Content load_content(const ContentSources& src) {
    Content c;
    load_species(c, src.species);
    load_phonology(c, src.phonology);
    load_culture(c, src.culture);
    load_diseases(c, src.diseases);
    load_techniques(c, src.techniques);
    Hasher h;
    for (std::string_view text : {src.species, src.phonology, src.culture, src.diseases, src.techniques}) {
        h.u64(text.size());
        h.bytes(text.data(), text.size());
    }
    c.hash = h.digest();
    return c;
}

const Content& default_content() {
    static const Content content = load_content(default_sources());
    return content;
}

}  // namespace histsim
