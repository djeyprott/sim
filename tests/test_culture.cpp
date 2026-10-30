// Languages and cultures (M2 slice 2): content, sound changes, word
// replacement, family trees, splits, shift and extinction.
#include <set>
#include <string>

#include "histsim/language.hpp"
#include "histsim/simulation.hpp"
#include "histsim/systems/culture.hpp"
#include "testing.hpp"

using namespace histsim;

namespace {

PhonemeId ph(const Content& c, const std::string& text) {
    for (std::size_t i = 0; i < c.phonemes.size(); ++i) {
        if (c.phonemes[i].text == text) return static_cast<PhonemeId>(i);
    }
    throw std::runtime_error("no phoneme " + text);
}

std::size_t rule(const Content& c, const std::string& name) {
    for (std::size_t i = 0; i < c.sound_changes.size(); ++i) {
        if (c.sound_changes[i].name == name) return i;
    }
    throw std::runtime_error("no rule " + name);
}

std::vector<PhonemeId> word(const Content& c, std::initializer_list<const char*> sounds) {
    std::vector<PhonemeId> w;
    for (const char* s : sounds) w.push_back(ph(c, s));
    return w;
}

Language one_word(const Content& c, std::vector<PhonemeId> w) {
    Language l;
    l.words.resize(c.concepts.size());
    for (auto& x : l.words) x.sounds = w;
    return l;
}

// Every language's parent came before it and is in the same family; every
// cohort speaks a living language and belongs to a living culture.
// With `strict`, also: a language is living exactly when someone speaks it
// (at the very start, a language whose land is all empty is still listed).
void check_tree(const SimState& s, bool strict) {
    std::set<std::uint32_t> spoken, practiced;
    for (const Cohort& co : s.cohorts) {
        spoken.insert(co.language);
        practiced.insert(co.culture);
    }
    std::uint32_t living = 0;
    for (std::uint32_t i = 1; i < s.languages.size(); ++i) {
        const Language& l = s.languages[i];
        CHECK_EQ(l.id, i);
        CHECK(l.parent < i);
        if (l.parent == 0) {
            CHECK_EQ(l.family, i);
        } else {
            CHECK_EQ(l.family, s.languages[l.parent].family);
            CHECK(l.founded >= s.languages[l.parent].founded);
        }
        if (strict) CHECK_EQ(l.extinct == kNever, spoken.count(i) == 1);
        if (l.extinct == kNever) ++living;
    }
    for (std::uint32_t id : spoken) CHECK(id > 0 && s.languages[id].extinct == kNever);
    for (std::uint32_t id : practiced) CHECK(id > 0 && s.cultures[id].extinct == kNever);
    if (strict) CHECK_EQ(living, static_cast<std::uint32_t>(spoken.size()));
    std::set<std::string> names;
    for (std::uint32_t i = 1; i < s.languages.size(); ++i) names.insert(s.languages[i].name);
    CHECK_EQ(names.size(), s.languages.size() - 1);  // names are unique
}

}  // namespace

// ---- Content

TEST(content_phonology_and_culture_packs_load) {
    const Content& c = default_content();
    CHECK(c.phonemes.size() >= 30);
    CHECK(c.sound_changes.size() >= 20);
    CHECK(c.concepts.size() >= 60);
    CHECK(c.traits.size() >= 8 && c.traits.size() <= kMaxTraits);
    CHECK_EQ(c.concepts[c.people_concept].id, std::string("people"));
    for (const SoundChange& r : c.sound_changes) CHECK(r.applies != 0);
    // Shifts map onto the sound with the shifted features.
    const SoundChange& voicing = c.sound_changes[rule(c, "intervocalic voicing")];
    CHECK_EQ(voicing.shift_to[ph(c, "t")], ph(c, "d"));
    CHECK_EQ(voicing.shift_to[ph(c, "k")], ph(c, "g"));
    CHECK(!((voicing.applies >> ph(c, "'")) & 1u));  // no voiced glottal stop in the pack
}

// ---- Sound changes

TEST(language_sound_change_applies_in_its_environment_only) {
    const Content& c = default_content();
    // t-a-t-a: only the t between vowels voices.
    Language l = one_word(c, word(c, {"t", "a", "t", "a"}));
    CHECK(apply_sound_change(c, l, rule(c, "intervocalic voicing")));
    CHECK(l.words[0].sounds == word(c, {"t", "a", "d", "a"}));
    CHECK_EQ(l.sound_changes.size(), std::size_t{1});
    // Velars palatalize before front vowels only.
    Language k = one_word(c, word(c, {"k", "i", "k", "u"}));
    CHECK(apply_sound_change(c, k, rule(c, "velar palatalization")));
    CHECK(k.words[0].sounds == word(c, {"ch", "i", "k", "u"}));
    // Nothing to change: reported, not logged.
    Language none = one_word(c, word(c, {"m", "a"}));
    CHECK(!apply_sound_change(c, none, rule(c, "intervocalic voicing")));
    CHECK(none.sound_changes.empty());
}

TEST(language_sound_change_never_leaves_a_word_without_a_vowel) {
    const Content& c = default_content();
    Language l = one_word(c, word(c, {"t", "a"}));
    CHECK(!apply_sound_change(c, l, rule(c, "loss of final vowels")));  // would leave "t"
    CHECK(l.words[0].sounds == word(c, {"t", "a"}));
    Language m = one_word(c, word(c, {"t", "a", "m", "a"}));
    CHECK(apply_sound_change(c, m, rule(c, "loss of final vowels")));
    CHECK(m.words[0].sounds == word(c, {"t", "a", "m"}));
}

