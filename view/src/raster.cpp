#include "histsim/view/raster.hpp"

#include <cmath>
#include <stdexcept>

#include "histsim/view/map_modes.hpp"

#include <algorithm>

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

// Pixel position of a cell center on the equirectangular map (display math).
void cell_pixel(const World& world, CellId c, int w, int h, double& x, double& y) {
    const Vec3& p = world.grid.center(c);
    const double lon = std::atan2(p.y, p.x) * 180.0 / kPi;
    const double lat = std::asin(std::clamp(p.z, -1.0, 1.0)) * 180.0 / kPi;
    x = (lon + 180.0) / 360.0 * w - 0.5;
    y = (90.0 - lat) / 180.0 * h - 0.5;
}

void plot(std::vector<std::uint8_t>& rgba, int w, int h, int x, int y, const Rgba& c) {
    if (y < 0 || y >= h) return;
    x = ((x % w) + w) % w;
    std::uint8_t* px = &rgba[(static_cast<std::size_t>(y) * w + x) * 4];
    px[0] = c.r;
    px[1] = c.g;
    px[2] = c.b;
    px[3] = c.a;
}

// A thick line by stamping squares along it.
void line(std::vector<std::uint8_t>& rgba, int w, int h, double x0, double y0, double x1, double y1, int width,
          const Rgba& c) {
    const double len = std::max(std::fabs(x1 - x0), std::fabs(y1 - y0));
    const int steps = std::max(1, static_cast<int>(std::ceil(len)));
    const int r0 = -(width - 1) / 2, r1 = width / 2;
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const int x = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
        const int y = static_cast<int>(std::lround(y0 + (y1 - y0) * t));
        for (int dy = r0; dy <= r1; ++dy) {
            for (int dx = r0; dx <= r1; ++dx) plot(rgba, w, h, x + dx, y + dy, c);
        }
    }
}

void draw_rivers(const World& world, int w, int h, std::vector<std::uint8_t>& rgba) {
    const Rgba blue = hex("#2a78d6");
    const double scale = static_cast<double>(w) / 2048.0;
    for (CellId c = 0; c < world.cell_count(); ++c) {
        if (!is_river(world, c)) continue;
        const CellId d = world.downstream[c];
        if (d == kInvalidCell) continue;
        double x0, y0, x1, y1;
        cell_pixel(world, c, w, h, x0, y0);
        cell_pixel(world, d, w, h, x1, y1);
        if (std::fabs(x1 - x0) > 0.5 * w) continue;  // crosses the date line
        const double q = world.discharge_m3s[c];
        const int base = q < 3000.0 ? 1 : (q < 15000.0 ? 2 : 3);
        const int width = std::max(1, static_cast<int>(std::lround(base * scale)));
        line(rgba, w, h, x0, y0, x1, y1, width, blue);
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
    const Rgba outline = hex("#eda100");
    const std::vector<std::uint8_t>* hl = options.highlight;
    auto lit = [&](CellId c) { return hl != nullptr && (*hl)[c] != 0; };

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
            if (hl != nullptr) {
                const CellId right = cells[static_cast<std::size_t>(y) * w + (x + 1) % w];
                const CellId down = y + 1 < h ? cells[i + w] : c;
                if (lit(c) != lit(right) || lit(c) != lit(down)) {
                    color = outline;
                } else if (lit(c)) {
                    color = {static_cast<std::uint8_t>((color.r + 255) / 2), static_cast<std::uint8_t>((color.g + 255) / 2),
                             static_cast<std::uint8_t>((color.b + 255) / 2), color.a};
                }
            }
            std::uint8_t* px = &rgba[i * 4];
            px[0] = color.r;
            px[1] = color.g;
            px[2] = color.b;
            px[3] = color.a;
        }
    }
    if (options.rivers) {
        draw_rivers(world, w, h, rgba);
    }
}

}  // namespace histsim::view
