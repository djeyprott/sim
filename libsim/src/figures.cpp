#include "histsim/figures.hpp"

#include <algorithm>

#include "histsim/language.hpp"
#include "systems/vital_rates.inc"

namespace histsim {

std::uint32_t add_figure(SimState& s, Figure f) {
    if (s.figures.empty()) s.figures.emplace_back();  // figure 0 is no one
    f.id = static_cast<std::uint32_t>(s.figures.size());
    s.figures.push_back(std::move(f));
    return s.figures.back().id;
}

std::string figure_name(const Content& content, const Language& lang, RngStream& rng) {
    return romanize(content, coin_word(content, inventory(lang), lang.syllables, 2, 3, rng), true);
}

Tick draw_death(Tick born, Tick now, RngStream& rng) {
    constexpr int kOldest = 110;
    int age = static_cast<int>(std::max<Tick>(0, now - born) / kTicksPerYear);
    for (; age < kOldest; ++age) {
        const int bin = std::min(age / 5, 20);
        if (rng.uniform() < vital::kForagerDeathProb[bin] * kFigureMortality) break;
    }
    // Some time in that year of life, and after `now`.
    const Tick at = born + static_cast<Tick>(age) * kTicksPerYear + static_cast<Tick>(rng.uniform() * kTicksPerYear);
    return std::max(at, now + 1);
}

std::uint32_t record_person(SimState& s, const Content& content, CellId home, Tick now, int min_age, int max_age,
                            std::uint16_t roles, RngStream& rng, const std::string& name) {
    Figure f;
    const Cohort* lead = nullptr;
    for (const Cohort& co : s.cohorts_in(home)) {
        if (lead == nullptr || co.total() > lead->total()) lead = &co;
    }
    if (lead != nullptr) {
        f.culture = lead->culture;
        f.language = lead->language;
    }
    f.home = home;
    f.polity = home < s.polity_of.size() ? s.polity_of[home] : 0;
    f.female = rng.uniform() < 0.1;
    f.born = now - static_cast<Tick>((min_age + rng.uniform() * (max_age - min_age)) * kTicksPerYear);
    f.died = draw_death(f.born, now, rng);
    f.name = !name.empty() || f.language >= s.languages.size() ? name : figure_name(content, s.languages[f.language], rng);
    f.roles = roles;
    f.ability = rng.uniform();
    f.martial = rng.uniform();
    f.piety = rng.uniform();
    f.ambition = rng.uniform();
    if (roles & kRoleProphet) f.piety = 0.7 + 0.3 * f.piety;
    f.goal = figure_goal(f);
    return add_figure(s, std::move(f));
}

const Figure* reigning_person(const SimState& s, std::uint32_t polity) {
    if (polity == 0 || polity >= s.polities.size()) return nullptr;
    const std::uint32_t r = s.polities[polity].ruler;
    if (r == 0 || r >= s.rulers.size()) return nullptr;
    const std::uint32_t f = s.rulers[r].figure;
    return f != 0 && f < s.figures.size() ? &s.figures[f] : nullptr;
}

FigureGoal figure_goal(const Figure& f) {
    if (f.roles & kRoleProphet) return FigureGoal::Faith;
    if (f.roles & kRoleInventor) return FigureGoal::Knowledge;
    if (f.roles & (kRoleClaimant | kRoleUsurper)) return FigureGoal::Power;
    // The strongest leaning, if any is strong.
    FigureGoal goal = FigureGoal::Family;
    double best = 0.65;
    auto consider = [&](double v, FigureGoal g) {
        if (v > best) best = v, goal = g;
    };
    if (f.roles & kRoleRuler) {
        consider(f.martial, FigureGoal::Conquest);
        consider(f.ability, FigureGoal::Prosperity);
    }
    consider(f.piety, FigureGoal::Faith);
    consider(f.ambition, FigureGoal::Power);
    return goal;
}

const char* goal_name(FigureGoal g) {
    switch (g) {
        case FigureGoal::None: return "";
        case FigureGoal::Conquest: return "conquest";
        case FigureGoal::Prosperity: return "a prosperous realm";
        case FigureGoal::Faith: return "faith";
        case FigureGoal::Power: return "power";
        case FigureGoal::Knowledge: return "knowledge";
        case FigureGoal::Family: return "the family's future";
    }
    return "";
}

bool figure_alive(const Figure& f, Tick now) { return f.id != 0 && f.born <= now && (f.died == kNever || now < f.died); }

int figure_age(const Figure& f, Tick now) {
    const Tick end = f.died == kNever ? now : std::min(now, f.died);
    return static_cast<int>((end - f.born) / kTicksPerYear);
}

}  // namespace histsim
