#include "histsim/systems/conditions.hpp"

#include <algorithm>

#include "histsim/parallel.hpp"

namespace histsim {

void ConditionsSystem::step(StepContext& ctx) {
    const World& w = ctx.world;
    SimState& s = ctx.state;
    const std::size_t n = w.cell_count();
    Conditions& k = s.conditions;
    for (std::vector<float>* v : {&k.food, &k.danger, &k.burden, &k.bondage, &k.sickness, &k.room, &k.town_gap,
                                  &k.slow_food, &k.slow_danger, &k.slow_burden, &k.slow_sickness}) {
        if (v->size() != n) v->assign(n, 0.0f);
    }
    const auto at = [](const auto& v, CellId c) { return c < v.size() ? static_cast<double>(v[c]) : 0.0; };
    parallel_for(n, [&](std::size_t c_begin, std::size_t c_end) {
        for (CellId c = static_cast<CellId>(c_begin); c < c_end; ++c) {
            const double people = static_cast<double>(s.cell_population[c]);
            if (people <= 0.0) {
                // No one to live through anything; slow averages start
                // afresh when people come.
                k.food[c] = k.slow_food[c] = 1.0f;
                k.danger[c] = k.burden[c] = k.bondage[c] = k.sickness[c] = 0.0f;
                k.slow_danger[c] = k.slow_burden[c] = k.slow_sickness[c] = 0.0f;
                k.room[c] = static_cast<float>(std::max(0.0, at(s.env.capacity, c) - at(s.food_import, c)));
                k.town_gap[c] = 0.0f;
                continue;
            }
            // The capacity after markets includes food brought in and stores
            // drawn on: what people actually have to eat this year.
            const double fed = at(s.env.capacity, c);
            const double food = std::min(kMaxFood, fed / people);
            const double own = std::max(0.0, fed - at(s.food_import, c));  // what the land itself feeds
            const double taken = at(s.food_taken, c);
            const double burden = taken > 0.0 ? std::min(1.0, taken / std::max(1.0, own + taken)) : 0.0;
            double bonded = 0.0;
            for (auto i = s.cohort_begin[c]; i < s.cohort_begin[c + 1]; ++i) {
                bonded += s.cohorts[i].bonded * static_cast<double>(s.cohorts[i].total());
            }
            const double town = at(s.urban, c) * people, would = at(s.urban_target, c) * people;
            k.food[c] = static_cast<float>(food);
            k.danger[c] = static_cast<float>(std::min(1.0, at(s.war_deaths, c) / people));
            k.burden[c] = static_cast<float>(burden);
            k.bondage[c] = static_cast<float>(std::min(1.0, bonded / people));
            k.sickness[c] = static_cast<float>(std::min(1.0, at(s.disease_deaths, c) / people));
            k.room[c] = static_cast<float>(own - people);
            k.town_gap[c] = static_cast<float>(would - town);
            const auto slow = [](float& avg, double now) { avg = static_cast<float>(avg + kSlowRate * (now - avg)); };
            slow(k.slow_food[c], food);
            slow(k.slow_danger[c], k.danger[c]);
            slow(k.slow_burden[c], burden);
            slow(k.slow_sickness[c], k.sickness[c]);
        }
    }, 1024);
    // Food taken is counted afresh until the next gathering.
    std::fill(s.food_taken.begin(), s.food_taken.end(), 0.0);
}

}  // namespace histsim
