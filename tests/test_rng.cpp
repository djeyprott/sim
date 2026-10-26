#include <set>

#include "histsim/rng.hpp"
#include "testing.hpp"

using namespace histsim;

// Known-answer vectors for philox4x32 with 10 rounds, from the Random123
// distribution (kat_vectors).
TEST(philox_known_answers) {
    {
        const PhiloxCounter out = philox4x32_10({0, 0, 0, 0}, {0, 0});
        CHECK_EQ(out[0], 0x6627e8d5u);
        CHECK_EQ(out[1], 0xe169c58du);
        CHECK_EQ(out[2], 0xbc57ac4cu);
        CHECK_EQ(out[3], 0x9b00dbd8u);
    }
    {
        const PhiloxCounter out =
            philox4x32_10({0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}, {0xffffffffu, 0xffffffffu});
        CHECK_EQ(out[0], 0x408f276du);
        CHECK_EQ(out[1], 0x41c83b0eu);
        CHECK_EQ(out[2], 0xa20bc7c6u);
        CHECK_EQ(out[3], 0x6d5451fdu);
    }
    {
        const PhiloxCounter out =
            philox4x32_10({0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u}, {0xa4093822u, 0x299f31d0u});
        CHECK_EQ(out[0], 0xd16cfe09u);
        CHECK_EQ(out[1], 0x94fdccebu);
        CHECK_EQ(out[2], 0x5001e420u);
        CHECK_EQ(out[3], 0x24126ea1u);
    }
}

TEST(rng_stream_is_a_pure_function_of_its_key) {
    RngStream a(42, 120, Stream::Demography, 7);
    RngStream b(42, 120, Stream::Demography, 7);
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(a.next_u32(), b.next_u32());
    }
}

TEST(rng_streams_differ_by_each_key_part) {
    auto first = [](std::uint64_t seed, Tick tick, Stream stream, std::uint32_t entity) {
        RngStream r(seed, tick, stream, entity);
        return r.next_u64();
    };
    const std::uint64_t base = first(42, 120, Stream::Demography, 7);
    CHECK(first(43, 120, Stream::Demography, 7) != base);
    CHECK(first(42, 121, Stream::Demography, 7) != base);
    CHECK(first(42, 120, Stream::Migration, 7) != base);
    CHECK(first(42, 120, Stream::Demography, 8) != base);
    CHECK(first(42ull | (1ull << 40), 120, Stream::Demography, 7) != base);  // high seed bits count
}

TEST(rng_uniform_in_unit_interval_with_sane_mean) {
    RngStream r(1, 0, Stream::WorldGen, 0);
    double sum = 0.0;
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        const double u = r.uniform();
        CHECK(u >= 0.0 && u < 1.0);
        sum += u;
    }
    CHECK_NEAR(sum / n, 0.5, 0.005);
}

TEST(rng_below_covers_range_without_overflow) {
    RngStream r(9, 3, Stream::Migration, 1);
    std::set<std::uint32_t> seen;
    for (int i = 0; i < 2000; ++i) {
        const std::uint32_t v = r.below(6);
        CHECK(v < 6);
        seen.insert(v);
    }
    CHECK_EQ(seen.size(), std::size_t{6});
}

TEST(stochastic_round_is_unbiased) {
    RngStream r(5, 0, Stream::Demography, 0);
    std::int64_t sum = 0;
    const int n = 200000;
    for (int i = 0; i < n; ++i) {
        const std::int64_t v = stochastic_round(2.3, r);
        CHECK(v == 2 || v == 3);
        sum += v;
    }
    CHECK_NEAR(static_cast<double>(sum) / n, 2.3, 0.005);
    CHECK_EQ(stochastic_round(0.0, r), std::int64_t{0});
    CHECK_EQ(stochastic_round(-4.0, r), std::int64_t{0});
    CHECK_EQ(stochastic_round(7.0, r), std::int64_t{7});
}
