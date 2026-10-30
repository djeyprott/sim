#include "histsim/language.hpp"

#include <algorithm>

namespace histsim {

namespace {

bool is_vowel(const Content& c, PhonemeId p) { return c.phonemes[p].kind == PhonemeKind::Vowel; }

// Pick an index with probability proportional to weight; weights >= 0 with a
// positive sum.
template <typename F>
std::size_t weighted_pick(std::size_t n, F&& weight, RngStream& rng) {
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) total += weight(i);
    double x = rng.uniform() * total;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = weight(i);
        if (x < w) return i;
        x -= w;
    }
    // Rounding: the last option with any weight.
    for (std::size_t i = n; i-- > 0;) {
        if (weight(i) > 0.0) return i;
    }
    return 0;
}

std::uint8_t pick_option(const WeightedOptions& o, RngStream& rng) {
    return static_cast<std::uint8_t>(
        weighted_pick(o.options.size(), [&](std::size_t i) { return o.options[i].second; }, rng));
}

double replacement_rate(const Concept& k) { return kReplacementBase * (1.0 - 0.85 * k.stability); }

bool env_holds(const Content& c, const std::vector<PhonemeId>& w, std::size_t i, SoundEnv env) {
    const std::size_t n = w.size();
    const bool has_prev = i > 0, has_next = i + 1 < n;
    switch (env) {
        case SoundEnv::Anywhere: return true;
        case SoundEnv::WordInitial: return i == 0;
        case SoundEnv::WordFinal: return i + 1 == n;
        case SoundEnv::BetweenVowels: return has_prev && has_next && is_vowel(c, w[i - 1]) && is_vowel(c, w[i + 1]);
        case SoundEnv::BeforeFrontVowel:
            return has_next && is_vowel(c, w[i + 1]) && c.phonemes[w[i + 1]].backness == Backness::Front;
        case SoundEnv::BeforeHighFrontVowel:
            return has_next && is_vowel(c, w[i + 1]) && c.phonemes[w[i + 1]].backness == Backness::Front &&
                   c.phonemes[w[i + 1]].height == Height::High;
        case SoundEnv::BeforeConsonant: return has_next && !is_vowel(c, w[i + 1]);
        case SoundEnv::AfterVowel: return has_prev && is_vowel(c, w[i - 1]);
        case SoundEnv::BeforeVelar:
            return has_next && !is_vowel(c, w[i + 1]) && c.phonemes[w[i + 1]].place == Place::Velar;
    }
    return false;
}

}  // namespace

std::uint64_t inventory(const Language& lang) {
    std::uint64_t m = 0;
    for (const Word& w : lang.words) {
        for (PhonemeId p : w.sounds) m |= std::uint64_t{1} << p;
    }
    return m;
}

std::vector<PhonemeId> coin_word(const Content& c, std::uint64_t inv, std::uint32_t syllables, int min_syl,
                                 int max_syl, RngStream& rng) {
    const auto span = static_cast<std::uint32_t>(max_syl - min_syl + 1);
    const int count = min_syl + static_cast<int>(rng.below(span));
    // A sound of the right kind from the inventory, not repeating the one
    // before. If none qualifies (sound changes can shrink an inventory a
    // lot), relax: allow the repeat, then any sound of that kind.
    auto pick_sound = [&](bool vowel, PhonemeId avoid) {
        const auto np = c.phonemes.size();
        for (int level = 0; level < 3; ++level) {
            auto weight = [&](std::size_t p) {
                if (is_vowel(c, static_cast<PhonemeId>(p)) != vowel) return 0.0;
                if (level < 2 && !((inv >> p) & 1u)) return 0.0;
                if (level < 1 && p == avoid) return 0.0;
                return c.phonemes[p].frequency + 1e-3;
            };
            double total = 0.0;
            for (std::size_t p = 0; p < np; ++p) total += weight(p);
            if (total > 0.0) return static_cast<PhonemeId>(weighted_pick(np, weight, rng));
        }
        return static_cast<PhonemeId>(0);  // unreachable: the pack has both kinds
    };
    std::vector<PhonemeId> word;
    for (int s = 0; s < count; ++s) {
        // Shapes: CV is the most common everywhere, so it gets triple weight.
        const std::size_t shape = weighted_pick(
            c.syllables.size(),
            [&](std::size_t i) {
                if (!((syllables >> i) & 1u)) return 0.0;
                // A vowel-initial syllable after a vowel would make a hiatus.
                if (!word.empty() && is_vowel(c, word.back()) && c.syllables[i].shape[0] == 'V') return 0.0;
                return i == 0 ? 3.0 : 1.0;
            },
            rng);
        for (char slot : c.syllables[shape].shape) {
            const PhonemeId prev = word.empty() ? static_cast<PhonemeId>(255) : word.back();
            word.push_back(pick_sound(slot == 'V', prev));
        }
    }
    return word;
}

