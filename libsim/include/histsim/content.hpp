// Content packs: the data that defines what exists in a world. Now: wild
// species, the sounds and core vocabulary of languages with their sound
// changes, and culture traits. Later: crops, goods, technologies, diseases
// and institutions.
//
// The default packs live in data/*.json and are compiled into the library, so
// every build carries exactly the content it was tested with. Their hash goes
// into the world hash, so a snapshot made with different content is refused.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace histsim {

// [min, best_from, best_to, max]: 0 outside min..max, 1 in best_from..best_to,
// linear in between.
struct Trapezoid {
    double lo = 0.0, best_lo = 0.0, best_hi = 0.0, hi = 0.0;

    [[nodiscard]] double operator()(double x) const noexcept {
        if (x <= lo || x >= hi) return 0.0;
        if (x < best_lo) return (x - lo) / (best_lo - lo);
        if (x > best_hi) return (hi - x) / (hi - best_hi);
        return 1.0;
    }
};

// ---------------------------------------------------------------- species

enum class SpeciesKind : std::uint8_t { Plant, Animal };

struct SpeciesDef {
    std::string id;
    std::string name;
    std::string domesticated;  // name of the domesticated form, e.g. "Wheat and barley"
    bool domesticated_before_start = false;  // e.g. dogs: known where native from the start
    SpeciesKind kind = SpeciesKind::Plant;
    std::string food;
    Trapezoid coldest_month_c;
    Trapezoid warmest_month_c;
    Trapezoid annual_precip_mm;
    Trapezoid rain_season;
    Trapezoid elevation_m;
    double river_affinity = 0.0;
    int native_regions = 1;
    double range_km = 2000.0;
    double domesticability = 0.5;
    double food_value = 0.5;
};

inline constexpr std::size_t kMaxSpecies = 32;  // presence is a 32-bit mask per cell

// ---------------------------------------------------------------- phonology

// Articulatory features. Values index the name lists in content.cpp.
enum class PhonemeKind : std::uint8_t { Consonant, Vowel };
enum class Place : std::uint8_t { Labial, Dental, Alveolar, Palatal, Velar, Uvular, Glottal, Count };
enum class Manner : std::uint8_t { Stop, Affricate, Fricative, Nasal, Lateral, Rhotic, Glide, Count };
enum class Height : std::uint8_t { High, Mid, Low, Count };
enum class Backness : std::uint8_t { Front, Central, Back, Count };

struct Phoneme {
    std::string text;  // romanization
    PhonemeKind kind = PhonemeKind::Consonant;
    Place place = Place::Labial;        // consonants
    Manner manner = Manner::Stop;       // consonants
    bool voiced = false;                // consonants
    Height height = Height::High;       // vowels
    Backness backness = Backness::Front;  // vowels
    double frequency = 0.5;             // share of languages that have it
};

// A phoneme id (index into Content::phonemes). Inventories are 64-bit sets.
using PhonemeId = std::uint8_t;
inline constexpr std::size_t kMaxPhonemes = 64;

// Where in a word a sound change applies.
enum class SoundEnv : std::uint8_t {
    Anywhere,
    WordInitial,
    WordFinal,
    BetweenVowels,
    BeforeFrontVowel,
    BeforeHighFrontVowel,
    BeforeConsonant,
    AfterVowel,
    BeforeVelar,
};

struct SoundChange {
    enum class Action : std::uint8_t { Shift, Delete, InsertAfter };
    std::string name;
    SoundEnv where = SoundEnv::Anywhere;
    Action action = Action::Shift;
    double weight = 1.0;
    // Precomputed from the rule: which phonemes it applies to, and for a
    // shift what each becomes (a phoneme whose shifted form is not in the
    // pack is left alone).
    std::uint64_t applies = 0;               // bit p set if the rule changes phoneme p
    std::vector<PhonemeId> shift_to;          // per phoneme, for Action::Shift
    PhonemeId insert = 0;                     // for Action::InsertAfter
};

struct SyllableShape {
    std::string shape;  // C and V, e.g. "CVC"
    double probability = 1.0;
};

struct Concept {
    std::string id;
    double stability = 0.5;  // 0-1: resistance to replacement
    int min_syllables = 1;
    int max_syllables = 2;
};

// A weighted choice, e.g. word order {"SOV", 45}.
struct WeightedOptions {
    std::vector<std::pair<std::string, double>> options;
};

inline constexpr std::size_t kMaxConcepts = 128;

// ---------------------------------------------------------------- culture

