// Math functions that give bit-identical results on every platform.
//
// libm's sin, cos, exp and friends are not correctly rounded, and different C
// libraries return different last bits, which would make runs diverge between
// Windows, macOS and Linux. These versions use only + - * /, sqrt and floor,
// which IEEE 754 specifies exactly. Measured against libm: sin and cos within
// 4e-14, asin_unit within 3e-7 (used for distances, where that is plenty).
#pragma once

#include <cmath>

#include "histsim/types.hpp"

namespace histsim::pmath {

// sin and cos for any finite argument: reduce to [-pi/4, pi/4] by quadrant,
// then a Taylor polynomial.
namespace detail {

inline double sin_poly(double x) {  // |x| <= pi/4
    const double x2 = x * x;
    return x * (1.0 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040 + x2 * (1.0 / 362880 +
               x2 * (-1.0 / 39916800 + x2 * (1.0 / 6227020800.0)))))));
}

inline double cos_poly(double x) {  // |x| <= pi/4
    const double x2 = x * x;
    return 1.0 + x2 * (-1.0 / 2 + x2 * (1.0 / 24 + x2 * (-1.0 / 720 + x2 * (1.0 / 40320 +
               x2 * (-1.0 / 3628800 + x2 * (1.0 / 479001600.0 + x2 * (-1.0 / 87178291200.0)))))));
}

}  // namespace detail

inline double sin(double x) {
    const double q = std::floor(x / (0.5 * kPi) + 0.5);  // nearest quadrant
    const double r = x - q * (0.5 * kPi);
    const long long k = static_cast<long long>(q) & 3;
    switch (k) {
        case 0: return detail::sin_poly(r);
        case 1: return detail::cos_poly(r);
        case 2: return -detail::sin_poly(r);
        default: return -detail::cos_poly(r);
    }
}

inline double cos(double x) { return sin(x + 0.5 * kPi); }

// Arcsine on [0, 1] (series below 0.5, half-angle identity above).
namespace detail {
inline double asin_series(double x) {  // 0 <= x <= 0.5
    const double x2 = x * x;
    return x * (1.0 + x2 * (1.0 / 6.0 + x2 * (3.0 / 40.0 + x2 * (5.0 / 112.0 +
               x2 * (35.0 / 1152.0 + x2 * (63.0 / 2816.0 + x2 * (231.0 / 13312.0 + x2 * (143.0 / 10240.0))))))));
}
}  // namespace detail

inline double asin_unit(double s) {
    if (s <= 0.5) {
        return detail::asin_series(s);
    }
    return 0.5 * kPi - 2.0 * detail::asin_series(std::sqrt(0.5 * (1.0 - s)));
}

// An approximately standard-normal draw from four uniforms (Irwin-Hall). Its
// tails stop at +/-3.5, which suits climate noise: no absurd outliers.
inline double normal_from_uniforms(double u1, double u2, double u3, double u4) {
    return (u1 + u2 + u3 + u4 - 2.0) * 1.7320508075688772;  // sqrt(3)
}

}  // namespace histsim::pmath
