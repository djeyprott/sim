// Languages as evolving objects: sound inventories, core vocabularies,
// regular sound changes, word replacement and names.
//
// A language family starts from a generated proto-language. Each language
// then changes on its own: now and then a sound law from data/phonology.json
// applies to every word at once, and words are replaced by new ones, stable
// meanings (numbers, pronouns) more slowly than others. When a dialect splits
// off (see systems/culture.hpp) the daughter starts as a copy of its parent,
// so related languages share cognates and show regular sound correspondences.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "histsim/content.hpp"
#include "histsim/rng.hpp"
#include "histsim/state.hpp"

namespace histsim {

// Rates (per language per year).
inline constexpr double kSoundChangesPerYear = 1.0 / 350.0;
// Word replacement: concept rate = kReplacementBase * (1 - 0.85 * stability),
// so a language keeps about 85% of its core vocabulary per thousand years
// (glottochronology: 81-86%).
inline constexpr double kReplacementBase = 0.00035;
inline constexpr double kTypologyChangesPerYear = 1.0 / 3000.0;

// A new language family: sounds picked by their frequency among the world's
// languages, syllable shapes, grammar features and a fresh vocabulary.
Language make_proto_language(const Content& content, RngStream& rng, std::uint64_t& next_cognate);

// Sounds a language uses (bit p set for phoneme p): those in its words.
std::uint64_t inventory(const Language& lang);

// A new word in the language's own sound system.
std::vector<PhonemeId> coin_word(const Content& content, std::uint64_t inventory, std::uint32_t syllables,
                                 int min_syllables, int max_syllables, RngStream& rng);

// Apply one sound change to every word. Returns false if it changed nothing.
// A word the change would leave without a vowel or shorter than two sounds
// keeps its old form.
bool apply_sound_change(const Content& content, Language& lang, std::size_t rule);

// One year of change: maybe a sound law, a replaced word, a grammar shift.
void evolve_language(const Content& content, Language& lang, RngStream& rng, std::uint64_t& next_cognate);

// Share of core vocabulary that two languages inherited from the same
// ancestral words (0-1).
double lexical_similarity(const Language& a, const Language& b);

// Romanized text of a word; `capitalize` for names.
std::string romanize(const Content& content, const std::vector<PhonemeId>& sounds, bool capitalize = false);

// A name for a language: its word for "people" (many real languages are
// named so), or, if that is taken by `avoid`, its word for "land".
std::string language_name(const Content& content, const Language& lang, const std::string& avoid = {});

const char* word_order_name(const Content& content, const Language& lang);
const char* morphology_name(const Content& content, const Language& lang);
const char* tone_name(const Content& content, const Language& lang);

}  // namespace histsim