Language make_proto_language(const Content& c, RngStream& rng, std::uint64_t& next_cognate) {
    Language lang;
    const std::size_t np = c.phonemes.size();
    // Sounds: each with its cross-linguistic frequency, topped up to at least
    // three vowels and six consonants with the most common missing ones.
    std::uint64_t inv = 0;
    int vowels = 0, consonants = 0;
    for (std::size_t p = 0; p < np; ++p) {
        if (rng.uniform() < c.phonemes[p].frequency) {
            inv |= std::uint64_t{1} << p;
            (is_vowel(c, static_cast<PhonemeId>(p)) ? vowels : consonants) += 1;
        }
    }
    auto top_up = [&](bool vowel, int& have, int need) {
        while (have < need) {
            std::size_t best = np;
            for (std::size_t p = 0; p < np; ++p) {
                if ((inv >> p) & 1u || is_vowel(c, static_cast<PhonemeId>(p)) != vowel) continue;
                if (best == np || c.phonemes[p].frequency > c.phonemes[best].frequency) best = p;
            }
            if (best == np) return;
            inv |= std::uint64_t{1} << best;
            ++have;
        }
    };
    top_up(true, vowels, 3);
    top_up(false, consonants, 6);

    lang.syllables = 1;  // CV
    for (std::size_t i = 1; i < c.syllables.size(); ++i) {
        if (rng.uniform() < c.syllables[i].probability) lang.syllables |= 1u << i;
    }
    lang.word_order = pick_option(c.word_order, rng);
    lang.morphology = pick_option(c.morphology, rng);
    lang.tone = pick_option(c.tone, rng);

    lang.words.resize(c.concepts.size());
    for (std::size_t k = 0; k < c.concepts.size(); ++k) {
        lang.words[k].sounds =
            coin_word(c, inv, lang.syllables, c.concepts[k].min_syllables, c.concepts[k].max_syllables, rng);
        lang.words[k].cognate = next_cognate++;
    }
    lang.name = language_name(c, lang);
    return lang;
}

bool apply_sound_change(const Content& c, Language& lang, std::size_t rule) {
    const SoundChange& r = c.sound_changes[rule];
    bool changed_any = false;
    std::vector<PhonemeId> out;
    for (Word& word : lang.words) {
        const std::vector<PhonemeId>& w = word.sounds;
        out.clear();
        bool changed = false;
        // Conditions are read from the old form, so the change applies to all
        // positions at once, as a sound law does.
        for (std::size_t i = 0; i < w.size(); ++i) {
            const bool hit = ((r.applies >> w[i]) & 1u) && env_holds(c, w, i, r.where);
            if (!hit) {
                out.push_back(w[i]);
                continue;
            }
            changed = true;
            switch (r.action) {
                case SoundChange::Action::Shift: out.push_back(r.shift_to[w[i]]); break;
                case SoundChange::Action::Delete: break;
                case SoundChange::Action::InsertAfter:
                    out.push_back(w[i]);
                    out.push_back(r.insert);
                    break;
            }
        }
        if (!changed) continue;
        const bool has_vowel = std::any_of(out.begin(), out.end(), [&](PhonemeId p) { return is_vowel(c, p); });
        if (out.size() < 2 || !has_vowel) continue;  // too much would be lost: this word resists
        word.sounds = out;
        changed_any = true;
    }
    if (changed_any) lang.sound_changes.push_back(static_cast<std::uint16_t>(rule));
    return changed_any;
}

