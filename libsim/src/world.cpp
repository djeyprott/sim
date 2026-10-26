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

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

}  // namespace

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
        p *= 1.0 - 0.65 * subtropic;
        p *= 1.0 / (1.0 + inland_km / 1500.0);
        w.precipitation_mm[c] = p;

        // Forager density (people per km^2), a placeholder until M1: warm and
        // wet land supports up to ~0.2/km^2; ice, deserts and ocean support none.
        if (w.is_land[c]) {
            const double warmth = clamp01((w.temperature_c[c] + 8.0) / 30.0);
            const double wetness = clamp01((p - 100.0) / 1100.0);
            w.forager_capacity[c] = 0.2 * warmth * wetness * w.grid.area_km2(c);
        } else {
            w.forager_capacity[c] = 0.0;
        }
    }
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
    return h.digest();
}

}  // namespace histsim
