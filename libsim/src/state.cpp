#include "histsim/state.hpp"

#include <algorithm>

#include "state_io.hpp"

namespace histsim {

void SimState::normalize_cohorts(std::size_t cell_count) {
    const auto empty = [](const Cohort& c) { return c.total() == 0; };
    // Common case (after demography): keys are already sorted and unique, so
    // only empty cohorts need dropping.
    bool sorted = true;
    for (std::size_t i = 1; i < cohorts.size() && sorted; ++i) sorted = cohorts[i - 1].key_less(cohorts[i]);
    if (sorted) {
        cohorts.erase(std::remove_if(cohorts.begin(), cohorts.end(), empty), cohorts.end());
        refresh(cell_count);
        return;
    }

    // Sort indices, not the (large) cohorts. Stable: cohorts with equal keys
    // keep the order they were added in, so the weighted averages below are
    // computed in the same order with every standard library (std::sort's
    // order of equals varies).
    std::vector<std::uint32_t> order(cohorts.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<std::uint32_t>(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](std::uint32_t a, std::uint32_t b) { return cohorts[a].key_less(cohorts[b]); });
    std::vector<Cohort> merged;
    merged.reserve(cohorts.size());
    for (std::uint32_t idx : order) {
        const Cohort& c = cohorts[idx];
        if (!merged.empty() && merged.back().same_key(c)) {
            Cohort& m = merged.back();
            // Traits and dialect: newcomers (later in the list: migrants,
            // people who shifted) mostly learn the residents' ways. They move
            // the residents' values by the square of their share, so a few
            // newcomers change little and a crowd of them nearly everything.
            const double wm = static_cast<double>(m.total()), wc = static_cast<double>(c.total());
            if (wm + wc > 0.0) {
                const double share = wc / (wm + wc);
                const double f = share * share;
                for (std::size_t t = 0; t < kMaxTraits; ++t) m.traits[t] += f * (c.traits[t] - m.traits[t]);
                for (int d = 0; d < kDialectDims; ++d) m.dialect[d] += f * (c.dialect[d] - m.dialect[d]);
                m.farming += f * (c.farming - m.farming);
            }
            for (int b = 0; b < kAgeBins; ++b) {
                m.female[b] += c.female[b];
                m.male[b] += c.male[b];
            }
        } else if (!empty(c)) {
            merged.push_back(c);
        } else {
            // An empty cohort contributes nothing, but must not become the
            // resident that later newcomers merge into.
            continue;
        }
    }
    merged.erase(std::remove_if(merged.begin(), merged.end(), empty), merged.end());
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
