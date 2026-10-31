// Turning per-cell colors into a map image.
//
// A MapRaster maps every pixel of an equirectangular (longitude x latitude)
// image to the grid cell it falls in. It is built once per grid and size;
// after that, drawing a frame is one lookup per pixel and hover is one lookup.
// The same image can be wrapped on a sphere for a globe view.
#pragma once

#include <cstdint>
#include <vector>

#include "histsim/grid.hpp"
#include "histsim/view/color.hpp"
#include "histsim/world.hpp"

namespace histsim::view {

class MapRaster {
public:
    // width should be twice height for an undistorted equirectangular map.
    static MapRaster build(const Grid& grid, int width, int height);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

    // Cell under pixel (x, y); kInvalidCell outside the image.
    [[nodiscard]] CellId cell_at(int x, int y) const noexcept;

    [[nodiscard]] const std::vector<CellId>& cells() const noexcept { return cells_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<CellId> cells_;  // row-major, top row = north
};

struct RenderOptions {
    bool coastlines = true;  // dark line where land meets ocean
    bool rivers = false;     // rivers drawn as blue lines, wider for more flow
    // Optional per-cell flags (size = cell count): flagged cells are lightened
    // and outlined, e.g. to preview the area an intervention will touch.
    const std::vector<std::uint8_t>* highlight = nullptr;
    // Optional per-cell region ids (size = cell count), e.g. the polity ruling
    // each cell: a line is drawn where land cells of different regions meet
    // (and around a region where it meets unclaimed land).
    const std::vector<std::uint32_t>* borders = nullptr;
};

// Fill `rgba` (width * height * 4 bytes) from one color per cell.
void render(const MapRaster& raster, const World& world, const std::vector<Rgba>& cell_colors,
            std::vector<std::uint8_t>& rgba, const RenderOptions& options = {});

}  // namespace histsim::view
