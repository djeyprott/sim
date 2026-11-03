#include "histsim/scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace histsim {

namespace {

// HISTSIM_PROFILE=1: time each system, printed to stderr at exit. Timing never
// touches the simulation.
struct Profile {
    bool on = std::getenv("HISTSIM_PROFILE") != nullptr;
    std::mutex mutex;
    std::map<std::string, double> seconds;
    ~Profile() {
        if (!on) return;
        double total = 0.0;
        for (const auto& [name, t] : seconds) total += t;
        for (const auto& [name, t] : seconds) {
            std::fprintf(stderr, "%-12s %8.3f s  %5.1f%%\n", name.c_str(), t, total > 0 ? 100.0 * t / total : 0.0);
        }
    }
};

Profile& profile() {
    static Profile p;
    return p;
}

}  // namespace

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
            if (profile().on) {
                const auto a = std::chrono::steady_clock::now();
                system->step(ctx);
                const std::chrono::duration<double> d = std::chrono::steady_clock::now() - a;
                std::lock_guard<std::mutex> lock(profile().mutex);
                profile().seconds[std::string(system->name())] += d.count();
            } else {
                system->step(ctx);
            }
        }
    }
    ++state.tick;
}

}  // namespace histsim