void evolve_language(const Content& c, Language& lang, RngStream& rng, std::uint64_t& next_cognate) {
    // A sound law: tried among the rules that touch sounds the language has.
    if (rng.uniform() < kSoundChangesPerYear) {
        const std::uint64_t inv = inventory(lang);
        for (int attempt = 0; attempt < 4; ++attempt) {
            const std::size_t rule = weighted_pick(
                c.sound_changes.size(),
                [&](std::size_t i) { return (c.sound_changes[i].applies & inv) ? c.sound_changes[i].weight : 0.0; },
                rng);
            if ((c.sound_changes[rule].applies & inv) && apply_sound_change(c, lang, rule)) break;
        }
    }
    // A replaced word. The chance of two in one year is negligible, so draw
    // once against the total rate, then pick the meaning by its rate.
    double total = 0.0;
    for (const Concept& k : c.concepts) total += replacement_rate(k);
    if (rng.uniform() < total) {
        const std::size_t k = weighted_pick(
            c.concepts.size(), [&](std::size_t i) { return replacement_rate(c.concepts[i]); }, rng);
        lang.words[k].sounds = coin_word(c, inventory(lang), lang.syllables, c.concepts[k].min_syllables,
                                         c.concepts[k].max_syllables, rng);
        lang.words[k].cognate = next_cognate++;
    }
    // Grammar drifts rarely.
    if (rng.uniform() < kTypologyChangesPerYear) {
        switch (rng.below(3)) {
            case 0: lang.word_order = pick_option(c.word_order, rng); break;
            case 1: lang.morphology = pick_option(c.morphology, rng); break;
            default: lang.tone = pick_option(c.tone, rng); break;
        }
    }
}

double lexical_similarity(const Language& a, const Language& b) {
    const std::size_t n = std::min(a.words.size(), b.words.size());
    if (n == 0) return 0.0;
    std::size_t same = 0;
    for (std::size_t k = 0; k < n; ++k) same += a.words[k].cognate == b.words[k].cognate ? 1 : 0;
    return static_cast<double>(same) / static_cast<double>(n);
}

std::string romanize(const Content& c, const std::vector<PhonemeId>& sounds, bool capitalize) {
    std::string s;
    for (PhonemeId p : sounds) s += c.phonemes[p].text;
    if (capitalize) {
        // Skip a leading glottal stop mark, then capitalize the first letter:
        // ASCII, or a Latin-1 letter in UTF-8 (C3 A0-BE -> C3 80-9E, e.g. ë -> Ë).
        for (std::size_t i = 0; i < s.size(); ++i) {
            auto ch = static_cast<unsigned char>(s[i]);
            if (ch >= 'a' && ch <= 'z') {
                s[i] = static_cast<char>(ch - 'a' + 'A');
                break;
            }
            if (ch == 0xC3 && i + 1 < s.size()) {
                const auto next = static_cast<unsigned char>(s[i + 1]);
                if (next >= 0xA0 && next <= 0xBE && next != 0xB7) s[i + 1] = static_cast<char>(next - 0x20);
                break;
            }
            if (ch != '\'') break;
        }
    }
    return s;
}

std::string language_name(const Content& c, const Language& lang, const std::string& avoid) {
    std::string name = romanize(c, lang.words[c.people_concept].sounds, true);
    if (name == avoid) name = romanize(c, lang.words[c.land_concept].sounds, true);
    if (name == avoid) name += romanize(c, lang.words[c.river_concept].sounds, false);
    return name;
}

const char* word_order_name(const Content& c, const Language& l) {
    return c.word_order.options[l.word_order].first.c_str();
}
const char* morphology_name(const Content& c, const Language& l) {
    return c.morphology.options[l.morphology].first.c_str();
}
const char* tone_name(const Content& c, const Language& l) { return c.tone.options[l.tone].first.c_str(); }

}  // namespace histsim
