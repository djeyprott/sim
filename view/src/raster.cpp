#include "histsim/view/raster.hpp"

#include <cmath>
#include <stdexcept>

#include "histsim/view/map_modes.hpp"

namespace histsim::view {

namespace {

double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Walk from `start` to the cell whose center is nearest p, always stepping to
// the neighbor that is closer. On a near-Delaunay grid like ours this reaches
// the nearest center, and consecutive pixels need only a step or two.
CellId nearest_from(const Grid& grid, CellId start, const Vec3& p) {
    CellId best = start;
    double best_dot = dot(grid.center(best), p);
    for (;;) {
        CellId next = best;
        for (CellId nb : grid.neighbors(best)) {
            const double d = dot(grid.center(nb), p);
            if (d > best_dot) {
                best_dot = d;
                next = nb;
            }
        }
        if (next == best) {
            return best;
        }
        best = next;
    }
}

}  // namespace

MapRaster MapRaster::build(const Grid& grid, int width, int height) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 8192) {
        throw std::invalid_argument("map size out of range");
    }
    MapRaster r;
    r.width_ = width;
    r.height_ = height;
    r.cells_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    CellId row_start = 0;
    for (int y = 0; y < height; ++y) {
        const double lat = (90.0 - (y + 0.5) * 180.0 / height) * kPi / 180.0;
        const double clat = std::cos(lat), slat = std::sin(lat);
        CellId c = row_start;
        for (int x = 0; x < width; ++x) {
            const double lon = (-180.0 + (x + 0.5) * 360.0 / width) * kPi / 180.0;
            const Vec3 p{clat * std::cos(lon), clat * std::sin(lon), slat};
            c = nearest_from(grid, c, p);
            r.cells_[static_cast<std::size_t>(y) * width + x] = c;
            if (x == 0) {
                row_start = c;
            }
        }
    }
    return r;
}

CellId MapRaster::cell_at(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return kInvalidCell;
    }
    return cells_[static_cast<std::size_t>(y) * width_ + x];
}

void render(const MapRaster& raster, const World& world, const std::vector<Rgba>& cell_colors,
            std::vector<std::uint8_t>& rgba, const RenderOptions& options) {
    const int w = raster.width(), h = raster.height();
    rgba.resize(static_cast<std::size_t>(w) * h * 4);
    const auto& cells = raster.cells();
    const Rgba coast = coastline_color();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const CellId c = cells[i];
            Rgba color = cell_colors[c];
            if (options.coastlines) {
                // A land pixel whose right or lower neighbor is ocean (or the
                // reverse) sits on the coast. Wraps around at the date line.
                const CellId right = cells[static_cast<std::size_t>(y) * w + (x + 1) % w];
                const CellId down = y + 1 < h ? cells[i + w] : c;
                const std::uint8_t land = world.is_land[c];
                if (world.is_land[right] != land || world.is_land[down] != land) {
                    color = coast;
                }
            }
            std::uint8_t* px = &rgba[i * 4];
            px[0] = color.r;
            px[1] = color.g;
            px[2] = color.b;
            px[3] = color.a;
        }
    }
}

}  // namespace histsim::view
