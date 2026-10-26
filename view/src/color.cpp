#include "histsim/view/color.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace histsim::view {

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("bad hex color");
}

std::uint8_t mix(std::uint8_t a, std::uint8_t b, double t) {
    return static_cast<std::uint8_t>(std::lround(a + (b - a) * t));
}

}  // namespace

Rgba hex(const char* s) {
    if (s[0] == '#') ++s;
    auto byte = [&](int i) { return static_cast<std::uint8_t>(hex_digit(s[i]) * 16 + hex_digit(s[i + 1])); };
    return {byte(0), byte(2), byte(4), 255};
}

ColorScale::ColorScale(std::vector<Stop> stops, bool log_scale) : stops_(std::move(stops)), log_(log_scale) {
    if (stops_.size() < 2) {
        throw std::invalid_argument("a color scale needs at least two stops");
    }
}

Rgba ColorScale::operator()(double value) const {
    auto key = [&](double v) { return log_ ? std::log10(std::max(v, 1e-12)) : v; };
    const double v = key(value);
    if (!(v > key(stops_.front().value))) return stops_.front().color;  // also catches NaN
    if (v >= key(stops_.back().value)) return stops_.back().color;
    for (std::size_t i = 1; i < stops_.size(); ++i) {
        const double hi = key(stops_[i].value);
        if (v <= hi) {
            const double lo = key(stops_[i - 1].value);
            const double t = (v - lo) / (hi - lo);
            const Rgba& a = stops_[i - 1].color;
            const Rgba& b = stops_[i].color;
            return {mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t), mix(a.a, b.a, t)};
        }
    }
    return stops_.back().color;
}

}  // namespace histsim::view
