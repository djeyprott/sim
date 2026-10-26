// The planet's surface as a geodesic grid of roughly equal-area cells.
//
// Grid is plain data, so any generator can fill it: the built-in icosahedral
// grid below, or later an H3 or imported grid. Systems only see cell centers,
// areas and neighbor lists, never how the grid was built.
//
// Cell areas are not exactly equal: on the icosahedral grid the largest cell
// is about 1.8 times the smallest (similar to H3). Systems must always use
// area_km2() rather than assume a fixed cell size. A true equal-area layout
// (Snyder's ISEA projection) is a possible later upgrade.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "histsim/types.hpp"

namespace histsim {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

class Grid {
public:
    [[nodiscard]] std::size_t cell_count() const noexcept { return center_.size(); }

    // Unit vector from the planet's center to the cell center. z is the spin
    // axis, so z equals the sine of latitude. Sim code should use this rather
    // than latitude/longitude, which need non-portable trig functions.
    [[nodiscard]] const Vec3& center(CellId c) const noexcept { return center_[c]; }

    [[nodiscard]] double area_km2(CellId c) const noexcept { return area_km2_[c]; }

    // Neighbors in counter-clockwise order around the cell (seen from outside
    // the planet). Six for most cells, five for the twelve pentagons.
    [[nodiscard]] std::span<const CellId> neighbors(CellId c) const noexcept {
        return {neighbor_ids_.data() + neighbor_offset_[c], neighbor_ids_.data() + neighbor_offset_[c + 1]};
    }

    // Great-circle distance between two cell centers, in km. Uses only
    // arithmetic and sqrt (accurate to about 1e-7 relative at any angle).
    [[nodiscard]] double distance_km(CellId a, CellId b) const noexcept;

    // Subdivision level and a hash of the full geometry (for world-file checks).
    [[nodiscard]] int level() const noexcept { return level_; }
    [[nodiscard]] std::uint64_t geometry_hash() const noexcept;

    // Display helpers (degrees). They use libm trig, so their results may differ
    // in the last bits across platforms: never feed them into simulation state.
    [[nodiscard]] double latitude_deg_display(CellId c) const noexcept;
    [[nodiscard]] double longitude_deg_display(CellId c) const noexcept;

private:
    friend Grid make_icosahedral_grid(int level);

    int level_ = 0;
    std::vector<Vec3> center_;
    std::vector<double> area_km2_;
    std::vector<std::uint32_t> neighbor_offset_;  // CSR offsets, size cell_count + 1
    std::vector<CellId> neighbor_ids_;
};

// Hexagonal grid dual to an icosahedron subdivided `level` times.
// Cell count is 10 * 4^level + 2:
//   level 5:     10,242 cells, ~49,800 km^2 each (fast tests)
//   level 7:    163,842 cells,  ~3,100 km^2 each
//   level 8:    655,362 cells,    ~780 km^2 each
// Built with + - * / and sqrt only, so it is bit-identical on every platform.
Grid make_icosahedral_grid(int level);

}  // namespace histsim
