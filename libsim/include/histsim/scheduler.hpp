// Runs systems at their own rates in a fixed order within each tick.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "histsim/rng.hpp"
#include "histsim/state.hpp"
#include "histsim/world.hpp"

namespace histsim {

// Phase order within a tick (design doc, "Simulation loop and time scales").
// Systems in a later phase see the committed results of earlier phases.
enum class Phase : std::uint8_t {
    Climate = 0,
    Environment,
    Production,
    Markets,
    Demography,
    Migration,
    CultureTech,
    Polities,
    Agents,
    Events,
    Count
};

// What a system may touch during one step.
struct StepContext {
    const World& world;
    SimState& state;
    std::uint64_t seed;
    Tick tick;

    [[nodiscard]] RngStream rng(Stream stream, std::uint32_t entity) const noexcept {
        return RngStream(seed, tick, stream, entity);
    }
};

// A simulation system. Systems hold no state of their own: everything that
// changes lives in SimState, so snapshots capture the whole run.
class System {
public:
    virtual ~System() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Phase phase() const noexcept = 0;
    // Run every `period` ticks, at ticks where tick % period == offset.
    [[nodiscard]] virtual Tick period() const noexcept = 0;
    [[nodiscard]] virtual Tick offset() const noexcept { return 0; }
    virtual void step(StepContext& ctx) = 0;
};

class Scheduler {
public:
    // Systems in the same phase run in the order they were added.
    void add(std::unique_ptr<System> system);

    // Run every system due at state.tick, then advance the tick by one.
    void run_tick(const World& world, SimState& state, std::uint64_t seed);

    [[nodiscard]] const std::vector<std::unique_ptr<System>>& systems() const noexcept { return systems_; }

private:
    std::vector<std::unique_ptr<System>> systems_;  // sorted by phase, stable
};

}  // namespace histsim
