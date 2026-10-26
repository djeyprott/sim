#include "histsim/state.hpp"

#include "histsim/hash.hpp"

namespace histsim {

std::int64_t SimState::total_population() const noexcept {
    std::int64_t total = 0;
    for (std::uint32_t p : population.read()) {
        total += p;
    }
    return total;
}

std::uint64_t SimState::hash() const noexcept {
    Hasher h;
    h.i64(tick);
    h.span<std::uint32_t>(population.read());
    h.span<std::uint8_t>(ever_settled);
    h.i64(ledger.initial_population);
    h.i64(ledger.births);
    h.i64(ledger.deaths);
    h.i64(ledger.migrants);
    h.u64(events.size());
    for (const Event& e : events) {
        h.i64(e.tick);
        h.u32(static_cast<std::uint32_t>(e.type));
        h.u32(e.cell);
        h.i64(e.value);
    }
    return h.digest();
}

}  // namespace histsim
