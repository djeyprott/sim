#include "histsim/grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "histsim/hash.hpp"
#include "histsim/portable_math.hpp"

namespace histsim {

namespace {

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }
Vec3 normalize(const Vec3& a) { return scale(a, 1.0 / norm(a)); }

using Tri = std::array<std::uint32_t, 3>;

// The 12 vertices and 20 faces of a regular icosahedron, faces wound
// counter-clockwise seen from outside.
void base_icosahedron(std::vector<Vec3>& verts, std::vector<Tri>& faces) {
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    const Vec3 raw[12] = {{-1, t, 0}, {1, t, 0},   {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                          {0, -1, -t}, {0, 1, -t}, {t, 0, -1},  {t, 0, 1},  {-t, 0, -1}, {-t, 0, 1}};
    verts.clear();
    for (const Vec3& v : raw) {
        verts.push_back(normalize(v));
    }
    faces = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
             {11, 10, 2}, {10, 7, 6}, {7, 1, 8},   {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
             {3, 8, 9},   {4, 9, 5},  {2, 4, 11},  {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
}

// Split every triangle into four, sharing edge midpoints between neighbors.
void subdivide(std::vector<Vec3>& verts, std::vector<Tri>& faces) {
    std::unordered_map<std::uint64_t, std::uint32_t> midpoint;  // lookup only, never iterated
    midpoint.reserve(faces.size() * 2);
    auto mid = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint64_t key = a < b ? (static_cast<std::uint64_t>(a) << 32) | b
                                        : (static_cast<std::uint64_t>(b) << 32) | a;
        auto it = midpoint.find(key);
        if (it != midpoint.end()) {
            return it->second;
        }
        const auto id = static_cast<std::uint32_t>(verts.size());
        verts.push_back(normalize(add(verts[a], verts[b])));
        midpoint.emplace(key, id);
        return id;
    };
    std::vector<Tri> next;
    next.reserve(faces.size() * 4);
    for (const Tri& f : faces) {
        const std::uint32_t ab = mid(f[0], f[1]);
        const std::uint32_t bc = mid(f[1], f[2]);
        const std::uint32_t ca = mid(f[2], f[0]);
        next.push_back({f[0], ab, ca});
        next.push_back({f[1], bc, ab});
        next.push_back({f[2], ca, bc});
        next.push_back({ab, bc, ca});
    }
    faces = std::move(next);
}

// Quadrant-then-cross-product comparison of two tangent-plane directions:
// sorts by angle without atan2.
int half_plane(double u, double v) { return (v > 0.0 || (v == 0.0 && u > 0.0)) ? 0 : 1; }

}  // namespace

Grid make_icosahedral_grid(int level) {
    if (level < 0 || level > 10) {
        throw std::invalid_argument("grid level must be between 0 and 10");
    }

    std::vector<Vec3> verts;
    std::vector<Tri> faces;
    base_icosahedron(verts, faces);
    for (int i = 0; i < level; ++i) {
        subdivide(verts, faces);
    }

    const std::size_t n = verts.size();

    // Incident triangles per vertex, in the deterministic order faces were made.
    std::vector<std::vector<std::uint32_t>> incident(n);
    for (std::uint32_t fi = 0; fi < faces.size(); ++fi) {
        for (std::uint32_t v : faces[fi]) {
            incident[v].push_back(fi);
        }
    }

    Grid g;
    g.level_ = level;
    g.center_ = verts;
    g.area_km2_.assign(n, 0.0);
    g.neighbor_offset_.assign(n + 1, 0);

    // Each triangle's corner of the hexagonal dual cell: its centroid, on the sphere.
    std::vector<Vec3> corner(faces.size());
    for (std::size_t fi = 0; fi < faces.size(); ++fi) {
        const Tri& f = faces[fi];
        corner[fi] = normalize(add(add(verts[f[0]], verts[f[1]]), verts[f[2]]));
    }

    double total_area = 0.0;
    std::vector<std::pair<std::uint32_t, Vec3>> around;  // (face, direction) for sorting
    std::vector<CellId> neigh;
    for (std::uint32_t v = 0; v < n; ++v) {
        const Vec3& c = verts[v];
        // Tangent basis at c.
        const Vec3 helper = std::fabs(c.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
        const Vec3 e1 = normalize(cross(helper, c));
        const Vec3 e2 = cross(c, e1);

        around.clear();
        for (std::uint32_t fi : incident[v]) {
            const Vec3 d = sub(corner[fi], c);
            around.push_back({fi, Vec3{dot(d, e1), dot(d, e2), 0.0}});
        }
        std::sort(around.begin(), around.end(), [](const auto& a, const auto& b) {
            const double au = a.second.x, av = a.second.y, bu = b.second.x, bv = b.second.y;
            const int ha = half_plane(au, av), hb = half_plane(bu, bv);
            if (ha != hb) {
                return ha < hb;
            }
            const double cr = au * bv - av * bu;
            if (cr != 0.0) {
                return cr > 0.0;
            }
            return a.first < b.first;  // tie-break keeps the order stable everywhere
        });

        // Area: fan of flat triangles from the center to consecutive corners.
        // Scaled to the true sphere area below.
        double area = 0.0;
        for (std::size_t i = 0; i < around.size(); ++i) {
            const Vec3& p = corner[around[i].first];
            const Vec3& q = corner[around[(i + 1) % around.size()].first];
            area += 0.5 * norm(cross(sub(p, c), sub(q, c)));
        }
        g.area_km2_[v] = area;
        total_area += area;

        // Neighbors: between consecutive corners lies the edge shared with one
        // neighbor, the vertex common to both triangles other than v.
        neigh.clear();
        for (std::size_t i = 0; i < around.size(); ++i) {
            const Tri& f1 = faces[around[i].first];
            const Tri& f2 = faces[around[(i + 1) % around.size()].first];
            for (std::uint32_t a : f1) {
                if (a != v && (a == f2[0] || a == f2[1] || a == f2[2])) {
                    neigh.push_back(a);
                    break;
                }
            }
        }
        g.neighbor_offset_[v + 1] = g.neighbor_offset_[v] + static_cast<std::uint32_t>(neigh.size());
        g.neighbor_ids_.insert(g.neighbor_ids_.end(), neigh.begin(), neigh.end());
    }

    const double to_km2 = kPlanetAreaKm2 / total_area;
    for (double& a : g.area_km2_) {
        a *= to_km2;
    }
    return g;
}

double Grid::distance_km(CellId a, CellId b) const noexcept {
    // Chord length c = 2 sin(theta / 2), so theta = 2 asin(c / 2).
    const double s = std::min(1.0, 0.5 * norm(sub(center_[a], center_[b])));
    return 2.0 * pmath::asin_unit(s) * kPlanetRadiusKm;
}

std::uint64_t Grid::geometry_hash() const noexcept {
    Hasher h;
    h.u32(static_cast<std::uint32_t>(level_));
    for (const Vec3& v : center_) {
        h.f64(v.x);
        h.f64(v.y);
        h.f64(v.z);
    }
    h.span<double>(area_km2_);
    h.span<std::uint32_t>(neighbor_offset_);
    h.span<CellId>(neighbor_ids_);
    return h.digest();
}

double Grid::latitude_deg_display(CellId c) const noexcept {
    return std::asin(std::clamp(center_[c].z, -1.0, 1.0)) * 180.0 / kPi;
}

double Grid::longitude_deg_display(CellId c) const noexcept {
    return std::atan2(center_[c].y, center_[c].x) * 180.0 / kPi;
}

}  // namespace histsim
