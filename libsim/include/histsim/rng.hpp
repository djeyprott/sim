// Counter-based random numbers.
//
// Every random draw is a pure function of (seed, tick, stream, entity, index),
// computed with Philox4x32-10 (Salmon et al., "Parallel Random Numbers: As Easy
// as 1, 2, 3", SC'11). There is no hidden generator state to carry between
// ticks or threads, so systems can run in any order or in parallel and a run
// still replays bit for bit.
#pragma once

#include <array>
#include <cstdint>

#include "histsim/types.hpp"

namespace histsim {

using PhiloxCounter = std::array<std::uint32_t, 4>;
using PhiloxKey = std::array<std::uint32_t, 2>;

// Philox4x32 with 10 rounds. Matches the Random123 reference implementation.
PhiloxCounter philox4x32_10(PhiloxCounter counter, PhiloxKey key) noexcept;

// Identifies which system is drawing numbers, so two systems never share a
// sequence. Append new values; never renumber existing ones (that would change
// every saved run).
enum class Stream : std::uint32_t {
    WorldGen = 1,
    Demography = 2,
    Migration = 3,
    Climate = 4,
    Intervention = 5,
    Culture = 6,
    Language = 7,
};

// A sequence of draws for one (seed, tick, stream, entity) tuple.
//
// The tuple is hashed once with Philox (counter [0, entity, stream, tick],
// key = the 64-bit seed) into a 64-bit starting state; draws then come from
// SplitMix64 (Steele et al. 2014), several times cheaper per number. Every
// draw is still a pure function of the tuple and its position in the stream.
class RngStream {
public:
    RngStream(std::uint64_t seed, Tick tick, Stream stream, std::uint32_t entity) noexcept;

    std::uint64_t next_u64() noexcept {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    std::uint32_t next_u32() noexcept {
        if (has_spare_) {
            has_spare_ = false;
            return spare_;
        }
        const std::uint64_t v = next_u64();
        spare_ = static_cast<std::uint32_t>(v);
        has_spare_ = true;
        return static_cast<std::uint32_t>(v >> 32);
    }

    // Uniform double in [0, 1) with 53 random bits.
    double uniform() noexcept { return static_cast<double>(next_u64() >> 11) * 0x1.0p-53; }

    // Uniform double in [0, 1) with 32 random bits: half the cost, plenty for
    // deciding a rounding or a rare event.
    double uniform32() noexcept { return static_cast<double>(next_u32()) * 0x1.0p-32; }

    // Uniform integer in [0, n). n must be > 0. Uses rejection, so it is unbiased.
    std::uint32_t below(std::uint32_t n) noexcept;

private:
    std::uint64_t state_ = 0;
    std::uint32_t spare_ = 0;
    bool has_spare_ = false;
};

// Round x >= 0 to an integer, up with probability frac(x). The expected value
// equals x, which keeps small rates (e.g. 0.3 births a year in a tiny band)
// from being rounded away.
inline std::int64_t stochastic_round(double x, RngStream& rng) noexcept {
    if (!(x > 0.0)) {
        return 0;
    }
    const auto whole = static_cast<std::int64_t>(x);  // truncation == floor for x > 0
    const double frac = x - static_cast<double>(whole);
    if (frac == 0.0) {
        return whole;
    }
    return whole + (rng.uniform32() < frac ? 1 : 0);
}

}  // namespace histsim
