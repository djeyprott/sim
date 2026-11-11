#include "histsim/state.hpp"

#include <algorithm>

#include "histsim/parallel.hpp"
#include "state_io.hpp"

namespace histsim {

void SimState::normalize_cohorts(std::size_t cell_count) {
    static const std::vector<Cohort> none;
    normalize_cohorts(cell_count, none);
}

void SimState::normalize_cohorts(std::size_t cell_count, const std::vector<Cohort>& extra) {
    const auto empty = [](const Cohort& c) { return c.total() == 0; };
    // The list is the cohorts followed by `extra`.
    const std::size_t own = cohorts.size(), total = own + extra.size();
    const auto at = [&](std::size_t i) -> const Cohort& { return i < own ? cohorts[i] : extra[i - own]; };
    if (!extra.empty() && (own == 0 || cohorts[own - 1].key_less(extra.front()))) {
        // Arrivals that could simply follow: rare; add them and go on.
        bool in_order = true;
        for (std::size_t i = 1; i < extra.size() && in_order; ++i) in_order = extra[i - 1].key_less(extra[i]);
        if (in_order) {
            cohorts.insert(cohorts.end(), extra.begin(), extra.end());
            normalize_cohorts(cell_count, std::vector<Cohort>{});
            return;
        }
    }
    // Common case (after demography): keys are already sorted and unique, so
    // only empty cohorts need dropping.
    std::size_t prefix = total == 0 ? 0 : 1;  // length of the sorted, unique start
    while (prefix < total && at(prefix - 1).key_less(at(prefix))) ++prefix;
    if (prefix == total) {
        // (Only with no extra: sorted arrivals were added above.)
        // Drop empty cohorts: each chunk counts its own, then copies what it
        // keeps to its place in a spare buffer (in parallel; order kept).
        const std::size_t m = cohorts.size();
        const std::size_t chunk = 4096, chunks = (m + chunk - 1) / chunk;
        std::vector<std::size_t> kept(chunks + 1, 0);
        parallel_for(chunks, [&](std::size_t k_begin, std::size_t k_end) {
            for (std::size_t k = k_begin; k < k_end; ++k) {
                std::size_t count = 0;
                for (std::size_t i = k * chunk; i < std::min(m, (k + 1) * chunk); ++i) count += empty(cohorts[i]) ? 0 : 1;
                kept[k + 1] = count;
            }
        }, 1);
        for (std::size_t k = 0; k < chunks; ++k) kept[k + 1] += kept[k];
        if (kept[chunks] != m) {
            thread_local std::vector<Cohort> spare_kept;
            std::vector<Cohort> out = std::move(spare_kept);
            out.resize(kept[chunks]);
            parallel_for(chunks, [&](std::size_t k_begin, std::size_t k_end) {
                for (std::size_t k = k_begin; k < k_end; ++k) {
                    std::size_t to = kept[k];
                    for (std::size_t i = k * chunk; i < std::min(m, (k + 1) * chunk); ++i) {
                        if (!empty(cohorts[i])) out[to++] = cohorts[i];
                    }
                }
            }, 1);
            std::swap(cohorts, out);
            spare_kept = std::move(out);
        }
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
    std::vector<Key> keys(total);
    parallel_for(total, [&](std::size_t i_begin, std::size_t i_end) {
        for (std::size_t i = i_begin; i < i_end; ++i) {
            const Cohort& c = at(i);
            keys[i] = {c.cell, c.culture, c.language, static_cast<std::uint32_t>(i)};
        }
    }, 4096);
    const auto key_less = [](const Key& x, const Key& y) {
        if (x.cell != y.cell) return x.cell < y.cell;
        if (x.culture != y.culture) return x.culture < y.culture;
        if (x.language != y.language) return x.language < y.language;
        return x.index < y.index;
    };
    // The start of the list is already in order: sort the rest and merge.
    std::sort(keys.begin() + static_cast<std::ptrdiff_t>(prefix), keys.end(), key_less);
    std::inplace_merge(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(prefix), keys.end(), key_less);
    std::vector<std::uint32_t> order(total);
    for (std::size_t i = 0; i < keys.size(); ++i) order[i] = keys[i].index;

    // Runs of equal keys merge into one cohort each, every run on its own
    // (in parallel), in list order within the run: the same result with any
    // number of threads.
    std::vector<std::size_t> run_begin;
    run_begin.reserve(total / 2 + 1);
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
            for (std::size_t i = run_begin[r]; i < run_begin[r + 1] && !any; ++i) any = !empty(at(order[i]));
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
                const Cohort& c = at(order[i]);
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
    // Cohorts are sorted by cell: cell c's begin at the first cohort of a
    // cell >= c. Each cohort that starts a cell writes the begins of the cells
    // since the previous cohort's, so the writes never overlap (in parallel).
    const std::size_t m = cohorts.size();
    cell_population.assign(cell_count, 0);
    cohort_begin.assign(cell_count + 1, 0);
    parallel_for(m, [&](std::size_t i_begin, std::size_t i_end) {
        for (std::size_t i = i_begin; i < i_end; ++i) {
            const std::size_t from = i == 0 ? 0 : static_cast<std::size_t>(cohorts[i - 1].cell) + 1;
            for (std::size_t c = from; c <= cohorts[i].cell; ++c) cohort_begin[c] = static_cast<std::uint32_t>(i);
        }
    }, 4096);
    const std::size_t after = m == 0 ? 0 : static_cast<std::size_t>(cohorts[m - 1].cell) + 1;
    for (std::size_t c = after; c <= cell_count; ++c) cohort_begin[c] = static_cast<std::uint32_t>(m);
    parallel_for(cell_count, [&](std::size_t c_begin, std::size_t c_end) {
        for (std::size_t c = c_begin; c < c_end; ++c) {
            std::uint32_t pop = 0;
            for (auto i = cohort_begin[c]; i < cohort_begin[c + 1]; ++i) pop += static_cast<std::uint32_t>(cohorts[i].total());
            cell_population[c] = pop;
        }
    }, 1024);
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