TEST(language_romanize_capitalizes_names) {
    const Content& c = default_content();
    CHECK_EQ(romanize(c, word(c, {"ë", "y", "u"}), true), std::string("\xC3\x8Byu"));  // Ëyu
    CHECK_EQ(romanize(c, word(c, {"'", "a"}), true), std::string("'A"));
    CHECK_EQ(romanize(c, word(c, {"sh", "a"}), false), std::string("sha"));
}

// ---- Vocabulary change

TEST(language_keeps_about_85_percent_of_core_vocabulary_per_millennium) {
    const Content& c = default_content();
    double total = 0.0;
    constexpr int kRuns = 60;
    for (int k = 0; k < kRuns; ++k) {
        RngStream r(static_cast<std::uint64_t>(k), 0, Stream::Language, 1);
        std::uint64_t cognate = 1;
        const Language start = make_proto_language(c, r, cognate);
        Language l = start;
        for (int y = 0; y < 1000; ++y) {
            RngStream ry(static_cast<std::uint64_t>(k), y, Stream::Language, 2);
            evolve_language(c, l, ry, cognate);
        }
        total += lexical_similarity(l, start);
        CHECK_EQ(lexical_similarity(start, start), 1.0);
    }
    const double kept = total / kRuns;
    CHECK(kept > 0.78);
    CHECK(kept < 0.92);
}

TEST(language_proto_languages_are_unrelated_and_well_formed) {
    const Content& c = default_content();
    std::uint64_t cognate = 1;
    RngStream r(1, 0, Stream::Language, 1);
    const Language a = make_proto_language(c, r, cognate);
    const Language b = make_proto_language(c, r, cognate);
    CHECK_EQ(lexical_similarity(a, b), 0.0);
    CHECK_EQ(a.words.size(), c.concepts.size());
    for (const Word& w : a.words) {
        CHECK(!w.sounds.empty());
        bool vowel = false;
        for (PhonemeId p : w.sounds) vowel |= c.phonemes[p].kind == PhonemeKind::Vowel;
        CHECK(vowel);
    }
    CHECK(!a.name.empty());
}

// ---- Culture traits

TEST(culture_targets_follow_circumstances) {
    const Content& c = default_content();
    const CultureTrait* mobility = nullptr;
    for (const CultureTrait& t : c.traits) {
        if (t.id == "mobility") mobility = &t;
    }
    CHECK(mobility != nullptr);
    std::array<double, kCultureVarCount> inland{}, coast{};
    coast[static_cast<int>(CultureVar::Aquatic)] = 1.0;
    // Rich fishing grounds settle people down.
    CHECK(trait_target(*mobility, coast) < trait_target(*mobility, inland) - 0.3);
    for (const CultureTrait& t : c.traits) {
        std::array<double, kCultureVarCount> all{};
        all.fill(1.0);
        const double x = trait_target(t, all);
        CHECK(x >= 0.0 && x <= 1.0);
    }
}

// ---- Runs

TEST(culture_run_starts_with_diversified_families) {
    WorldParams p;
    p.grid_level = 4;
    const Simulation sim = Simulation::create(p);
    const SimState& s = sim.state();
    check_tree(s, false);
    const PeopleCounts n = count_peoples(s);
    CHECK(n.families >= 20);
    CHECK(n.languages > 3 * n.families);
    CHECK_EQ(n.cultures, n.families);
    // Ancestors are extinct before the run began, after their founding.
    bool ancestor = false;
    for (std::size_t i = 1; i < s.languages.size(); ++i) {
        const Language& l = s.languages[i];
        CHECK(l.founded <= 0);
        if (l.extinct != kNever) {
            ancestor = true;
            CHECK(l.extinct < 0 && l.extinct > l.founded);
            CHECK(l.name.rfind("Proto-", 0) == 0);
        }
    }
    CHECK(ancestor);
    // Sister languages share most of their vocabulary; other families none.
    for (std::size_t i = 1; i < s.languages.size(); ++i) {
        const Language& l = s.languages[i];
        if (l.parent == 0) continue;
        CHECK(lexical_similarity(l, s.languages[l.parent]) > 0.25);
    }
    // Traits are within range.
    for (const Cohort& co : s.cohorts) {
        for (std::size_t t = 0; t < default_content().traits.size(); ++t) {
            CHECK(co.traits[t] >= 0.0 && co.traits[t] <= 1.0);
        }
    }
}

TEST(culture_languages_split_shift_and_die_out_over_centuries) {
    WorldParams p;
    p.grid_level = 4;
    Simulation sim = Simulation::create(p);
    const auto start = count_peoples(sim.state());
    sim.step_years(600);
    const SimState& s = sim.state();
    check_tree(s, true);
    int splits = 0, culture_splits = 0, extinct = 0;
    for (const Event& e : s.events) {
        splits += e.type == EventType::LanguageSplit;
        culture_splits += e.type == EventType::CultureSplit;
        extinct += e.type == EventType::LanguageExtinct;
    }
    CHECK(splits > 0);
    CHECK(culture_splits > 0);
    CHECK(extinct > 0);
    const auto now = count_peoples(s);
    CHECK(now.families <= start.families);  // families can only die out
    CHECK(now.languages > start.languages / 3);
    // History records the counts.
    CHECK_EQ(s.history.back().languages, now.languages);
    CHECK_EQ(s.history.back().cultures, now.cultures);
    // A daughter born in the run starts close to its parent.
    for (const Event& e : s.events) {
        if (e.type != EventType::LanguageSplit) continue;
        const Language& d = s.languages[static_cast<std::size_t>(e.value)];
        CHECK_EQ(d.founded, e.tick);
        CHECK(lexical_similarity(d, s.languages[d.parent]) > 0.6);
        break;
    }
}
