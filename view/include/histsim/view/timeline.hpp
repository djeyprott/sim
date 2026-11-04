// The timeline: what happened, in words, newest first.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "histsim/simulation.hpp"

namespace histsim::view {

// Kinds of entry, for filtering: "realms" (polities founded, ended,
// subjugated, cities, treks), "war" (wars, alliances, raids), "peoples"
// (languages, cultures, migrations), "faith", "nature" (eruptions, cold
// spells, diseases, god mode) and "discovery" (domestications, inventions).
const char* event_kind(EventType type);

// One line about an event ("The Ujukut set out from ... with 88,390
// people"), or empty for events not worth a line.
std::string describe_event(const Simulation& sim, const Event& event);

// Whether an event concerns a polity (founded, ended, at war, allied,
// subjugated, converted, on the move, raiding...).
bool event_involves(const SimState& state, const Event& event, std::uint32_t polity);

struct TimelineEntry {
    std::int64_t year = 0;
    std::string kind;
    std::string text;
    CellId cell = kInvalidCell;
    std::uint32_t figure = 0;  // the person it is about (SimState::figures), 0 if none
};

// The figure an event is about (a ruler crowned, a claimant, an inventor, a
// prophet), 0 if none.
std::uint32_t event_figure(const SimState& state, const Event& event);

// Up to `max_entries` entries, newest first: of the given kind ("" for all
// but the most frequent: raids, newly settled land and ordinary
// successions), about the given polity (0 for any; then successions are
// included), at the given cell (kInvalidCell for anywhere; at a cell, raids
// and settling are included).
std::vector<TimelineEntry> timeline(const Simulation& sim, std::string_view kind, std::uint32_t polity, CellId cell,
                                    std::size_t max_entries);

// Where a cell is on the flat map (0-1 across from 180 W and down from the
// north pole), for centering the view on it.
void cell_uv(const World& world, CellId cell, double& u, double& v);

}  // namespace histsim::view
