// Colors and color scales for map modes.
//
// Display code only: nothing here feeds simulation state, so it may use any
// math it likes.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace histsim::view {

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    friend bool operator==(const Rgba&, const Rgba&) = default;
};

// "#rrggbb" -> Rgba. Only used with literal palette values.
Rgba hex(const char* s);

// Piecewise-linear color scale. Values below the first stop or above the last
// take the end colors. With log_scale, stops and lookups use log10(value).
class ColorScale {
public:
    struct Stop {
        double value;
        Rgba color;
    };

    ColorScale() = default;
    ColorScale(std::vector<Stop> stops, bool log_scale = false);

    [[nodiscard]] Rgba operator()(double value) const;
    [[nodiscard]] const std::vector<Stop>& stops() const noexcept { return stops_; }
    [[nodiscard]] bool log_scale() const noexcept { return log_; }

private:
    std::vector<Stop> stops_;
    bool log_ = false;
};

}  // namespace histsim::view