// Circumstances that push culture traits (see data/culture.json).
enum class CultureVar : std::uint8_t {
    Crowding,
    Aquatic,
    Seasonality,
    GrowingSeason,
    Game,
    WildPlants,
    Farming,
    Ruled,
    Count
};
inline constexpr std::size_t kCultureVarCount = static_cast<std::size_t>(CultureVar::Count);

struct CultureTrait {
    std::string id;
    std::string name;
    std::string low;   // label of the 0 pole
    std::string high;  // label of the 1 pole
    double drift = 0.05;  // typical random change per century
    double adapt = 0.1;   // share of the gap to the target closed per century
    double base = 0.5;
    std::array<double, kCultureVarCount> push{};
};

inline constexpr std::size_t kMaxTraits = 12;

// ---------------------------------------------------------------- diseases

enum class DiseaseKind : std::uint8_t { Endemic, Epidemic };

// Age groups for disease effects: infants 0-4, children 5-14, adults 15-49,
// elders 50+.
inline constexpr int kDiseaseAgeGroups = 4;
[[nodiscard]] constexpr int disease_age_group(int age_bin) noexcept {
    return age_bin == 0 ? 0 : (age_bin < 3 ? 1 : (age_bin < 10 ? 2 : 3));
}

struct DiseaseDef {
    std::string id;
    std::string name;
    DiseaseKind kind = DiseaseKind::Endemic;
    // Endemic.
    double excess_mortality = 0.0;  // yearly death chance at full burden
    std::array<double, kDiseaseAgeGroups> age_weight{1.0, 1.0, 1.0, 1.0};
    double farming_weight = 0.0;
    double density_weight = 0.0;
    // Epidemic.
    double r0 = 0.0;
    std::array<double, kDiseaseAgeGroups> fatality{};
    double immunity_years = -1.0;  // -1 for life, 0 none
    double critical_population = 0.0;
    // Niche (always 1 when absent).
    Trapezoid warmest_month_c{-1e9, -1e9, 1e9, 1e9};
    Trapezoid annual_precip_mm{-1e9, -1e9, 1e9, 1e9};
    Trapezoid elevation_m{-1e9, -1e9, 1e9, 1e9};
    bool has_niche = false;
    // Emergence.
    std::uint32_t hosts = 0;  // species bits; 0 = none needed
    double emergence_min_population = 0.0;
    double emergence_min_town = 0.0;  // people in towns in the cell
    double emergence_chance = 0.0;
    bool present_at_start = false;
};

inline constexpr std::size_t kMaxDiseases = 16;

// ---------------------------------------------------------------- techniques

// Where a technique can be invented and works (see data/techniques.json).
enum class TechPlace : std::uint8_t { Anywhere, River, Coast, Water, Dry, Hills, Forest };

struct TechEffects {
    double crop_yield = 0.0;
    double herd_yield = 0.0;
    double forage = 0.0;
    double aquatic = 0.0;
    double harvest_buffer = 0.0;
    double famine = 0.0;
    double contact = 0.0;
    double administration = 0.0;
};

struct TechniqueDef {
    std::string id;
    std::string name;
    std::uint64_t prerequisites = 0;     // technique bits
    bool requires_crops = false;
    bool requires_herds = false;
    std::uint32_t requires_species = 0;  // any of these domesticates (0 = none needed)
    TechPlace where = TechPlace::Anywhere;
    double rate = 0.0;                   // inventions per year per sqrt(10,000 people)
    std::array<double, kMaxTraits> affinity{};
    double start_chance = 0.0;
    double min_town = 0.0;               // people in towns needed in the cell
    TechEffects effects;
};

inline constexpr std::size_t kMaxTechniques = 64;

// ---------------------------------------------------------------- pack

struct Content {
    std::vector<SpeciesDef> species;

    std::vector<Phoneme> phonemes;
    std::vector<SyllableShape> syllables;
    WeightedOptions word_order, morphology, tone;
    std::vector<SoundChange> sound_changes;
    std::vector<Concept> concepts;
    std::size_t people_concept = 0;  // index of "people" (language and culture names)
    std::size_t land_concept = 0;
    std::size_t river_concept = 0;

    std::vector<CultureTrait> traits;

    std::vector<DiseaseDef> diseases;
    std::vector<TechniqueDef> techniques;

    std::uint64_t hash = 0;  // of all source texts
};

// The text of each pack file.
struct ContentSources {
    std::string_view species;
    std::string_view phonology;
    std::string_view culture;
    std::string_view diseases;
    std::string_view techniques;
};

// The packs compiled into this build.
ContentSources default_sources();

// Parse and validate packs. Throws std::runtime_error with the file position
// or field path on any problem.
Content load_content(const ContentSources& sources);

// The default packs, parsed once.
const Content& default_content();

}  // namespace histsim
