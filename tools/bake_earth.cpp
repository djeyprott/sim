// Bakes Earth's relief onto the level-7 cell grid (data/earth/elevation_l7.bin).
//
//   histsim_bake_earth ETOPO10_ICE.bin OUT.bin
//
// Input: ETOPO10 (g2e/etopo10 on GitHub, MIT): NOAA's ETOPO1 (public domain)
// resampled to 10 arc-minutes, the ice-surface version (Antarctica and
// Greenland are land), 2161 x 1081 little-endian int16 metres, rows from
// 90 N (the top) down, columns from 180 W, grid-registered.
//
// Each raster point (weighted by its area, cos latitude) goes to the nearest
// cell. A cell is land if at least half its area is above sea level; its
// elevation is the mean of its land (or, for sea, its sea) points. Output:
// one little-endian int16 per cell, metres; land cells are at least +1,
// sea cells at most -1.
//
// This runs once, offline: it may use libm freely, since its output is
// committed and the simulation only reads the integers.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

#include "histsim/grid.hpp"

using histsim::CellId;
using histsim::Grid;
using histsim::Vec3;

namespace {

constexpr int kCols = 2161, kRows = 1081;
constexpr double kStepDeg = 1.0 / 6.0;
constexpr double kPi = 3.14159265358979323846;

double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

CellId nearest_from(const Grid& g, CellId start, const Vec3& p) {
    CellId best = start;
    double best_dot = dot(g.center(best), p);
    for (;;) {
        CellId next = best;
        for (CellId nb : g.neighbors(best)) {
            const double d = dot(g.center(nb), p);
            if (d > best_dot) best_dot = d, next = nb;
        }
        if (next == best) return best;
        best = next;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: histsim_bake_earth ETOPO10_ICE.bin OUT.bin\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<std::int16_t> raster(static_cast<std::size_t>(kCols) * kRows);
    in.read(reinterpret_cast<char*>(raster.data()), static_cast<std::streamsize>(raster.size() * 2));
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    const Grid g = histsim::make_icosahedral_grid(7);
    const std::size_t n = g.cell_count();
    std::vector<double> total(n, 0.0), land(n, 0.0), land_sum(n, 0.0), sea_sum(n, 0.0);
    CellId at = 0;
    for (int r = 0; r < kRows; ++r) {
        const double lat = (90.0 - r * kStepDeg) * kPi / 180.0;
        const double weight = std::cos(lat) + 1e-6;  // the poles still count a little
        for (int c = 0; c < kCols - 1; ++c) {        // the last column repeats the first
            const double lon = (-180.0 + c * kStepDeg) * kPi / 180.0;
            const Vec3 p{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
            at = nearest_from(g, at, p);
            const double z = raster[static_cast<std::size_t>(r) * kCols + c];
            total[at] += weight;
            if (z > 0.0) {
                land[at] += weight;
                land_sum[at] += weight * z;
            } else {
                sea_sum[at] += weight * z;
            }
        }
    }
    std::vector<std::int16_t> out(n, 0);
    std::size_t land_cells = 0, empty = 0;
    for (CellId c = 0; c < n; ++c) {
        if (total[c] <= 0.0) {
            ++empty;
            out[c] = -1;
            continue;
        }
        if (land[c] >= 0.5 * total[c]) {
            ++land_cells;
            out[c] = static_cast<std::int16_t>(std::max(1.0, std::round(land_sum[c] / land[c])));
        } else {
            const double sea = total[c] - land[c];
            out[c] = static_cast<std::int16_t>(std::min(-1.0, std::round(sea_sum[c] / sea)));
        }
    }
    std::ofstream o(argv[2], std::ios::binary);
    for (std::int16_t v : out) {
        const auto u = static_cast<std::uint16_t>(v);
        const char b[2] = {static_cast<char>(u & 0xFF), static_cast<char>(u >> 8)};
        o.write(b, 2);
    }
    std::printf("%zu cells, %zu land (%.1f%%), %zu without raster points\n", n, land_cells,
                100.0 * static_cast<double>(land_cells) / static_cast<double>(n), empty);
    return 0;
}
