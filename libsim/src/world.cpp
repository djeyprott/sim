#include "histsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <queue>

#include "histsim/hash.hpp"
#include "histsim/portable_math.hpp"
#include "histsim/rng.hpp"

namespace histsim {

namespace {

// ------------------------------------------------------------------ noise

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

double fractal_noise(std::uint64_t seed, const Vec3& p, int octaves = 7) {
    double sum = 0.0, amplitude = 1.0, total = 0.0, frequency = 1.6;
    for (std::uint32_t octave = 0; octave < static_cast<std::uint32_t>(octaves); ++octave) {
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

// ------------------------------------------------------------------ geometry

Vec3 normalized(double x, double y, double z) {
    const double len = std::sqrt(x * x + y * y + z * z);
    return {x / len, y / len, z / len};
}

double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Walk from `start` to the cell whose center is nearest p.
CellId nearest_from(const Grid& g, CellId start, const Vec3& p) {
    CellId best = start;
    double best_dot = dot(g.center(best), p);
    for (;;) {
        CellId next = best;
        for (CellId nb : g.neighbors(best)) {
            const double d = dot(g.center(nb), p);
            if (d > best_dot) {
                best_dot = d;
                next = nb;
            }
        }
        if (next == best) return best;
        best = next;
    }
}

// f(x) through the points (xs[i], ys[i]), xs increasing, clamped at the ends.
template <std::size_t N>
double piecewise(double x, const std::array<double, N>& xs, const std::array<double, N>& ys) {
    if (x <= xs[0]) return ys[0];
    for (std::size_t i = 1; i < N; ++i) {
        if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    }
    return ys[N - 1];
}

double bump(double x, double center, double half_width) {
    const double t = (x - center) / half_width;
    return std::max(0.0, 1.0 - t * t);
}

// ------------------------------------------------------------------ climate

// Mean annual temperature at sea level by sine of latitude: 27 °C at the
// equator, about 12 °C at 40°, 3 °C at 50°, -20 °C at the poles.
double sea_level_temperature(double z) {
    const double z2 = z * z;
    return 27.0 - 30.0 * z2 - 17.0 * z2 * z2;
}

// Rainfall belts before winds and mountains: wet equator (ITCZ), dry
// subtropics, the mid-latitude storm track, dry poles. mm per year.
double zonal_precipitation(double abs_z) {
    static constexpr std::array<double, 8> a = {0.0, 0.18, 0.42, 0.5, 0.62, 0.78, 0.92, 1.0};
    static constexpr std::array<double, 8> p = {2600, 1900, 450, 350, 650, 1050, 450, 180};
    return piecewise(abs_z, a, p);
}

struct Upwind {
    double land_km = 0.0;       // land crossed upwind before reaching the sea
    double barrier_m = 0.0;     // highest land crossed
};

// March upwind from a cell until the sea (or 4,000 km of land). The wind
// belt is set by the cell's latitude: trade winds and polar easterlies blow
// from the east, the westerlies (30°-60°) from the west.
Upwind trace_upwind(const World& w, CellId c, double step_km) {
    Upwind u;
    const Vec3& p0 = w.grid.center(c);
    const double abs_z = std::fabs(p0.z);
    const double toward = (abs_z > 0.5 && abs_z < 0.866) ? -1.0 : 1.0;  // upwind: +east or -east
    Vec3 p = p0;
    CellId cell = c;
    const double step = step_km / kPlanetRadiusKm;
    while (u.land_km < 4000.0) {
        const double ex = -p.y, ey = p.x;  // east = z x p
        const double elen = std::sqrt(ex * ex + ey * ey);
        if (elen < 1e-6) break;  // at a pole there is no east
        p = normalized(p.x + toward * step * ex / elen, p.y + toward * step * ey / elen, p.z);
        cell = nearest_from(w.grid, cell, p);
        if (!w.is_land[cell]) return u;
        u.land_km += step_km;
        u.barrier_m = std::max(u.barrier_m, w.elevation_m[cell]);
    }
    u.land_km = std::max(u.land_km, 4000.0);
    return u;
}

// Months: phase 0 at mid-summer of the cell's hemisphere.
double season_phase(double z, int m) {
    const double peak = z >= 0.0 ? 6.0 : 0.0;  // mid-July in the north, mid-January in the south
    return 2.0 * kPi * (static_cast<double>(m) - peak) / 12.0;
}

// Potential evapotranspiration of a month, mm (a simple temperature model).
double month_pet_mm(double t) { return std::max(0.0, 6.0 * t); }

Biome classify(const World& w, CellId c) {
    if (!w.is_land[c]) return Biome::Ocean;
    std::array<double, 12> t{}, p{};
    double cold = 1e9, warm = -1e9, annual = 0.0, summer = 0.0;
    const double z = w.grid.center(c).z;
    for (int m = 0; m < 12; ++m) {
        t[static_cast<std::size_t>(m)] = w.month_temperature_c(c, m);
        p[static_cast<std::size_t>(m)] = w.month_precipitation_mm(c, m);
        cold = std::min(cold, t[static_cast<std::size_t>(m)]);
        warm = std::max(warm, t[static_cast<std::size_t>(m)]);
        annual += p[static_cast<std::size_t>(m)];
        if (pmath::cos(season_phase(z, m)) > 0.0) summer += p[static_cast<std::size_t>(m)];
    }
    if (warm < 0.0) return Biome::IceSheet;
    if (warm < 10.0) return Biome::Tundra;

    // Köppen aridity threshold, mm: more rain is needed when it falls in summer.
    const double summer_share = annual > 0.0 ? summer / annual : 0.5;
    const double extra = summer_share >= 0.7 ? 280.0 : (summer_share <= 0.3 ? 0.0 : 140.0);
    const double threshold = 20.0 * w.temperature_c[c] + extra;
    if (annual < threshold) {
        if (annual < 0.5 * threshold) return Biome::Desert;
        return w.temperature_c[c] >= 18.0 ? Biome::DryShrubland : Biome::TemperateGrassland;
    }
    double driest = 1e9, driest_summer = 1e9, wettest_winter = 0.0;
    for (int m = 0; m < 12; ++m) {
        const double pm = p[static_cast<std::size_t>(m)];
        driest = std::min(driest, pm);
        if (pmath::cos(season_phase(z, m)) > 0.0) {
            driest_summer = std::min(driest_summer, pm);
        } else {
            wettest_winter = std::max(wettest_winter, pm);
        }
    }
    if (cold >= 18.0) {
        return (driest >= 60.0 || driest >= 100.0 - annual / 25.0) ? Biome::TropicalRainforest
                                                                    : Biome::TropicalSavanna;
    }
    if (cold > -3.0) {
        if (driest_summer < 40.0 && driest_summer < wettest_winter / 3.0) return Biome::Mediterranean;
        return warm >= 22.0 ? Biome::WarmTemperateForest : Biome::TemperateForest;
    }
    int warm_months = 0;
    for (double tm : t) warm_months += tm >= 10.0 ? 1 : 0;
    return warm_months >= 4 ? Biome::TemperateForest : Biome::BorealForest;
}

// ------------------------------------------------------------------ rivers

// Priority-flood (Barnes et al. 2014): flood the land from the sea in order
// of elevation, raising pits just enough to drain, so every land cell has a
// path downhill to the ocean. Each cell drains to the cell it was flooded
// from. Then add up runoff from the top of each basin down to the sea.
void build_rivers(World& w, const std::vector<double>& runoff_m3s) {
    const std::size_t n = w.cell_count();
    w.downstream.assign(n, kInvalidCell);
    w.discharge_m3s.assign(n, 0.0);
    w.basin_km2.assign(n, 0.0);
    std::vector<double> filled(w.elevation_m);
    std::vector<std::uint8_t> done(n, 0);
    using Item = std::pair<double, CellId>;  // (filled elevation, cell): a strict total order
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    for (CellId c = 0; c < n; ++c) {
        if (!w.is_land[c]) {
            open.push({w.elevation_m[c], c});
            done[c] = 1;
        }
    }
    std::vector<CellId> order;  // land cells in flood order (low to high)
    order.reserve(n);
    while (!open.empty()) {
        const CellId c = open.top().second;
        open.pop();
        if (w.is_land[c]) order.push_back(c);
        for (CellId nb : w.grid.neighbors(c)) {
            if (done[nb]) continue;
            done[nb] = 1;
            filled[nb] = std::max(w.elevation_m[nb], filled[c] + 0.01);
            w.downstream[nb] = c;
            open.push({filled[nb], nb});
        }
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const CellId c = *it;
        w.discharge_m3s[c] += runoff_m3s[c];
        w.basin_km2[c] += w.grid.area_km2(c);
        const CellId d = w.downstream[c];
        if (d != kInvalidCell && w.is_land[d]) {
            w.discharge_m3s[d] += w.discharge_m3s[c];
            w.basin_km2[d] += w.basin_km2[c];
        }
    }
}

// ------------------------------------------------------------------ soils

double biome_soil(Biome b) {
    switch (b) {
        case Biome::TemperateGrassland: return 0.90;  // deep, dark grassland soils
        case Biome::TemperateForest: return 0.70;
        case Biome::WarmTemperateForest: return 0.62;
        case Biome::Mediterranean: return 0.60;
        case Biome::TropicalSavanna: return 0.50;
        case Biome::DryShrubland: return 0.42;
        case Biome::BorealForest: return 0.32;       // acid, leached
        case Biome::TropicalRainforest: return 0.30; // nutrients locked in the forest, not the soil
        case Biome::Tundra: return 0.15;
        case Biome::Desert: return 0.15;
        default: return 0.0;
    }
}

// ------------------------------------------------------------------ species

double species_suitability(const World& w, const SpeciesDef& s, CellId c) {
    if (!w.is_land[c]) return 0.0;
    double v = s.coldest_month_c(w.coldest_month_c(c)) * s.warmest_month_c(w.warmest_month_c(c)) *
               s.annual_precip_mm(w.precipitation_mm[c]) * s.rain_season(w.rain_season[c]) *
               s.elevation_m(w.elevation_m[c]);
    if (s.river_affinity > 0.0) {
        const double river = std::min(1.0, w.discharge_m3s[c] / 1500.0);
        v *= (1.0 - s.river_affinity) + s.river_affinity * river;
    }
    return v;
}

// Native ranges: a few homelands among well-suited land, at least range_km
// apart; from each the species spreads through suitable country up to
// range_km away. Barriers (sea, desert, mountains) stop the spread, so the
// same climate on another continent may have no native species at all.
void place_species(World& w, const Content& content) {
    const std::size_t n = w.cell_count();
    const std::size_t count = content.species.size();
    w.species_mask.assign(n, 0);
    w.species_abundance.assign(n * count, 0);
    std::vector<double> suit(n);
    std::vector<std::uint8_t> reached(n);
    std::deque<CellId> queue;
    for (std::size_t s = 0; s < count; ++s) {
        const SpeciesDef& def = content.species[s];
        for (CellId c = 0; c < n; ++c) suit[c] = species_suitability(w, def, c);

        std::vector<CellId> candidates;
        for (double cut : {0.6, 0.3}) {
            for (CellId c = 0; c < n; ++c) {
                if (suit[c] >= cut) candidates.push_back(c);
            }
            if (!candidates.empty()) break;
        }
        if (candidates.empty()) continue;  // no suitable land on this world

        // The cells a homeland at `origin` would cover.
        auto spread = [&](CellId origin, std::vector<CellId>& out) {
            out.clear();
            std::fill(reached.begin(), reached.end(), 0);
            reached[origin] = 1;
            queue.push_back(origin);
            while (!queue.empty()) {
                const CellId c = queue.front();
                queue.pop_front();
                out.push_back(c);
                for (CellId nb : w.grid.neighbors(c)) {
                    if (reached[nb] || suit[nb] < 0.15) continue;
                    if (w.grid.distance_km(origin, nb) > def.range_km) continue;
                    reached[nb] = 1;
                    queue.push_back(nb);
                }
            }
        };

        // Homelands need room: a species does not originate on a speck of
        // land. Of 24 random candidates far enough from earlier homelands,
        // take the one whose range would cover the most land.
        RngStream rng(w.params.seed, 0, Stream::WorldGen, 100 + static_cast<std::uint32_t>(s));
        std::vector<CellId> origins, range, best_range;
        for (int r = 0; r < def.native_regions; ++r) {
            CellId best = kInvalidCell;
            best_range.clear();
            for (int attempt = 0; attempt < 24; ++attempt) {
                const CellId c = candidates[rng.below(static_cast<std::uint32_t>(candidates.size()))];
                const bool far = std::all_of(origins.begin(), origins.end(), [&](CellId o) {
                    return w.grid.distance_km(o, c) >= def.range_km;
                });
                if (!far) continue;
                spread(c, range);
                if (range.size() > best_range.size()) {
                    best = c;
                    best_range.swap(range);
                }
            }
            if (best == kInvalidCell) break;  // no room for another homeland
            origins.push_back(best);
            for (const CellId c : best_range) {
                const auto abundance = static_cast<std::uint8_t>(std::min(255.0, std::floor(suit[c] * 255.0 + 0.5)));
                std::uint8_t& slot = w.species_abundance[c * count + s];
                slot = std::max(slot, abundance);
                if (slot > 0) w.species_mask[c] |= 1u << s;
            }
        }
    }
}

// ------------------------------------------------------------------ peoples

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

    // About one starting people per 2 million km^2 of land (40 cells at grid
    // level 5), whatever the grid level.
    double land_km2 = 0.0;
    for (CellId c : land) land_km2 += w.grid.area_km2(c);
    const std::size_t target = std::max<std::size_t>(4, static_cast<std::size_t>(land_km2 / 2.0e6));
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

const char* biome_name(Biome b) {
    switch (b) {
        case Biome::Ocean: return "Ocean";
        case Biome::IceSheet: return "Ice sheet";
        case Biome::Tundra: return "Tundra";
        case Biome::BorealForest: return "Boreal forest";
        case Biome::TemperateForest: return "Temperate forest";
        case Biome::WarmTemperateForest: return "Warm temperate forest";
        case Biome::Mediterranean: return "Mediterranean woodland";
        case Biome::TemperateGrassland: return "Temperate grassland";
        case Biome::DryShrubland: return "Dry shrubland";
        case Biome::Desert: return "Desert";
        case Biome::TropicalSavanna: return "Tropical savanna";
        case Biome::TropicalRainforest: return "Tropical rainforest";
        default: return "Unknown";
    }
}

double forager_density(double temperature_c, double precipitation_mm) {
    const double warmth = std::clamp((temperature_c + 8.0) / 30.0, 0.0, 1.0);
    const double wetness = std::clamp((precipitation_mm - 100.0) / 1100.0, 0.0, 1.0);
    return 0.2 * warmth * wetness;
}

double World::month_temperature_c(CellId c, int m) const noexcept {
    return temperature_c[c] + 0.5 * temperature_range_c[c] * pmath::cos(season_phase(grid.center(c).z, m));
}

double World::month_precipitation_mm(CellId c, int m) const noexcept {
    return precipitation_mm[c] / 12.0 * (1.0 + rain_season[c] * pmath::cos(season_phase(grid.center(c).z, m)));
}

World generate_test_world(const WorldParams& params, const Content& content) {
    World w;
    w.params = params;
    w.content_hash = content.hash;
    // The default pack lives for the whole program: refer to it without
    // owning it. Any other pack is copied, so the world never dangles.
    if (&content == &default_content()) {
        w.content = std::shared_ptr<const Content>(std::shared_ptr<const Content>{}, &content);
    } else {
        w.content = std::make_shared<const Content>(content);
    }
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
        if (d == kUnreached) d = 0;  // only possible on an all-land world
    }

    // --- Baseline climate: winds, rain shadows, seasons ----------------------
    const double cell_km = std::sqrt(kPlanetAreaKm2 / static_cast<double>(n));
    w.temperature_c.resize(n);
    w.precipitation_mm.resize(n);
    w.temperature_range_c.resize(n);
    w.rain_season.resize(n);
    w.continentality.resize(n);
    for (CellId c = 0; c < n; ++c) {
        const double z = w.grid.center(c).z;
        const double abs_z = std::fabs(z);
        const double elev = w.elevation_m[c];
        const double alt_km = std::max(0.0, elev) / 1000.0;
        w.temperature_c[c] = sea_level_temperature(z) - 6.5 * alt_km;

        if (!w.is_land[c]) {
            w.precipitation_mm[c] = zonal_precipitation(abs_z);
            w.temperature_range_c[c] = 1.0 + 6.0 * abs_z;
            w.rain_season[c] = 0.0;
            w.continentality[c] = 0.0;
            continue;
        }
        const Upwind up = trace_upwind(w, c, cell_km);
        const double moisture = 1.0 / (1.0 + up.land_km / 1400.0);
        double p = zonal_precipitation(abs_z) * (0.25 + 0.75 * moisture);
        const double shadow_m = up.barrier_m - elev;
        if (shadow_m > 800.0) {
            p /= 1.0 + (shadow_m - 800.0) / 1200.0;  // dry lee of mountains
        } else {
            p *= 1.0 + 0.35 * std::min(alt_km, 3.0);  // rain on windward slopes
        }
        w.precipitation_mm[c] = p;

        const double cont = std::min(1.0, up.land_km / 2500.0);
        w.continentality[c] = cont;
        w.temperature_range_c[c] = 2.0 + abs_z * (8.0 + 37.0 * cont);

        // Rain season, signed: + summer rain (monsoons, continental
        // interiors), - winter rain (Mediterranean coasts of the westerlies).
        const double summer = 0.1 + 0.7 * bump(abs_z, 0.30, 0.18) + 0.3 * cont * bump(abs_z, 0.62, 0.25) +
                              0.25 * bump(abs_z, 0.92, 0.15);
        const double winter = 0.75 * bump(abs_z, 0.58, 0.13) * (1.0 - cont);
        w.rain_season[c] = std::clamp(summer - winter, -0.85, 0.9);
    }

    // --- Biomes and growing season ---------------------------------------------
    w.biome.resize(n);
    w.growing_months.resize(n);
    std::vector<double> runoff_m3s(n, 0.0);
    for (CellId c = 0; c < n; ++c) {
        w.biome[c] = static_cast<std::uint8_t>(classify(w, c));
        if (!w.is_land[c]) {
            w.growing_months[c] = 0;
            continue;
        }
        int growing = 0;
        double pet = 0.0;
        for (int m = 0; m < 12; ++m) {
            const double t = w.month_temperature_c(c, m);
            const double pm = w.month_precipitation_mm(c, m);
            pet += month_pet_mm(t);
            growing += (t >= 5.0 && pm >= 0.35 * month_pet_mm(t)) ? 1 : 0;
        }
        w.growing_months[c] = static_cast<std::uint8_t>(growing);
        const double runoff_mm = std::max(0.0, w.precipitation_mm[c] - 0.8 * pet);
        runoff_m3s[c] = runoff_mm * w.grid.area_km2(c) * 1000.0 / 31557600.0;
    }

    // --- Rivers, soils, aquatic foraging -----------------------------------------
    build_rivers(w, runoff_m3s);
    w.soil_fertility.assign(n, 0.0);
    w.aquatic_bonus.assign(n, 0.0);
    w.forager_capacity.assign(n, 0.0);
    for (CellId c = 0; c < n; ++c) {
        if (!w.is_land[c]) continue;
        double relief = 0.0;
        for (CellId nb : w.grid.neighbors(c)) {
            if (w.is_land[nb]) relief = std::max(relief, std::fabs(w.elevation_m[nb] - w.elevation_m[c]));
        }
        const double q = w.discharge_m3s[c];
        const double alluvium = 0.35 * q / (q + 1500.0);
        const double variety = 0.2 * (fractal_noise(params.seed ^ 0x501Lu, w.grid.center(c), 4) - 0.5);
        const double base = biome_soil(static_cast<Biome>(w.biome[c]));
        w.soil_fertility[c] = std::clamp(base / (1.0 + relief / 1500.0) + alluvium + variety, 0.0, 1.0);

        // Coasts (shellfish, fish) and big rivers (fish, waterfowl) feed
        // foragers more densely than land alone.
        const double coast = w.coast_distance[c] == 1 ? 0.3 : 0.0;
        w.aquatic_bonus[c] = std::min(0.8, coast + 0.6 * q / (q + 3000.0));
        w.forager_capacity[c] = forager_density(w.temperature_c[c], w.precipitation_mm[c]) *
                                w.grid.area_km2(c) * (1.0 + w.aquatic_bonus[c]);
    }

    place_species(w, content);
    assign_initial_groups(w);
    build_climate_mapping(w);
    return w;
}

std::uint64_t World::hash() const noexcept {
    Hasher h;
    h.u64(params.seed);
    h.u32(static_cast<std::uint32_t>(params.grid_level));
    h.f64(params.ocean_fraction);
    h.u64(content_hash);
    h.u64(grid.geometry_hash());
    h.span<double>(elevation_m);
    h.span<std::uint8_t>(is_land);
    h.span<std::uint32_t>(coast_distance);
    h.span<double>(temperature_c);
    h.span<double>(precipitation_mm);
    h.span<double>(temperature_range_c);
    h.span<double>(rain_season);
    h.span<double>(continentality);
    h.span<std::uint8_t>(biome);
    h.span<std::uint8_t>(growing_months);
    h.span<std::uint32_t>(downstream);
    h.span<double>(discharge_m3s);
    h.span<double>(basin_km2);
    h.span<double>(soil_fertility);
    h.span<double>(aquatic_bonus);
    h.span<double>(forager_capacity);
    h.span<std::uint32_t>(species_mask);
    h.span<std::uint8_t>(species_abundance);
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
