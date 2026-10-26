// Interventions ("god mode"): changes a user makes to a running world.
//
// Each intervention is a Command applied through Simulation::apply(). It
// takes effect immediately, is recorded in SimState::commands with the tick it
// was applied at, and uses only deterministic arithmetic and the run's own
// random streams. So a run with interventions saves, hashes and replays
// exactly like any other: same seed + same commands at the same ticks = same
// history.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "histsim/state.hpp"
#include "histsim/world.hpp"

namespace histsim {

struct InterventionResult {
    bool ok = false;
    std::string message;              // one line for the user, e.g. "Plague killed 1,204 people in 7 cells."
    std::int64_t people = 0;          // people killed or added
    std::size_t cells = 0;            // cells affected
};

// Limits applied to command parameters (values outside are clamped).
inline constexpr double kMaxRadiusKm = 5000.0;
inline constexpr double kMaxEruptionWm2 = 30.0;
inline constexpr double kMaxImposedForcingWm2 = 20.0;
inline constexpr double kMaxRegionalTempC = 20.0;
inline constexpr std::int64_t kMaxModifierYears = 500;
inline constexpr std::int64_t kMaxSettlers = 1000000;

// Cells whose centers lie within radius_km of the center cell (the center
// always included), with a weight: 1 inside 70% of the radius, falling
// linearly to 0 at the edge when `taper` is set, else 1 throughout.
struct WeightedCell {
    CellId cell;
    double weight;
};
std::vector<WeightedCell> cells_within(const World& world, CellId center, double radius_km, bool taper);

// Applies the command to the state (called by Simulation::apply).
InterventionResult apply_command(const World& world, SimState& state, std::uint64_t seed, Command command);

// Short human-readable name of a command type, e.g. "Plague".
const char* command_name(CommandType type);

}  // namespace histsim
