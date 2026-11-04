// Pages about historical figures (SimState::figures): who they were, what
// they did, and their families, in words, with links to the people named.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "histsim/simulation.hpp"

namespace histsim::view {

struct FigureLine {
    std::string label;  // "Born", "Father", "Year 3120", ...
    std::string text;
    std::uint32_t figure = 0;  // a person the line is about, to link to (0: none)
    CellId cell = kInvalidCell;  // a place to show, if any
};

struct FigurePage {
    std::uint32_t id = 0;
    std::string title;     // "Ashur II of the House of Nuru"
    std::string subtitle;  // "Ruler of Kish, year 3120-3151; able, warlike"
    std::vector<FigureLine> life, family, deeds;
};

// The name to show: a ruler's with its regnal number.
std::string figure_title(const SimState& state, std::uint32_t id);

// Everything about one figure (an empty page for an unknown id).
FigurePage figure_page(const Simulation& sim, std::uint32_t id);

// The figures most worth reading about now, at most `max`: the rulers of
// the largest realms, then the teachers of living faiths and the most
// recent inventors.
std::vector<std::uint32_t> notable_figures(const Simulation& sim, std::size_t max);

// The person ruling a polity now (0 if none).
std::uint32_t polity_ruler_figure(const SimState& state, std::uint32_t polity);

}  // namespace histsim::view
