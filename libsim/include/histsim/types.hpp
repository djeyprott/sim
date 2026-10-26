// Basic types and constants shared across the simulation core.
#pragma once

#include <cstdint>

namespace histsim {

using CellId = std::uint32_t;
using Tick = std::int64_t;

inline constexpr CellId kInvalidCell = 0xFFFFFFFFu;

// The base tick is one month. Systems run every N ticks (see scheduler.hpp).
inline constexpr Tick kTicksPerYear = 12;

// Planet constants. Procedural worlds use Earth's radius and physics.
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kPlanetRadiusKm = 6371.0;
inline constexpr double kPlanetAreaKm2 = 4.0 * kPi * kPlanetRadiusKm * kPlanetRadiusKm;

}  // namespace histsim
