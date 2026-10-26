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
};

// A sequence of draws for one (seed, tick, stream, entity) tuple.
//
// Counter layout: [block index, entity, stream, tick]. Key: the 64-bit seed.
// Each block yields four 32-bit values.
class RngStream {
public:
    RngStream(std::uint64_t seed, Tick tick, Stream stream, std::uint32_t entity) noexcept;

    std::uint32_t next_u32() noexcept;
    std::uint64_t next_u64() noexcept;

    // Uniform double in [0, 1) with 53 random bits.
    double uniform() noexcept;

    // Uniform integer in [0, n). n must be > 0. Uses rejection, so it is unbiased.
    std::uint32_t below(std::uint32_t n) noexcept;

private:
    void refill() noexcept;

    PhiloxKey key_;
    PhiloxCounter counter_;
    PhiloxCounter block_{};
    int used_ = 4;
};

// Round x >= 0 to an integer, up with probability frac(x). The expected value
// equals x, which keeps small rates (e.g. 0.3 births a year in a tiny band)
// from being rounded away.
std::int64_t stochastic_round(double x, RngStream& rng) noexcept;

}  // namespace histsim
