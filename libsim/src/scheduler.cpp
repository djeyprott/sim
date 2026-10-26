#include "histsim/scheduler.hpp"

#include <algorithm>
#include <stdexcept>

namespace histsim {

void Scheduler::add(std::unique_ptr<System> system) {
    if (!system || system->period() <= 0 || system->offset() < 0 || system->offset() >= system->period()) {
        throw std::invalid_argument("system needs period > 0 and 0 <= offset < period");
    }
    const Phase phase = system->phase();
    // Insert after the last system of the same or an earlier phase.
    const auto pos = std::upper_bound(systems_.begin(), systems_.end(), phase,
                                      [](Phase p, const std::unique_ptr<System>& s) { return p < s->phase(); });
    systems_.insert(pos, std::move(system));
}

void Scheduler::run_tick(const World& world, SimState& state, std::uint64_t seed) {
    StepContext ctx{world, state, seed, state.tick};
    for (const auto& system : systems_) {
        if (state.tick % system->period() == system->offset()) {
            system->step(ctx);
        }
    }
    ++state.tick;
}

}  // namespace histsim
