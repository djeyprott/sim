#include "histsim/state.hpp"

#include <algorithm>

#include "state_io.hpp"

namespace histsim {

void SimState::normalize_cohorts(std::size_t cell_count) {
    // Equal keys are merged by summing, so their relative order after the
    // (unstable) sort does not matter: the result is the same everywhere.
    std::sort(cohorts.begin(), cohorts.end(), [](const Cohort& a, const Cohort& b) { return a.key_less(b); });
    std::vector<Cohort> merged;
    merged.reserve(cohorts.size());
    for (const Cohort& c : cohorts) {
        if (!merged.empty() && merged.back().same_key(c)) {
            Cohort& m = merged.back();
            for (int b = 0; b < kAgeBins; ++b) {
                m.female[b] += c.female[b];
                m.male[b] += c.male[b];
            }
        } else {
            merged.push_back(c);
        }
    }
    merged.erase(std::remove_if(merged.begin(), merged.end(), [](const Cohort& c) { return c.total() == 0; }),
                 merged.end());
    cohorts = std::move(merged);
    refresh(cell_count);
}

void SimState::refresh(std::size_t cell_count) {
    cell_population.assign(cell_count, 0);
    cohort_begin.assign(cell_count + 1, 0);
    for (const Cohort& c : cohorts) {
        cell_population[c.cell] += static_cast<std::uint32_t>(c.total());
        ++cohort_begin[c.cell + 1];
    }
    for (std::size_t i = 0; i < cell_count; ++i) {
        cohort_begin[i + 1] += cohort_begin[i];
    }
}

std::int64_t SimState::total_population() const noexcept {
    std::int64_t total = 0;
    for (const Cohort& c : cohorts) total += c.total();
    return total;
}

std::uint64_t SimState::hash() const {
    io::HashArchive ar;
    io::serialize(ar, const_cast<SimState&>(*this));  // the hasher only reads
    return ar.digest();
}

}  // namespace histsim
