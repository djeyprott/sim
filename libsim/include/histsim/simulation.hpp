// The Sim API: the one entry point the front end and the headless runner use.
//
// Callers read state through const accessors and change the run only through
// methods on this class, never by writing into World or SimState.
#pragma once

#include <cstdint>
#include <string>

#include "histsim/scheduler.hpp"
#include "histsim/state.hpp"
#include "histsim/world.hpp"

namespace histsim {

struct RunSummary {
    Tick tick = 0;
    std::int64_t population = 0;
    std::size_t inhabited_cells = 0;
    std::int64_t births = 0;
    std::int64_t deaths = 0;
    std::int64_t migrants = 0;
    std::uint64_t state_hash = 0;
};

class Simulation {
public:
    // A new run on a generated test world, populated by carrying capacity.
    static Simulation create(const WorldParams& params);

    // Resume from a snapshot. The world is regenerated from the stored
    // parameters and checked against the stored world hash.
    static Simulation load(const std::string& path);

    void save(const std::string& path) const;

    void step_ticks(Tick ticks);
    void step_years(std::int64_t years) { step_ticks(years * kTicksPerYear); }

    [[nodiscard]] const World& world() const noexcept { return world_; }
    [[nodiscard]] const SimState& state() const noexcept { return state_; }
    [[nodiscard]] Tick tick() const noexcept { return state_.tick; }
    // Whole years since the run began: the only date the interface shows.
    [[nodiscard]] std::int64_t year() const noexcept { return state_.tick / kTicksPerYear; }

    [[nodiscard]] std::uint64_t state_hash() const noexcept { return state_.hash(); }
    [[nodiscard]] RunSummary summary() const;

    // Format version written by save(). Bump it when the layout changes.
    static constexpr std::uint32_t kSnapshotVersion = 2;

private:
    Simulation() = default;
    void install_systems();

    World world_;
    SimState state_;
    Scheduler scheduler_;
};

}  // namespace histsim
