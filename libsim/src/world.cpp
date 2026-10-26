#include "histsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <deque>

#include "histsim/hash.hpp"
#include "histsim/rng.hpp"

namespace histsim {

namespace {

// Value noise on a 3D integer lattice. Lattice values come from Philox, so the
// noise depends only on the seed. Only + - * / and floor are used.
double lattice_value(std::uint64_t seed, std::uint32_t octave, std::int64_t ix, std::int64_t iy,
                     std::int64_t iz) {
    const PhiloxCounter ctr{static_cast<std::uint32_t>(ix), static_cast<std::uint32_t>(iy),
                            static_cast<std::uint32_t>(iz), 0x57A70000u | octave};
    const PhiloxKey key{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32)};
    return static_cast<double>(philox4x32_10(ctr, key)[0]) * 0x1.0p-32;
}

double smooth(double t) { return t * t * (3.0 - 2.0 * t); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }

double value_noise(std::uint64_t seed, std::uint32_t octave, double x, double y, double z) {
    const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const auto ix = static_cast<std::int64_t>(fx), iy = static_cast<std::int64_t>(fy),
               iz = static_cast<std::int64_t>(fz);
    const double tx = smooth(x - fx), ty = smooth(y - fy), tz = smooth(z - fz);
    auto v = [&](int dx, int dy, int dz) { return lattice_value(seed, octave, ix + dx, iy + dy, iz + dz); };
    const double x00 = lerp(v(0, 0, 0), v(1, 0, 0), tx);
    const double x10 = lerp(v(0, 1, 0), v(1, 1, 0), tx);
    const double x01 = lerp(v(0, 0, 1), v(1, 0, 1), tx);
    const double x11 = lerp(v(0, 1, 1), v(1, 1, 1), tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

double fractal_noise(std::uint64_t seed, const Vec3& p) {
    double sum = 0.0, amplitude = 1.0, total = 0.0, frequency = 1.6;
    for (std::uint32_t octave = 0; octave < 7; ++octave) {
        // Offset each octave so lattice points do not line up.
        const double off = 17.0 * static_cast<double>(octave);
        sum += amplitude * value_noise(seed, octave, p.x * frequency + off, p.y * frequency - off,
                                       p.z * frequency + 0.5 * off);
        total += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return sum / total;  // in [0, 1)
}


// Starting forager groups: Voronoi-like regions grown by breadth-first search
// over land from random seed cells, so each group is contiguous and an
// island no seed reaches becomes a group of its own. About one group per 40
// land cells at the default grid level.
void assign_initial_groups(World& w) {
    const std::size_t n = w.cell_count();
    std::vector<CellId> land;
    for (CellId c = 0; c < n; ++c) {
        if (w.is_land[c]) land.push_back(c);
    }
    w.initial_group.assign(n, 0);
    w.group_count = 0;
    if (land.empty()) return;

    const std::size_t target = std::max<std::size_t>(4, land.size() / 40);
    RngStream rng(w.params.seed, 0, Stream::WorldGen, 1);
    std::deque<CellId> queue;
    for (std::size_t i = 0; i < target; ++i) {
        const CellId c = land[rng.below(static_cast<std::uint32_t>(land.size()))];
        if (w.initial_group[c] == 0) {
            w.initial_group[c] = ++w.group_count;
            queue.push_back(c);
        }
    }
    auto grow = [&] {
        while (!queue.empty()) {
            const CellId c = queue.front();
            queue.pop_front();
            for (CellId nb : w.grid.neighbors(c)) {
                if (w.is_land[nb] && w.initial_group[nb] == 0) {
                    w.initial_group[nb] = w.initial_group[c];
                    queue.push_back(nb);
                }
            }
        }
    };
    grow();
    for (CellId c : land) {  // islands no seed reached
        if (w.initial_group[c] == 0) {
            w.initial_group[c] = ++w.group_count;
            queue.push_back(c);
            grow();
        }
    }
}

// For each fine cell, the three nearest coarse climate cells, weighted by
// inverse squared distance. Arithmetic only, so identical everywhere.
void build_climate_mapping(World& w) {
    w.climate_grid = make_icosahedral_grid(kClimateGridLevel);
    const std::size_t n = w.cell_count();
    w.climate_cells.resize(n);
    w.climate_weights.resize(n);
    for (CellId c = 0; c < n; ++c) {
        const Vec3& p = w.grid.center(c);
        std::array<CellId, 3> best{0, 0, 0};
        std::array<double, 3> best_d2{9.0, 9.0, 9.0};
        for (CellId k = 0; k < w.climate_grid.cell_count(); ++k) {
            const Vec3& q = w.climate_grid.center(k);
            const double dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            // Keep the three smallest; ties go to the lower id, which is stable.
            for (int slot = 0; slot < 3; ++slot) {
                if (d2 < best_d2[slot]) {
                    for (int j = 2; j > slot; --j) {
                        best_d2[j] = best_d2[j - 1];
                        best[j] = best[j - 1];
                    }
                    best_d2[slot] = d2;
                    best[slot] = k;
                    break;
                }
            }
        }
        double total = 0.0;
        std::array<double, 3> weight{};
        for (int i = 0; i < 3; ++i) {
            weight[i] = 1.0 / (best_d2[i] + 1e-6);
            total += weight[i];
        }
        for (int i = 0; i < 3; ++i) weight[i] /= total;
        w.climate_cells[c] = best;
        w.climate_weights[c] = weight;
    }
}

}  // namespace

double forager_density(double temperature_c, double precipitation_mm) {
    const double warmth = std::clamp((temperature_c + 8.0) / 30.0, 0.0, 1.0);
    const double wetness = std::clamp((precipitation_mm - 100.0) / 1100.0, 0.0, 1.0);
    return 0.2 * warmth * wetness;
}

World generate_test_world(const WorldParams& params) {
    World w;
    w.params = params;
    w.grid = make_icosahedral_grid(params.grid_level);
    const std::size_t n = w.grid.cell_count();

    // --- Elevation and sea level -------------------------------------------
    std::vector<double> raw(n);
    for (CellId c = 0; c < n; ++c) {
        raw[c] = fractal_noise(params.seed, w.grid.center(c));
    }
    std::vector<double> sorted = raw;
    const auto k = static_cast<std::size_t>(params.ocean_fraction * static_cast<double>(n - 1));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(k), sorted.end());
    const double sea = sorted[k];  // the k-th smallest value is unique, whatever the sort
    const double lo = *std::min_element(raw.begin(), raw.end());
    const double hi = *std::max_element(raw.begin(), raw.end());

    w.elevation_m.resize(n);
    w.is_land.resize(n);
    for (CellId c = 0; c < n; ++c) {
        if (raw[c] > sea) {
            const double t = (raw[c] - sea) / (hi - sea);
            w.elevation_m[c] = 6000.0 * t * t;  // most land low, a few high ranges
            w.is_land[c] = 1;
        } else {
            const double t = (sea - raw[c]) / (sea - lo);
            w.elevation_m[c] = -200.0 - 5000.0 * std::sqrt(t);  // shelf, then deep ocean
            w.is_land[c] = 0;
        }
    }

    // --- Distance from the coast (breadth-first from all ocean cells) --------
    constexpr std::uint32_t kUnreached = 0xFFFFFFFFu;
    w.coast_distance.assign(n, kUnreached);
    std::deque<CellId> queue;
    for (CellId c = 0; c < n; ++c) {
        if (!w.is_land[c]) {
            w.coast_distance[c] = 0;
            queue.push_back(c);
        }
    }
    while (!queue.empty()) {
        const CellId c = queue.front();
        queue.pop_front();
        for (CellId nb : w.grid.neighbors(c)) {
            if (w.coast_distance[nb] == kUnreached) {
                w.coast_distance[nb] = w.coast_distance[c] + 1;
                queue.push_back(nb);
            }
        }
    }
    for (std::uint32_t& d : w.coast_distance) {
        if (d == kUnreached) {
            d = 0;  // only possible on an all-land world
        }
    }

    // --- Baseline climate -----------------------------------------------------
    // z is the sine of latitude, so no trig is needed.
    // Coast distance is measured in cells; scale by cell size so the climate
    // looks the same at every grid level.
    const double cell_km = std::sqrt(kPlanetAreaKm2 / static_cast<double>(n));
    w.temperature_c.resize(n);
    w.precipitation_mm.resize(n);
    w.forager_capacity.resize(n);
    for (CellId c = 0; c < n; ++c) {
        const double z = w.grid.center(c).z;
        const double z2 = z * z;
        const double alt_km = std::max(0.0, w.elevation_m[c]) / 1000.0;
        w.temperature_c[c] = 28.0 - 48.0 * z2 - 6.5 * alt_km;

        const double abs_z = std::fabs(z);
        const double subtropic = std::max(0.0, 1.0 - ((abs_z - 0.45) / 0.15) * ((abs_z - 0.45) / 0.15));
        const double inland_km = static_cast<double>(w.coast_distance[c]) * cell_km;
        double p = 2200.0 * (1.0 - 0.75 * z2);
        p *= 1.0 - 0.8 * subtropic;
        p *= 1.0 / (1.0 + inland_km / 1200.0);
        w.precipitation_mm[c] = p;
        w.forager_capacity[c] =
            w.is_land[c] ? forager_density(w.temperature_c[c], p) * w.grid.area_km2(c) : 0.0;
    }

    assign_initial_groups(w);
    build_climate_mapping(w);
    return w;
}

std::uint64_t World::hash() const noexcept {
    Hasher h;
    h.u64(params.seed);
    h.u32(static_cast<std::uint32_t>(params.grid_level));
    h.f64(params.ocean_fraction);
    h.u64(grid.geometry_hash());
    h.span<double>(elevation_m);
    h.span<std::uint8_t>(is_land);
    h.span<std::uint32_t>(coast_distance);
    h.span<double>(temperature_c);
    h.span<double>(precipitation_mm);
    h.span<double>(forager_capacity);
    h.span<std::uint32_t>(initial_group);
    h.u32(group_count);
    h.u64(climate_grid.geometry_hash());
    for (std::size_t c = 0; c < climate_cells.size(); ++c) {
        for (int i = 0; i < 3; ++i) {
            h.u32(climate_cells[c][i]);
            h.f64(climate_weights[c][i]);
        }
    }
    return h.digest();
}

}  // namespace histsim
