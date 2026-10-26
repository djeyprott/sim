#include "histsim/rng.hpp"

namespace histsim {

namespace {

constexpr std::uint32_t kPhiloxM0 = 0xD2511F53u;
constexpr std::uint32_t kPhiloxM1 = 0xCD9E8D57u;
constexpr std::uint32_t kPhiloxW0 = 0x9E3779B9u;  // golden ratio
constexpr std::uint32_t kPhiloxW1 = 0xBB67AE85u;  // sqrt(3) - 1

inline void mulhilo(std::uint32_t a, std::uint32_t b, std::uint32_t& hi, std::uint32_t& lo) noexcept {
    const std::uint64_t product = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    hi = static_cast<std::uint32_t>(product >> 32);
    lo = static_cast<std::uint32_t>(product);
}

inline PhiloxCounter philox_round(const PhiloxCounter& c, const PhiloxKey& k) noexcept {
    std::uint32_t hi0, lo0, hi1, lo1;
    mulhilo(kPhiloxM0, c[0], hi0, lo0);
    mulhilo(kPhiloxM1, c[2], hi1, lo1);
    return {hi1 ^ c[1] ^ k[0], lo1, hi0 ^ c[3] ^ k[1], lo0};
}

}  // namespace

PhiloxCounter philox4x32_10(PhiloxCounter counter, PhiloxKey key) noexcept {
    for (int round = 0; round < 10; ++round) {
        if (round > 0) {
            key[0] += kPhiloxW0;
            key[1] += kPhiloxW1;
        }
        counter = philox_round(counter, key);
    }
    return counter;
}

RngStream::RngStream(std::uint64_t seed, Tick tick, Stream stream, std::uint32_t entity) noexcept {
    const PhiloxCounter ctr{0u, entity, static_cast<std::uint32_t>(stream), static_cast<std::uint32_t>(tick)};
    const PhiloxKey key{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32)};
    const PhiloxCounter h = philox4x32_10(ctr, key);
    state_ = (static_cast<std::uint64_t>(h[0]) << 32) | h[1];
}

std::uint32_t RngStream::below(std::uint32_t n) noexcept {
    // Reject the top partial bucket so every value in [0, n) is equally likely.
    const std::uint32_t threshold = (0u - n) % n;
    for (;;) {
        const std::uint32_t r = next_u32();
        if (r >= threshold) {
            return r % n;
        }
    }
}

}  // namespace histsim
