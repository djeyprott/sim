// Dynamic simulation state: everything that changes during a run and goes
// into snapshots and the state hash.
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "histsim/types.hpp"

namespace histsim {

// A per-cell field with a read buffer and a write buffer.
//
// A system reads the previous values through read() and writes new values
// through write(), then commit() makes the new values current. Because no
// system reads values it is writing in the same step, cells can be processed
// in any order, or in parallel, with the same result.
template <class T>
class DoubleBuffered {
public:
    void assign(std::size_t n, T value) {
        front_.assign(n, value);
        back_.assign(n, value);
    }

    [[nodiscard]] std::span<const T> read() const noexcept { return front_; }
    [[nodiscard]] std::span<T> write() noexcept { return back_; }

    // Start a write pass from a copy of the current values.
    void begin_from_current() { back_ = front_; }

    void commit() noexcept { std::swap(front_, back_); }

    // Direct access to the current values, for loading and initialization.
    [[nodiscard]] std::vector<T>& current() noexcept { return front_; }
    [[nodiscard]] const std::vector<T>& current() const noexcept { return front_; }

private:
    std::vector<T> front_;
    std::vector<T> back_;
};

// Append new values; never renumber existing ones (saved logs use them).
enum class EventType : std::uint32_t {
    CellSettled = 1,      // people reach a cell that was never inhabited; value = arrivals
    WorldPopulation = 2,  // world population crossed a power of ten; value = new total
};

struct Event {
    Tick tick = 0;
    EventType type = EventType::CellSettled;
    CellId cell = kInvalidCell;
    std::int64_t value = 0;
};

// Running totals that make conservation checkable: at any tick,
// population == initial_population + births - deaths.
struct Ledger {
    std::int64_t initial_population = 0;
    std::int64_t births = 0;
    std::int64_t deaths = 0;
    std::int64_t migrants = 0;  // moves between cells; does not change the total
};

struct SimState {
    Tick tick = 0;

    // M0 placeholder for cohorts: one forager headcount per cell. M1 replaces
    // this with cohorts carrying age-sex pyramids, culture and language.
    DoubleBuffered<std::uint32_t> population;
    std::vector<std::uint8_t> ever_settled;  // 1 once a cell has had people

    Ledger ledger;
    std::vector<Event> events;  // append-only, in tick order

    [[nodiscard]] std::int64_t total_population() const noexcept;
    [[nodiscard]] std::uint64_t hash() const noexcept;
};

}  // namespace histsim
