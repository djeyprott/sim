#include "histsim/state.hpp"

#include <algorithm>

#include "histsim/parallel.hpp"
#include "state_io.hpp"

namespace histsim {

void SimState::normalize_cohorts(std::size_t cell_count) {
    const auto empty = [](const Cohort& c) { return c.total() == 0; };
    // Common case (after demography): keys are already sorted and unique, so
    // only empty cohorts need dropping.
    std::size_t prefix = cohorts.empty() ? 0 : 1;  // length of the sorted, unique start
    while (prefix < cohorts.size() && cohorts[prefix - 1].key_less(cohorts[prefix])) ++prefix;
    if (prefix == cohorts.size()) {
        cohorts.erase(std::remove_if(cohorts.begin(), cohorts.end(), empty), cohorts.end());
        refresh(cell_count);
        return;
    }

    // Sort compact keys, not the (large) cohorts. Ties go by position in the
    // list, so cohorts with equal keys keep the order they were added in and
    // the weighted averages below are computed in the same order with every
    // standard library.
    struct Key {
        CellId cell;
        std::uint32_t culture, language, index;
    };
    std::vector<Key> keys(cohorts.size());
    for (std::size_t i = 0; i < cohorts.size(); ++i) {
        keys[i] = {cohorts[i].cell, cohorts[i].culture, cohorts[i].language, static_cast<std::uint32_t>(i)};
    }
    const auto key_less = [](const Key& x, const Key& y) {
        if (x.cell != y.cell) return x.cell < y.cell;
        if (x.culture != y.culture) return x.culture < y.culture;
        if (x.language != y.language) return x.language < y.language;
        return x.index < y.index;
    };
    // The start of the list is already in order: sort the rest and merge.
    std::sort(keys.begin() + static_cast<std::ptrdiff_t>(prefix), keys.end(), key_less);
    std::inplace_merge(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(prefix), keys.end(), key_less);
    std::vector<std::uint32_t> order(cohorts.size());
    for (std::size_t i = 0; i < keys.size(); ++i) order[i] = keys[i].index;

    // Runs of equal keys merge into one cohort each, every run on its own
    // (in parallel), in list order within the run: the same result with any
    // number of threads.
    std::vector<std::size_t> run_begin;
    run_begin.reserve(cohorts.size() / 2 + 1);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i == 0 || keys[i - 1].cell != keys[i].cell || keys[i - 1].culture != keys[i].culture ||
            keys[i - 1].language != keys[i].language) {
            run_begin.push_back(i);
        }
    }
    run_begin.push_back(order.size());
    keys = {};
    const std::size_t runs = run_begin.size() - 1;
    // A run is kept if anyone is in it; its place in the merged list follows.
    std::vector<std::uint32_t> place(runs + 1, 0);
    parallel_for(runs, [&](std::size_t r_begin, std::size_t r_end) {
        for (std::size_t r = r_begin; r < r_end; ++r) {
            bool any = false;
            for (std::size_t i = run_begin[r]; i < run_begin[r + 1] && !any; ++i) any = !empty(cohorts[order[i]]);
            place[r + 1] = any ? 1 : 0;
        }
    }, 256);
    for (std::size_t r = 0; r < runs; ++r) place[r + 1] += place[r];
    // The merged list is built in a buffer kept from the last call (the
    // cohorts it held then), sparing a large allocation every time.
    thread_local std::vector<Cohort> spare;
    std::vector<Cohort> merged = std::move(spare);
    merged.resize(place[runs]);
    parallel_for(runs, [&](std::size_t r_begin, std::size_t r_end) {
        for (std::size_t r = r_begin; r < r_end; ++r) {
            if (place[r + 1] == place[r]) continue;
            bool has = false;
            Cohort& m = merged[place[r]];
            for (std::size_t i = run_begin[r]; i < run_begin[r + 1]; ++i) {
                const Cohort& c = cohorts[order[i]];
                if (!has) {
                    // An empty cohort contributes nothing, but must not become
                    // the resident that later newcomers merge into.
                    if (empty(c)) continue;
                    m = c;
                    has = true;
                    continue;
                }
                // Traits and dialect: newcomers (later in the list: migrants,
                // people who shifted) mostly learn the residents' ways. They
                // move the residents' values by the square of their share, so
                // a few newcomers change little and a crowd of them nearly
                // everything.
                const double wm = static_cast<double>(m.total()), wc = static_cast<double>(c.total());
                if (wm + wc > 0.0 && (m.bonded != 0.0 || c.bonded != 0.0)) m.bonded = (wm * m.bonded + wc * c.bonded) / (wm + wc);
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
            }
        }
    }, 256);
    std::swap(cohorts, merged);
    spare = std::move(merged);
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
    // A market dies with its people: a cell emptied since the markets ran
    // has no goods on offer.
    if (goods_available.size() == cell_count) {
        for (std::size_t i = 0; i < cell_count; ++i) {
            if (cell_population[i] == 0) goods_available[i] = 0;
        }
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
