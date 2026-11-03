#include "histsim/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace histsim {

namespace {

int default_threads() {
    if (const char* env = std::getenv("HISTSIM_THREADS")) {
        const int t = std::atoi(env);
        if (t >= 1) return std::min(t, kMaxThreads);
    }
    const unsigned hw = std::thread::hardware_concurrency();
    return std::clamp(static_cast<int>(hw == 0 ? 1 : hw), 1, kMaxThreads);
}

// One job at a time; a caller that finds the pool busy (another simulation
// on another thread) runs its loop by itself.
class Pool {
public:
    ~Pool() { stop(); }

    void resize(int threads) {
        std::lock_guard<std::mutex> busy(submit_);
        stop();
        wanted_ = threads;
    }
    int size() {
        if (wanted_ == 0) wanted_ = default_threads();
        return wanted_;
    }

    void run(std::size_t n, const std::function<void(std::size_t, std::size_t)>& body, std::size_t grain) {
        thread_local bool inside = false;
        const int threads = size();
        std::unique_lock<std::mutex> busy(submit_, std::try_to_lock);
        if (threads <= 1 || n <= grain || inside || !busy.owns_lock()) {
            body(0, n);
            return;
        }
        start(threads - 1);
        const std::size_t chunks_wanted = static_cast<std::size_t>(threads) * 4;
        chunk_ = std::max(grain, (n + chunks_wanted - 1) / chunks_wanted);
        n_ = n;
        body_ = &body;
        next_.store(0);
        error_ = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = static_cast<int>(workers_.size());
            ++generation_;
        }
        wake_.notify_all();
        inside = true;
        work();
        inside = false;
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0; });
        body_ = nullptr;
        if (error_) std::rethrow_exception(error_);
    }

private:
    void start(int helpers) {
        if (static_cast<int>(workers_.size()) == helpers) return;
        stop();
        quit_ = false;
        for (int i = 0; i < helpers; ++i) {
            workers_.emplace_back([this] {
                std::uint64_t seen = 0;
                for (;;) {
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        wake_.wait(lock, [&] { return quit_ || generation_ != seen; });
                        if (quit_) return;
                        seen = generation_;
                    }
                    work();
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (--pending_ == 0) done_.notify_one();
                    }
                }
            });
        }
    }
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        wake_.notify_all();
        for (std::thread& t : workers_) t.join();
        workers_.clear();
    }
    void work() {
        for (;;) {
            const std::size_t begin = next_.fetch_add(chunk_);
            if (begin >= n_) return;
            try {
                (*body_)(begin, std::min(n_, begin + chunk_));
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!error_) error_ = std::current_exception();
            }
        }
    }

    std::mutex submit_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    std::vector<std::thread> workers_;
    bool quit_ = false;
    std::uint64_t generation_ = 0;
    int pending_ = 0;
    int wanted_ = 0;
    std::size_t n_ = 0, chunk_ = 1;
    std::atomic<std::size_t> next_{0};
    const std::function<void(std::size_t, std::size_t)>* body_ = nullptr;
    std::exception_ptr error_;
};

Pool& pool() {
    static Pool p;
    return p;
}

}  // namespace

void set_threads(int threads) { pool().resize(threads <= 0 ? default_threads() : std::min(threads, kMaxThreads)); }

int thread_count() { return pool().size(); }

void parallel_for(std::size_t n, const std::function<void(std::size_t, std::size_t)>& body, std::size_t grain) {
    if (n == 0) return;
    pool().run(n, body, std::max<std::size_t>(1, grain));
}

}  // namespace histsim
