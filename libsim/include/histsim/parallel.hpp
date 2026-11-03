// Deterministic parallel loops.
//
// parallel_for splits [0, n) into contiguous chunks and runs them on a small
// pool of worker threads. Results must not depend on how the range is split:
// each index may only write data that belongs to it (its own cohort, its own
// cell's slot), read data no other index writes in the same loop, and draw
// from its own random stream. Sums are then taken serially in index order
// afterwards (or are of integers, whose order does not matter), so a run is
// bit-identical with any number of threads, on any machine.
//
// The thread count defaults to the machine's cores (at most kMaxThreads); the
// HISTSIM_THREADS environment variable or set_threads() overrides it, and 1
// runs everything on the calling thread.
#pragma once

#include <cstddef>
#include <functional>

namespace histsim {

inline constexpr int kMaxThreads = 16;

void set_threads(int threads);  // 0 = the default
int thread_count();

// Calls body(begin, end) for contiguous chunks covering [0, n), at least
// `grain` indices each (except the last), possibly at once on several
// threads; returns when all are done. An exception thrown by a chunk is
// rethrown here. Not reentrant: do not call parallel_for from inside a body.
void parallel_for(std::size_t n, const std::function<void(std::size_t begin, std::size_t end)>& body,
                  std::size_t grain = 64);

}  // namespace histsim
