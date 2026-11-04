// Historical figures: helpers shared by the systems that record them
// (rulers and their families, inventors, prophets). See Figure in state.hpp.
#pragma once

#include <cstdint>
#include <string>

#include "histsim/types.hpp"

#include "histsim/content.hpp"
#include "histsim/rng.hpp"
#include "histsim/state.hpp"

namespace histsim {

// Figures die at the forager life table's rates times this (they eat well).
inline constexpr double kFigureMortality = 0.8;

// Appends a figure, giving it the next id; returns the id.
std::uint32_t add_figure(SimState& s, Figure f);

// A personal name in a language.
std::string figure_name(const Content& content, const Language& lang, RngStream& rng);

// When someone born at `born` and alive at `now` dies, drawn year by year
// from the life table (always after `now`).
Tick draw_death(Tick born, Tick now, RngStream& rng);

// A figure of the people living at `home` (its largest cohort's culture and
// language, the polity ruling it), aged `min_age` to `max_age` at `now`,
// with the given roles; returns its id. Its name is `name` if given.
std::uint32_t record_person(SimState& s, const Content& content, CellId home, Tick now, int min_age, int max_age,
                            std::uint16_t roles, RngStream& rng, const std::string& name = {});

// The person ruling a polity now, or null.
const Figure* reigning_person(const SimState& s, std::uint32_t polity);

// How a ruler's trait scales a chance: 0.5 + trait (1 on average, from 0.5
// to 1.5), or 1 without a ruler. Traits are uniform, so rates stay the same
// overall and only differ from reign to reign.
inline double trait_factor(const Figure* f, double Figure::*trait) { return f != nullptr ? 0.5 + f->*trait : 1.0; }

// What a figure most wants, from its roles and traits.
FigureGoal figure_goal(const Figure& f);

const char* goal_name(FigureGoal g);
bool figure_alive(const Figure& f, Tick now);
int figure_age(const Figure& f, Tick now);

}  // namespace histsim
