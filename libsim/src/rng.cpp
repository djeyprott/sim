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

RngStream::RngStream(std::uint64_t seed, Tick tick, Stream stream, std::uint32_t entity) noexcept
    : key_{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32)},
      counter_{0u, entity, static_cast<std::uint32_t>(stream), static_cast<std::uint32_t>(tick)} {}

void RngStream::refill() noexcept {
    block_ = philox4x32_10(counter_, key_);
    ++counter_[0];
    used_ = 0;
}

std::uint32_t RngStream::next_u32() noexcept {
    if (used_ == 4) {
        refill();
    }
    return block_[static_cast<std::size_t>(used_++)];
}

std::uint64_t RngStream::next_u64() noexcept {
    const std::uint64_t hi = next_u32();
    const std::uint64_t lo = next_u32();
    return (hi << 32) | lo;
}

double RngStream::uniform() noexcept {
    // 53 high bits scaled by 2^-53: exact, identical on every IEEE-754 platform.
    return static_cast<double>(next_u64() >> 11) * 0x1.0p-53;
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

std::int64_t stochastic_round(double x, RngStream& rng) noexcept {
    if (!(x > 0.0)) {
        return 0;
    }
    const auto whole = static_cast<std::int64_t>(x);  // truncation == floor for x > 0
    const double frac = x - static_cast<double>(whole);
    return whole + (rng.uniform() < frac ? 1 : 0);
}

}  // namespace histsim
