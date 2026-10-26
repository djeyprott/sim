// Stable 64-bit hashing of simulation state.
//
// Used for determinism checks: two runs with the same seed and inputs must
// produce the same state hash on every platform. Values are fed in a fixed
// little-endian byte order, independent of the host.
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace histsim {

class Hasher {
public:
    void bytes(const void* data, std::size_t size) noexcept {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            state_ ^= p[i];
            state_ *= kPrime;
        }
    }

    void u8(std::uint8_t v) noexcept { bytes(&v, 1); }

    void u32(std::uint32_t v) noexcept {
        const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                    static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
        bytes(b, 4);
    }

    void u64(std::uint64_t v) noexcept {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32));
    }

    void i64(std::int64_t v) noexcept { u64(static_cast<std::uint64_t>(v)); }

    void f64(double v) noexcept {
        std::uint64_t bits;
        static_assert(sizeof bits == sizeof v);
        std::memcpy(&bits, &v, sizeof bits);
        u64(bits);
    }

    void str(std::string_view s) noexcept {
        u64(s.size());
        bytes(s.data(), s.size());
    }

    template <class T>
    void span(std::span<const T> values) noexcept {
        u64(values.size());
        for (const T& v : values) {
            if constexpr (std::is_same_v<T, double>) {
                f64(v);
            } else if constexpr (std::is_same_v<T, std::uint8_t>) {
                u8(v);
            } else if constexpr (sizeof(T) == 4) {
                u32(static_cast<std::uint32_t>(v));
            } else {
                static_assert(sizeof(T) == 8, "unsupported element type");
                u64(static_cast<std::uint64_t>(v));
            }
        }
    }

    [[nodiscard]] std::uint64_t digest() const noexcept { return state_; }

private:
    // FNV-1a, 64-bit. Not cryptographic; it only has to be stable and sensitive.
    static constexpr std::uint64_t kOffset = 0xCBF29CE484222325ull;
    static constexpr std::uint64_t kPrime = 0x00000100000001B3ull;
    std::uint64_t state_ = kOffset;
};

}  // namespace histsim
